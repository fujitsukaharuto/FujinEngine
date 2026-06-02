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
                     uint32_t frameIndex,
                     uint32_t vpX, uint32_t vpY, uint32_t vpW, uint32_t vpH);

    // TAA resolve. Call after ParticlePass, BEFORE ExecuteFinal. No-op unless TaaEnabled.
    // On entry: HDR RT in RENDER_TARGET, depth buffer in DEPTH_WRITE. On exit both are restored.
    // Reprojects history via depth + previous view-proj, then copies the result back into the HDR RT
    // so the rest of the chain (bloom/tonemap) is unchanged.
    void ExecuteTAA(ID3D12GraphicsCommandList* cmd,
                    GraphicsDevice& gfx,
                    uint32_t frameIndex,
                    ID3D12Resource* depthResource,
                    uint32_t depthSRVSlot,
                    ID3D12Resource* velocityResource,
                    uint32_t velocitySRVSlot,
                    const Matrix4x4& invViewProjCur,
                    const Matrix4x4& viewProjPrev,
                    float jitterDeltaU, float jitterDeltaV,
                    uint32_t vpX, uint32_t vpY, uint32_t vpW, uint32_t vpH);

    // Screen-space reflections. Call after ParticlePass, BEFORE ExecuteTAA/ExecuteFinal.
    // No-op unless SsrEnabled. On entry: HDR RT in RENDER_TARGET, depth in DEPTH_WRITE, GBuffer
    // normal RT in PIXEL_SHADER_RESOURCE; all are restored on exit. Composites reflections into
    // the HDR RT (additive), so the rest of the chain is unchanged.
    void ExecuteSSR(ID3D12GraphicsCommandList* cmd,
                    GraphicsDevice& gfx,
                    const GBuffer& gbuffer,
                    ID3D12Resource* depthResource,
                    uint32_t depthSRVSlot,
                    const Matrix4x4& viewProj,
                    const Matrix4x4& invViewProj,
                    const Vector3& cameraPos,
                    uint32_t frameIndex,
                    uint32_t vpX, uint32_t vpY, uint32_t vpW, uint32_t vpH);

    // Exponential height fog + directional inscattering. Call after ParticlePass, BEFORE
    // ExecuteSSR/ExecuteTAA/ExecuteFinal. No-op unless FogEnabled. On entry: HDR RT in RENDER_TARGET,
    // depth in DEPTH_WRITE; both restored on exit. Composites fog into the HDR RT (blend), so the
    // rest of the chain is unchanged. sunDir = direction the sun light travels (sun → scene).
    void ExecuteFog(ID3D12GraphicsCommandList* cmd,
                    GraphicsDevice& gfx,
                    ID3D12Resource* depthResource,
                    uint32_t depthSRVSlot,
                    const Matrix4x4& invViewProj,
                    const Vector3& cameraPos,
                    const Vector3& cameraForward,
                    const Vector3& sunDir,
                    const Vector3& sunColor,
                    uint32_t shadowSRVSlot,             // CSM array (god rays)
                    const Matrix4x4* lightViewProj,     // [4] cascade matrices
                    const float* cascadeSplits,         // [4] view-depth splits
                    uint32_t frameIndex,
                    uint32_t vpX, uint32_t vpY, uint32_t vpW, uint32_t vpH);

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
    bool  TaaEnabled     = false; // temporal AA (motion-vector reprojection); off by default
    float TaaHistoryBlend = 0.9f; // fraction of reprojected history kept per frame
    bool  SsrEnabled        = false; // screen-space reflections; off by default
    float SsrIntensity      = 0.6f;  // overall reflection strength
    float SsrRoughnessCutoff = 0.6f; // surfaces rougher than this get no SSR
    float SsrThickness      = 0.5f;  // world-space hit tolerance
    // ── Exponential height fog ──
    bool  FogEnabled        = false; // off by default (zero regression)
    float FogDensity        = 0.02f; // density at the fog height plane
    float FogHeightFalloff  = 0.2f;  // how fast density drops with height
    float FogHeight         = 0.0f;  // world Y of the density reference plane
    float FogMaxOpacity     = 0.9f;  // cap on fog blend
    float FogInscatterExp   = 8.0f;  // sun-glow tightness
    float FogColor[3]          = { 0.5f, 0.6f, 0.7f };
    float FogInscatterColor[3] = { 1.0f, 0.9f, 0.7f };
    // Volumetric god rays (directional CSM ray-march). Steps 0 = off (pure height fog).
    int   GodRaySteps     = 0;
    float GodRayIntensity = 0.25f;
    float GodRayMaxDist   = 40.0f;
    float GodRayG         = 0.6f;    // Henyey-Greenstein scattering anisotropy

private:
    static constexpr uint32_t SSAO_CB_SIZE    = 256;
    static constexpr uint32_t BLOOM_CB_SIZE   = 256 * 4;  // 4 passes × 256B aligned
    static constexpr uint32_t TONEMAP_CB_SIZE = 256;
    static constexpr uint32_t FOG_CB_SIZE     = 512;      // height fog + god-ray (LightViewProj[4] etc.)

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

    // ── TAA ────────────────────────────────────────────────────────────
    ComPtr<ID3D12Resource>       m_taaTex[2];           // R16G16B16A16 ping-pong (history / output)
    uint32_t                     m_taaSRVSlot[2] = { 0, 0 };
    uint32_t                     m_taaUAVSlot[2] = { 0, 0 };
    D3D12_RESOURCE_STATES        m_taaState[2]   = { D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON };
    uint32_t                     m_taaWriteIndex = 0;
    bool                         m_taaHistoryValid = false;
    ComPtr<ID3D12RootSignature>  m_taaRS;
    ComPtr<ID3D12PipelineState>  m_taaPSO;
    ComPtr<ID3D12Resource>       m_taaCB[NUM_FRAMES_IN_FLIGHT];
    uint8_t*                     m_taaCBMapped[NUM_FRAMES_IN_FLIGHT] = {};

    // ── SSR ────────────────────────────────────────────────────────────
    ComPtr<ID3D12Resource>       m_ssrTex;              // R16G16B16A16, composited scene+reflection
    uint32_t                     m_ssrSRVSlot = 0;
    uint32_t                     m_ssrUAVSlot = 0;
    D3D12_RESOURCE_STATES        m_ssrState   = D3D12_RESOURCE_STATE_COMMON;
    ComPtr<ID3D12RootSignature>  m_ssrRS;
    ComPtr<ID3D12PipelineState>  m_ssrPSO;
    ComPtr<ID3D12Resource>       m_ssrCB[NUM_FRAMES_IN_FLIGHT];
    uint8_t*                     m_ssrCBMapped[NUM_FRAMES_IN_FLIGHT] = {};

    // ── Height fog ─────────────────────────────────────────────────────
    ComPtr<ID3D12Resource>       m_fogTex;              // R16G16B16A16, composited scene+fog
    uint32_t                     m_fogSRVSlot = 0;
    uint32_t                     m_fogUAVSlot = 0;
    D3D12_RESOURCE_STATES        m_fogState   = D3D12_RESOURCE_STATE_COMMON;
    ComPtr<ID3D12RootSignature>  m_fogRS;
    ComPtr<ID3D12PipelineState>  m_fogPSO;
    ComPtr<ID3D12Resource>       m_fogCB[NUM_FRAMES_IN_FLIGHT];
    uint8_t*                     m_fogCBMapped[NUM_FRAMES_IN_FLIGHT] = {};

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
    bool CreateTAAResources(GraphicsDevice& gfx);
    bool CreateSSRResources(GraphicsDevice& gfx);
    bool CreateFogResources(GraphicsDevice& gfx);

    void ReleaseResolutionResources();

    static void Barrier(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res,
                        D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);
    static void UAVBarrier(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res);

    void DispatchCompute(ID3D12GraphicsCommandList* cmd, uint32_t w, uint32_t h) const;
};

} // namespace Fujin
