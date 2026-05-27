#pragma once
#include "RGTypes.h"
#include <d3d12.h>
#include <vector>
#include <string>
#include <functional>
#include <unordered_map>

namespace Fujin {

// Lightweight render graph that tracks D3D12 resource states and inserts
// barriers automatically before each registered pass.
//
// Usage per frame:
//   BeginFrame()
//   Import(...)        x N
//   AddPass(...)       x N
//   Compile()          -- topological sort by read/write dependencies
//   Execute(cmd)       -- insert barriers + run each pass's execute callback
//   TransitionResource(...) -- optional post-frame state restoration
class RenderGraph {
public:
    void BeginFrame();

    // Register an externally-owned resource. initialState is used on the
    // very first call; subsequent calls use the state tracked from the
    // previous frame.
    RGHandle Import(const char* name,
                    ID3D12Resource* res,
                    D3D12_RESOURCE_STATES initialState);

    // Register a pass. The graph transitions:
    //   reads  → PIXEL_SHADER_RESOURCE | NON_PIXEL_SHADER_RESOURCE
    //   writes → the state embedded in each RGWrite
    // All transitions are inserted before the execute callback fires.
    void AddPass(const char* name,
                 std::vector<RGHandle> reads,
                 std::vector<RGWrite>  writes,
                 std::function<void(ID3D12GraphicsCommandList*)> execute);

    // Topological sort of passes by resource dependencies.
    void Compile();

    // Insert barriers and run passes in sorted order.
    void Execute(ID3D12GraphicsCommandList* cmd);

    // Explicit one-off transition (e.g. restore depth after final pass for ImGui).
    void TransitionResource(ID3D12GraphicsCommandList* cmd,
                            RGHandle h,
                            D3D12_RESOURCE_STATES newState);

    // Call before releasing/recreating a resource so stale pointer entries don't
    // cause state-mismatch barriers on the next Import (e.g. after GBuffer resize).
    void ForgetResource(ID3D12Resource* res);

    // Read-only accessors for editor visualization (valid after Compile())
    uint32_t GetPassCount() const { return static_cast<uint32_t>(m_passes.size()); }
    const std::string& GetPassName(uint32_t i) const { return m_passes[i].name; }
    const std::vector<RGHandle>& GetPassReads(uint32_t i) const { return m_passes[i].reads; }
    const std::vector<RGWrite>&  GetPassWrites(uint32_t i) const { return m_passes[i].writes; }
    const std::string& GetResourceName(RGHandle h) const { return m_resources[h.idx].name; }
    uint32_t GetResourceLastWritePass(RGHandle h) const { return m_resources[h.idx].lastWritePass; }
    const std::vector<uint32_t>& GetExecutionOrder() const { return m_order; }

private:
    struct ResourceEntry {
        std::string           name;
        ID3D12Resource*       resource      = nullptr;
        D3D12_RESOURCE_STATES state         = D3D12_RESOURCE_STATE_COMMON;
        uint32_t              lastWritePass = UINT32_MAX;
    };

    struct PassNode {
        std::string                                          name;
        std::vector<RGHandle>                                reads;
        std::vector<RGWrite>                                 writes;
        std::function<void(ID3D12GraphicsCommandList*)>      execute;
    };

    void DoBarrier(ID3D12GraphicsCommandList* cmd,
                   RGHandle h,
                   D3D12_RESOURCE_STATES newState);

    std::vector<ResourceEntry>  m_resources;
    std::vector<PassNode>       m_passes;
    std::vector<uint32_t>       m_order;

    // Persists across frames so cross-frame state transitions are tracked correctly.
    std::unordered_map<ID3D12Resource*, D3D12_RESOURCE_STATES> m_trackedStates;
};

} // namespace Fujin
