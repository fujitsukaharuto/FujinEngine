#pragma once
#include "Engine/Graphics/GraphicsDevice.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>
#include <functional>

namespace Fujin {

using Microsoft::WRL::ComPtr;

class IBLPreprocessor {
public:
    static constexpr uint32_t BRDF_LUT_SIZE       = 512;
    static constexpr uint32_t IRRADIANCE_SIZE     = 32;
    static constexpr uint32_t PREFILTER_BASE_SIZE = 128;
    static constexpr uint32_t PREFILTER_MIP_COUNT = 5;

    // Initializes and runs all IBL precomputation using a procedural default sky.
    bool Initialize(GraphicsDevice& gfx);
    void Shutdown();

    bool     IsReady()            const { return m_ready; }
    uint32_t GetBRDFLUTSlot()     const { return m_brdfSRVSlot; }
    uint32_t GetIrradianceSlot()  const { return m_irrSRVSlot; }
    uint32_t GetPrefilteredSlot() const { return m_prefSRVSlot; }

private:
    bool CreatePipelines(ID3D12Device* device);
    bool CreateOutputTextures(GraphicsDevice& gfx);
    bool UploadDefaultSky(GraphicsDevice& gfx);
    void RunCompute(GraphicsDevice& gfx);
    void ExecOneShot(GraphicsDevice& gfx,
                     std::function<void(ID3D12GraphicsCommandList*)> fn);

    ComPtr<ID3D12RootSignature> m_rs;
    ComPtr<ID3D12PipelineState> m_brdfPSO;
    ComPtr<ID3D12PipelineState> m_irrPSO;
    ComPtr<ID3D12PipelineState> m_prefPSO;

    // Source equirect HDR (procedural sky, RGBA32F, Texture2D)
    ComPtr<ID3D12Resource> m_skyTex;
    uint32_t               m_skySRVSlot = UINT32_MAX;

    // BRDF LUT (512x512 R16G16F, Texture2D)
    ComPtr<ID3D12Resource> m_brdfTex;
    uint32_t               m_brdfSRVSlot = UINT32_MAX;

    // Irradiance cubemap (32x32x6 RGBA16F, Texture2DArray → TextureCube SRV)
    ComPtr<ID3D12Resource> m_irrTex;
    uint32_t               m_irrSRVSlot = UINT32_MAX;

    // Prefiltered env cubemap (128x128x6 RGBA16F, PREFILTER_MIP_COUNT mips)
    ComPtr<ID3D12Resource> m_prefTex;
    uint32_t               m_prefUAVSlots[PREFILTER_MIP_COUNT] = {};
    uint32_t               m_prefSRVSlot                       = UINT32_MAX;

    bool m_ready = false;
};

} // namespace Fujin
