#include "RenderGraph.h"
#include <algorithm>

namespace Fujin {

void RenderGraph::BeginFrame() {
    m_resources.clear();
    m_passes.clear();
    m_order.clear();
}

RGHandle RenderGraph::Import(const char* name,
                              ID3D12Resource* res,
                              D3D12_RESOURCE_STATES initialState) {
    RGHandle h;
    h.idx = static_cast<uint32_t>(m_resources.size());

    ResourceEntry e;
    e.name     = name;
    e.resource = res;

    auto it = m_trackedStates.find(res);
    e.state = (it != m_trackedStates.end()) ? it->second : initialState;

    m_resources.push_back(e);
    return h;
}

void RenderGraph::AddPass(const char* name,
                           std::vector<RGHandle> reads,
                           std::vector<RGWrite>  writes,
                           std::function<void(ID3D12GraphicsCommandList*)> execute) {
    uint32_t passIdx = static_cast<uint32_t>(m_passes.size());

    for (auto& w : writes)
        if (w.handle.IsValid())
            m_resources[w.handle.idx].lastWritePass = passIdx;

    PassNode node;
    node.name    = name;
    node.reads   = std::move(reads);
    node.writes  = std::move(writes);
    node.execute = std::move(execute);
    m_passes.push_back(std::move(node));
}

void RenderGraph::Compile() {
    uint32_t n = static_cast<uint32_t>(m_passes.size());

    // Build adjacency: edge A→B if B reads what A last wrote.
    std::vector<std::vector<uint32_t>> adj(n);
    std::vector<uint32_t> indegree(n, 0);

    for (uint32_t b = 0; b < n; ++b) {
        for (auto h : m_passes[b].reads) {
            if (!h.IsValid()) continue;
            uint32_t a = m_resources[h.idx].lastWritePass;
            if (a != UINT32_MAX && a != b) {
                adj[a].push_back(b);
                ++indegree[b];
            }
        }
    }

    // Kahn's topological sort.
    std::vector<uint32_t> queue;
    queue.reserve(n);
    for (uint32_t i = 0; i < n; ++i)
        if (indegree[i] == 0) queue.push_back(i);

    m_order.clear();
    size_t front = 0;
    while (front < queue.size()) {
        uint32_t cur = queue[front++];
        m_order.push_back(cur);
        for (uint32_t next : adj[cur])
            if (--indegree[next] == 0) queue.push_back(next);
    }

    // Append any unreachable passes (should not happen in practice).
    for (uint32_t i = 0; i < n; ++i) {
        if (std::find(m_order.begin(), m_order.end(), i) == m_order.end())
            m_order.push_back(i);
    }
}

void RenderGraph::DoBarrier(ID3D12GraphicsCommandList* cmd,
                             RGHandle h,
                             D3D12_RESOURCE_STATES newState) {
    if (!h.IsValid()) return;
    auto& e = m_resources[h.idx];
    if (!e.resource || e.state == newState) return;

    D3D12_RESOURCE_BARRIER barrier      = {};
    barrier.Type                        = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource        = e.resource;
    barrier.Transition.StateBefore      = e.state;
    barrier.Transition.StateAfter       = newState;
    barrier.Transition.Subresource      = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd->ResourceBarrier(1, &barrier);

    e.state = newState;
    m_trackedStates[e.resource] = newState;
}

void RenderGraph::Execute(ID3D12GraphicsCommandList* cmd) {
    constexpr D3D12_RESOURCE_STATES kSRVState =
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

    for (uint32_t passIdx : m_order) {
        auto& pass = m_passes[passIdx];

        for (auto& w : pass.writes)
            DoBarrier(cmd, w.handle, w.targetState);

        for (auto h : pass.reads)
            DoBarrier(cmd, h, kSRVState);

        pass.execute(cmd);
    }
}

void RenderGraph::TransitionResource(ID3D12GraphicsCommandList* cmd,
                                      RGHandle h,
                                      D3D12_RESOURCE_STATES newState) {
    DoBarrier(cmd, h, newState);
}

void RenderGraph::ForgetResource(ID3D12Resource* res) {
    m_trackedStates.erase(res);
}

} // namespace Fujin
