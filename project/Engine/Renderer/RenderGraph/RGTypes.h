#pragma once
#include <d3d12.h>
#include <cstdint>

namespace Fujin {

struct RGHandle {
    static constexpr uint32_t kInvalid = UINT32_MAX;
    uint32_t idx = kInvalid;
    bool IsValid() const { return idx != kInvalid; }
    bool operator==(RGHandle o) const { return idx == o.idx; }
    bool operator!=(RGHandle o) const { return idx != o.idx; }
};

// Write access descriptor — bundles a handle with the required resource state
struct RGWrite {
    RGHandle              handle;
    D3D12_RESOURCE_STATES targetState;
};

inline RGWrite AsRTV(RGHandle h) { return {h, D3D12_RESOURCE_STATE_RENDER_TARGET}; }
inline RGWrite AsDSV(RGHandle h) { return {h, D3D12_RESOURCE_STATE_DEPTH_WRITE};  }
inline RGWrite AsUAV(RGHandle h) { return {h, D3D12_RESOURCE_STATE_UNORDERED_ACCESS}; }

} // namespace Fujin
