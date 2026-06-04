#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>
#include <vector>
#include <string>

namespace Fujin {

using Microsoft::WRL::ComPtr;

// GPU timing via timestamp queries, ring-buffered over frames-in-flight.
//
// Each measured scope records a begin+end timestamp. Results are read back
// `framesInFlight` frames later (once the GPU has finished that frame), so
// GetResults() reflects a recent — but not the current — frame. This avoids any
// GPU stall: we never wait on the queries, we just read whatever the slot held
// the last time its turn came around (BeginFrame relies on GraphicsDevice having
// already fence-waited that slot's previous submission).
//
// Scopes may nest; `depth` is tracked for UI indentation. Disabling the profiler
// skips all EndQuery calls (zero overhead) — toggle it on frame boundaries.
class GpuProfiler {
public:
    struct Result {
        std::string name;
        double      ms;
        uint32_t    depth;
    };

    bool Initialize(ID3D12Device* device, ID3D12CommandQueue* queue,
                    uint32_t maxScopesPerFrame, uint32_t framesInFlight);
    void Shutdown();

    // Frame lifecycle (drive from GraphicsDevice::BeginFrame / EndFrame).
    void BeginFrame(uint32_t frameIndex);          // reads back this slot's prior results
    void EndFrame(ID3D12GraphicsCommandList* cmd); // resolves this frame's queries (call before Close)

    // Open/close a measured scope (may nest).
    void BeginScope(ID3D12GraphicsCommandList* cmd, const char* name);
    void EndScope(ID3D12GraphicsCommandList* cmd);

    const std::vector<Result>& GetResults() const { return m_results; }
    bool IsEnabled() const { return m_enabled; }
    void SetEnabled(bool e) { m_enabled = e; }

private:
    struct Scope { std::string name; uint32_t depth; };

    ComPtr<ID3D12QueryHeap> m_heap;
    ComPtr<ID3D12Resource>  m_readback;        // framesInFlight * 2*maxScopes UINT64s
    uint64_t*               m_mapped = nullptr; // persistent map of m_readback

    uint32_t m_maxScopes      = 0;
    uint32_t m_framesInFlight = 0;
    uint64_t m_frequency      = 0;   // timestamp ticks per second
    bool     m_enabled        = true;
    bool     m_valid          = false; // Initialize() succeeded (guards all methods)

    // Current-frame recording state.
    uint32_t              m_frameIndex = 0;
    uint32_t              m_scopeCount = 0;
    uint32_t              m_depth      = 0;
    std::vector<Scope>    m_scopes;   // recorded this frame, indexed by scope id
    std::vector<uint32_t> m_stack;    // ids of currently-open scopes

    // Per-slot bookkeeping so BeginFrame knows how to interpret the readback.
    std::vector<std::vector<Scope>> m_frameScopes;
    std::vector<bool>               m_frameWritten;

    std::vector<Result> m_results;    // last completed frame's results (for the UI)
};

} // namespace Fujin
