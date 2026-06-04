#include "GpuProfiler.h"

namespace Fujin {

bool GpuProfiler::Initialize(ID3D12Device* device, ID3D12CommandQueue* queue,
                             uint32_t maxScopesPerFrame, uint32_t framesInFlight) {
    m_maxScopes      = maxScopesPerFrame;
    m_framesInFlight = framesInFlight;

    // Ticks per second for converting timestamp deltas to milliseconds.
    if (FAILED(queue->GetTimestampFrequency(&m_frequency)) || m_frequency == 0)
        return false;

    const uint32_t totalQueries = m_maxScopes * 2 * m_framesInFlight;

    D3D12_QUERY_HEAP_DESC qd = {};
    qd.Type  = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    qd.Count = totalQueries;
    if (FAILED(device->CreateQueryHeap(&qd, IID_PPV_ARGS(&m_heap))))
        return false;

    // Readback buffer: one UINT64 per query, persistently mapped.
    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_READBACK;

    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width            = sizeof(uint64_t) * totalQueries;
    rd.Height           = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels        = 1;
    rd.Format           = DXGI_FORMAT_UNKNOWN;
    rd.SampleDesc.Count = 1;
    rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    if (FAILED(device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&m_readback))))
        return false;

    if (FAILED(m_readback->Map(0, nullptr, reinterpret_cast<void**>(&m_mapped))))
        return false;

    m_frameScopes.assign(m_framesInFlight, {});
    m_frameWritten.assign(m_framesInFlight, false);
    m_valid = true;
    return true;
}

void GpuProfiler::Shutdown() {
    if (m_readback && m_mapped) {
        m_readback->Unmap(0, nullptr);
        m_mapped = nullptr;
    }
    m_readback.Reset();
    m_heap.Reset();
}

void GpuProfiler::BeginFrame(uint32_t frameIndex) {
    if (!m_valid) return;
    m_frameIndex = frameIndex;

    // Read back the results written the last time this slot was used. The fence wait
    // in GraphicsDevice::BeginFrame already guaranteed that submission completed, so
    // its resolved timestamps are valid (no extra GPU stall here).
    if (m_frameWritten[frameIndex]) {
        const uint32_t base   = frameIndex * m_maxScopes * 2;
        const auto&    scopes = m_frameScopes[frameIndex];
        m_results.clear();
        m_results.reserve(scopes.size());
        for (uint32_t s = 0; s < scopes.size(); ++s) {
            uint64_t t0 = m_mapped[base + 2 * s];
            uint64_t t1 = m_mapped[base + 2 * s + 1];
            double   ms = (t1 > t0) ? double(t1 - t0) / double(m_frequency) * 1000.0 : 0.0;
            m_results.push_back({ scopes[s].name, ms, scopes[s].depth });
        }
    }

    // Reset this frame's recording state.
    m_scopeCount = 0;
    m_depth      = 0;
    m_scopes.clear();
    m_stack.clear();
}

void GpuProfiler::BeginScope(ID3D12GraphicsCommandList* cmd, const char* name) {
    if (!m_valid || !m_enabled || m_scopeCount >= m_maxScopes) return;
    uint32_t id = m_scopeCount++;
    m_scopes.push_back({ name, m_depth });
    m_stack.push_back(id);
    ++m_depth;
    uint32_t q = m_frameIndex * m_maxScopes * 2 + id * 2;
    cmd->EndQuery(m_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, q);
}

void GpuProfiler::EndScope(ID3D12GraphicsCommandList* cmd) {
    if (!m_valid || !m_enabled || m_stack.empty()) return;
    uint32_t id = m_stack.back();
    m_stack.pop_back();
    if (m_depth > 0) --m_depth;
    uint32_t q = m_frameIndex * m_maxScopes * 2 + id * 2 + 1;
    cmd->EndQuery(m_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, q);
}

void GpuProfiler::EndFrame(ID3D12GraphicsCommandList* cmd) {
    if (!m_valid) return;
    // Even with nothing recorded, mark the slot so BeginFrame reads a consistent
    // (empty) result set instead of stale timestamps from an older enabled frame.
    if (m_scopeCount == 0) {
        m_frameScopes[m_frameIndex].clear();
        m_frameWritten[m_frameIndex] = true;
        return;
    }
    const uint32_t base = m_frameIndex * m_maxScopes * 2;
    cmd->ResolveQueryData(m_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                          base, m_scopeCount * 2,
                          m_readback.Get(), sizeof(uint64_t) * base);
    m_frameScopes[m_frameIndex]  = m_scopes;   // meta needed to interpret the readback next cycle
    m_frameWritten[m_frameIndex] = true;
}

} // namespace Fujin
