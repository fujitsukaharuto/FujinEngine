#pragma once
#include "Engine/Graphics/GraphicsDevice.h"
#include "Engine/Math/Math.h"
#include "Engine/Renderer/GBuffer.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>

namespace Fujin {

class PostProcessPass {
public:
    bool Initialize(GraphicsDevice& gfx, uint32_t width, uint32_t height);
    void Shutdown();
    void Resize(GraphicsDevice& gfx, uint32_t width, uint32_t height);

    // Call before LightingPass (one-frame lag SSAO using previous frame's GBuffer).
    // GBuffer RT1 must already be in NON_PIXEL_SHADER_RESOURCE state.
    // Depth SRV must already be in NON_PIXEL_SHADER_RESOURCE state.
    // Leaves SSAO blur result in PIXEL_SHADER_RESOURCE for LightingPass to read.
    void ExecuteSSAO(ID3D12GraphicsCommandList* cmd,
                     GraphicsDevice& gfx,
                     const GBuffer& gbuffer,
                     const Matrix4x4& viewProj,
                     const Matrix4x4& invViewProj,
                     uint32_t frameIndex);

    // Call after ParticlePass.
    // HDR RT must be in RENDER_TARGET state on entry.
    // Outputs to the swap-chain back buffer (gfx.GetCurrentRTV()).
    void ExecuteFinal(ID3D12GraphicsCommandList* cmd,
                      GraphicsDevice& gfx,
                      uint32_t frameIndex,
                      uint32_t vpX, uint32_t vpY, uint32_t vpW, uint32_t vpH);

    D3D12_CPU_DESCRIPTOR_HANDLE GetHDRRTV()      const { return m_hdrRTV; }
    ID3D12Resource*             GetHDRResource() const { return m_hdrRT.Get(); }
    uint32_t                    GetSSAOSRVSlot() const { return m_ssaoBlurSRVSlot; }

    bool  BloomEnabled   = true;
    float BloomStrength  = 0.12f;
    float BloomThreshold = 0.8f;
    float Exposure       = 0.45f; // ACES is brighter than Reinhard; tuned to match prior look
    bool  SSAOEnabled    = true;
    bool  FXAAEnabled    = true;

private:
    static constexpr uint32_t SSAO_CB_SIZE    = 256;
    static constexpr uint32_t BLOOM_CB_SIZE   = 256 * 4;  // 4 passes × 256B aligned
    static constexpr uint32_t TONEMAP_CB_SIZE = 256;

    // ── HDR render target ──────────────────────────────────────────────
    ComPtr<ID3D12Resource>       m_hdrRT;
    ComPtr<ID3D12DescriptorHeap> m_hdrRTVHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE  m_hdrRTV     = {};
    uint32_t                     m_hdrSRVSlot = 0;
    D3D12_RESOURCE_STATES        m_hdrState   = D3D12_RESOURCE_STATE_RENDER_TARGET;

    // ── SSAO ───────────────────────────────────────────────────────────
    ComPtr<ID3D12Resource>       m_ssaoRT;            // R8_UNORM, raw AO
    ComPtr<ID3D12Resource>       m_ssaoBlurRT;        // R8_UNORM, blurred AO
    uint32_t                     m_ssaoUAVSlot        = 0;
    uint32_t                     m_ssaoSRVSlot        = 0;
    uint32_t                     m_ssaoBlurUAVSlot    = 0;
    uint32_t                     m_ssaoBlurSRVSlot    = 0;
    D3D12_RESOURCE_STATES        m_ssaoState          = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_RESOURCE_STATES        m_ssaoBlurState      = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    ComPtr<ID3D12RootSignature>  m_ssaoRS;
    ComPtr<ID3D12PipelineState>  m_ssaoPSO;
    ComPtr<ID3D12RootSignature>  m_ssaoBlurRS;
    ComPtr<ID3D12PipelineState>  m_ssaoBlurPSO;
    ComPtr<ID3D12Resource>       m_ssaoCB[NUM_FRAMES_IN_FLIGHT];
    uint8_t*                     m_ssaoCBMapped[NUM_FRAMES_IN_FLIGHT] = {};

    // ── Bloom ──────────────────────────────────────────────────────────
    ComPtr<ID3D12Resource>       m_bloomA;             // R16G16B16A16, half-res ping
    ComPtr<ID3D12Resource>       m_bloomB;             // R16G16B16A16, half-res pong
    uint32_t                     m_bloomASRVSlot = 0, m_bloomAUAVSlot = 0;
    uint32_t                     m_bloomBSRVSlot = 0, m_bloomBUAVSlot = 0;
    D3D12_RESOURCE_STATES        m_bloomAState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_RESOURCE_STATES        m_bloomBState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    ComPtr<ID3D12RootSignature>  m_bloomRS;
    ComPtr<ID3D12PipelineState>  m_bloomDownPSO;
    ComPtr<ID3D12PipelineState>  m_bloomUpPSO;
    ComPtr<ID3D12Resource>       m_bloomCB[NUM_FRAMES_IN_FLIGHT];   // 4×256B sub-allocs
    uint8_t*                     m_bloomCBMapped[NUM_FRAMES_IN_FLIGHT] = {};

    // ── Tonemap + FXAA ─────────────────────────────────────────────────
    ComPtr<ID3D12RootSignature>  m_tonemapRS;
    ComPtr<ID3D12PipelineState>  m_tonemapPSO;
    ComPtr<ID3D12Resource>       m_tonemapCB[NUM_FRAMES_IN_FLIGHT];
    uint8_t*                     m_tonemapCBMapped[NUM_FRAMES_IN_FLIGHT] = {};

    uint32_t m_width  = 0;
    uint32_t m_height = 0;

    bool CreateHDRRT(GraphicsDevice& gfx);
    bool CreateSSAOResources(GraphicsDevice& gfx);
    bool CreateBloomResources(GraphicsDevice& gfx);
    bool CreateTonemapResources(GraphicsDevice& gfx);

    void ReleaseResolutionResources();

    static void Barrier(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res,
                        D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);
    static void UAVBarrier(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res);

    void DispatchCompute(ID3D12GraphicsCommandList* cmd, uint32_t w, uint32_t h) const;
};

} // namespace Fujin
