#pragma once
// DDGI irradiance probe volume (Stage C of the dynamic-GI roadmap).
//   A regular 3D grid of probes, each storing an SH9 (L2) radiance projection per RGB channel.
//   The apply pass (DdgiApply.CS) samples the 8 surrounding probes (trilinear) and evaluates the SH
//   along the surface normal to get indirect irradiance — this is the OFF-SCREEN GI that SSGI cannot
//   capture. Header-only (like RotatorComponent) so it needs no vcxproj entry; SceneRenderer owns one.
//
//   Capture (C-2) is screen-space radiance injection: each frame the lit scene's radiance is scattered
//   (atomically) into the nearest probe's SH accumulator, then a resolve pass temporally blends it into
//   the probe buffer. Because the result persists in the probes, geometry that has been on screen keeps
//   lighting the scene after it leaves the view. The probe buffer is therefore a DEFAULT-heap UAV (the
//   resolve compute pass writes it); the int accumulator is a separate DEFAULT-heap UAV.
#include "Engine/Graphics/GraphicsDevice.h"
#include "Engine/Math/Math.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>

namespace Fujin {

// Matches `struct ProbeSH { float sh[27]; float pad; }` in the DDGI shaders (112-byte stride).
struct ProbeSH {
    float sh[27]; // 9 SH coeffs × RGB, laid out as sh[coeff*3 + channel]
    float pad;
};
static_assert(sizeof(ProbeSH) == 112, "ProbeSH must match HLSL 112-byte stride");

// Per-probe int accumulator: 27 fixed-point SH sums + 1 sample count. See DdgiInject/Resolve.CS.
static constexpr uint32_t DDGI_ACCUM_INTS = 28;

class DdgiVolume {
public:
    // Grid configuration (world space). Defaults cover the test scene generously.
    Vector3  Origin  = { -16.0f, -2.0f, -16.0f }; // world position of probe (0,0,0)
    Vector3  Spacing = {   2.0f,  2.0f,   2.0f };  // world distance between adjacent probes
    uint32_t Dims[3] = { 16, 8, 16 };              // probe count per axis

    bool Initialize(GraphicsDevice& gfx) {
        ID3D12Device* dev = gfx.GetDevice();
        m_numProbes = Dims[0] * Dims[1] * Dims[2];

        // Probe SH buffer (DEFAULT heap, UAV + SRV). Created in NON_PIXEL_SHADER_RESOURCE; the first
        // resolve transitions it to UAV. Contents start as zero (DEFAULT heap is zero-initialised),
        // so before any capture EvalSH() returns 0 → DDGI adds nothing until probes fill in.
        if (!CreateDefaultBuffer(dev, (UINT64)m_numProbes * sizeof(ProbeSH),
                                 D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, m_probes))
            return false;
        m_probeState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

        if (!CreateDefaultBuffer(dev, (UINT64)m_numProbes * DDGI_ACCUM_INTS * sizeof(int),
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS, m_accum))
            return false;

        // Probe SRV (StructuredBuffer<ProbeSH>) + UAV (RWStructuredBuffer<ProbeSH>).
        m_probeSRVSlot = gfx.AllocateSRVSlot();
        m_probeUAVSlot = gfx.AllocateSRVSlot();
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC s = {};
            s.Format = DXGI_FORMAT_UNKNOWN; s.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            s.Buffer.NumElements = m_numProbes; s.Buffer.StructureByteStride = sizeof(ProbeSH);
            dev->CreateShaderResourceView(m_probes.Get(), &s, gfx.GetSRVCPUHandle(m_probeSRVSlot));
            D3D12_UNORDERED_ACCESS_VIEW_DESC u = {};
            u.Format = DXGI_FORMAT_UNKNOWN; u.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            u.Buffer.NumElements = m_numProbes; u.Buffer.StructureByteStride = sizeof(ProbeSH);
            dev->CreateUnorderedAccessView(m_probes.Get(), nullptr, &u, gfx.GetSRVCPUHandle(m_probeUAVSlot));
        }
        // Accumulator UAV (RWStructuredBuffer<int>).
        m_accumUAVSlot = gfx.AllocateSRVSlot();
        {
            D3D12_UNORDERED_ACCESS_VIEW_DESC u = {};
            u.Format = DXGI_FORMAT_UNKNOWN; u.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            u.Buffer.NumElements = m_numProbes * DDGI_ACCUM_INTS; u.Buffer.StructureByteStride = sizeof(int);
            dev->CreateUnorderedAccessView(m_accum.Get(), nullptr, &u, gfx.GetSRVCPUHandle(m_accumUAVSlot));
        }
        return true;
    }

    ID3D12Resource* ProbeResource() const { return m_probes.Get(); }
    uint32_t GetSRVSlot()      const { return m_probeSRVSlot; }
    uint32_t GetProbeUAVSlot() const { return m_probeUAVSlot; }
    uint32_t GetAccumUAVSlot() const { return m_accumUAVSlot; }
    uint32_t GetNumProbes()    const { return m_numProbes; }

    D3D12_RESOURCE_STATES ProbeState() const { return m_probeState; }
    void SetProbeState(D3D12_RESOURCE_STATES s) { m_probeState = s; }

private:
    static bool CreateDefaultBuffer(ID3D12Device* dev, UINT64 bytes, D3D12_RESOURCE_STATES initState,
                                    Microsoft::WRL::ComPtr<ID3D12Resource>& out) {
        D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rd = {};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width = bytes; rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
        rd.Format = DXGI_FORMAT_UNKNOWN; rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        return SUCCEEDED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                         initState, nullptr, IID_PPV_ARGS(&out)));
    }

    Microsoft::WRL::ComPtr<ID3D12Resource> m_probes;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_accum;
    uint32_t m_probeSRVSlot = 0;
    uint32_t m_probeUAVSlot = 0;
    uint32_t m_accumUAVSlot = 0;
    uint32_t m_numProbes    = 0;
    D3D12_RESOURCE_STATES m_probeState = D3D12_RESOURCE_STATE_COMMON;
};

} // namespace Fujin
