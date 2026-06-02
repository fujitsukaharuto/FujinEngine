#pragma once
#include "Engine/Graphics/GraphicsDevice.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>

namespace Fujin {

using Microsoft::WRL::ComPtr;

// GBuffer layout:
//   RT0: DXGI_FORMAT_R8G8B8A8_UNORM     - Albedo (rgb) + Metallic (a)
//   RT1: DXGI_FORMAT_R16G16B16A16_FLOAT - World Normal (rgb) + Roughness (a)
//   RT2: DXGI_FORMAT_R8G8B8A8_UNORM     - AO (r)
//   RT3: DXGI_FORMAT_R16G16_FLOAT       - Screen-space motion vector (UV delta) for TAA
class GBuffer {
public:
    static constexpr uint32_t RT_COUNT = 4;

    static constexpr DXGI_FORMAT RT_FORMATS[RT_COUNT] = {
        DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_FORMAT_R16G16_FLOAT,
    };

    bool Initialize(GraphicsDevice& gfx, uint32_t width, uint32_t height);
    void Shutdown();
    void Resize(GraphicsDevice& gfx, uint32_t width, uint32_t height);

    void TransitionToWrite(ID3D12GraphicsCommandList* cmd);
    void TransitionToRead(ID3D12GraphicsCommandList* cmd);
    void Clear(ID3D12GraphicsCommandList* cmd);

    D3D12_CPU_DESCRIPTOR_HANDLE GetRTV(uint32_t i)      const { return m_rtvHandles[i]; }
    uint32_t                    GetSRVSlot(uint32_t i)  const { return m_srvSlots[i]; }
    ID3D12Resource*             GetResource(uint32_t i) const { return m_rts[i].Get(); }

private:
    void CreateResources(GraphicsDevice& gfx, uint32_t width, uint32_t height);
    void ReleaseResources();

    ComPtr<ID3D12Resource>       m_rts[RT_COUNT];
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE  m_rtvHandles[RT_COUNT] = {};
    uint32_t                     m_srvSlots[RT_COUNT]   = {};
    bool                         m_srvSlotsAllocated     = false;
};

} // namespace Fujin
