#pragma once
#include "Engine/Graphics/GraphicsDevice.h"
#include "Engine/Math/Math.h"
#include "Engine/Renderer/GBuffer.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>

namespace Fujin {

class DdgiVolume;

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
                    uint32_t vpX, uint32_t vpY, uint32_t vpW, uint32_t vpH,
                    uint32_t probeSRVSlot = 0,
                    const float gridOrigin[3] = nullptr,
                    const float gridSpacing[3] = nullptr,
                    const uint32_t gridDims[3] = nullptr);

    // Screen-space global illumination (one-bounce indirect diffuse). Call after ParticlePass,
    // BEFORE ExecuteSSR/ExecuteTAA/ExecuteFinal. No-op unless SsgiEnabled. Same entry/exit states and
    // restore contract as ExecuteSSR (HDR→RENDER_TARGET, depth→DEPTH_WRITE, GBuffer normal+albedo→PSR);
    // additively composites bounced light into the HDR RT, so the rest of the chain is unchanged.
    void ExecuteSSGI(ID3D12GraphicsCommandList* cmd,
                     GraphicsDevice& gfx,
                     const GBuffer& gbuffer,
                     ID3D12Resource* depthResource,
                     uint32_t depthSRVSlot,
                     ID3D12Resource* velocityResource,
                     uint32_t velocitySRVSlot,
                     const Matrix4x4& viewProj,
                     const Matrix4x4& invViewProj,
                     const Matrix4x4& viewProjPrev,
                     const Vector3& cameraPos,
                     float jitterDeltaU, float jitterDeltaV,
                     uint32_t frameIndex,
                     uint32_t vpX, uint32_t vpY, uint32_t vpW, uint32_t vpH);

    // DDGI capture — inject the lit scene's radiance into the probe volume (screen-space scatter) and
    // resolve it into the probe SH buffer. Call once, BEFORE ExecuteDdgi (apply) and BEFORE SSGI/SSR
    // add GI to the HDR. Injection now re-injects a fraction of the probes' own irradiance (DdgiFeedback)
    // for multi-bounce GI; reads last frame's probe SH so it converges. No-op unless DdgiEnabled.
    // On entry HDR=RENDER_TARGET, depth=DEPTH_WRITE; both restored on exit. Leaves the probe buffer in
    // NON_PIXEL_SHADER_RESOURCE (ready for the apply pass to read).
    void ExecuteDdgiCapture(ID3D12GraphicsCommandList* cmd,
                            GraphicsDevice& gfx,
                            const GBuffer& gbuffer,
                            ID3D12Resource* depthResource,
                            uint32_t depthSRVSlot,
                            const Matrix4x4& invViewProj,
                            DdgiVolume& vol,
                            const float gridOrigin[3],
                            const float gridSpacing[3],
                            const uint32_t gridDims[3],
                            uint32_t frameIndex,
                            uint32_t vpX, uint32_t vpY, uint32_t vpW, uint32_t vpH);

    // DDGI apply — adds off-screen indirect diffuse from a probe volume into the HDR RT. Call after
    // ParticlePass, BEFORE SSR/TAA/Final (alongside SSGI). No-op unless DdgiEnabled. Same entry/exit
    // state contract as SSGI's first pass (HDR→RENDER_TARGET, depth→DEPTH_WRITE, GBuffer normal+albedo
    // →PSR all restored). Additive composite, so the rest of the chain is unchanged.
    void ExecuteDdgi(ID3D12GraphicsCommandList* cmd,
                     GraphicsDevice& gfx,
                     const GBuffer& gbuffer,
                     ID3D12Resource* depthResource,
                     uint32_t depthSRVSlot,
                     const Matrix4x4& invViewProj,
                     uint32_t probeSRVSlot,
                     const float gridOrigin[3],
                     const float gridSpacing[3],
                     const uint32_t gridDims[3],
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
    // ── Auto exposure (eye adaptation) ── overrides Exposure when enabled
    bool  AutoExposureEnabled = false; // off by default (uses manual Exposure → zero regression)
    float AutoExposureKey     = 0.18f; // target middle-grey luminance
    float AutoExposureMin     = 0.05f; // clamp on the computed exposure multiplier
    float AutoExposureMax     = 8.0f;
    float AutoExposureSpeed   = 0.05f; // per-frame adaptation rate (0..1; higher = snappier)
    float ExposureCompensation = 1.0f; // manual EV bias on top of auto exposure
    int   TonemapMode    = 0;     // 0 = ACES (Narkowicz), 1 = AgX (modern UE5-ish look)
    float VignetteIntensity   = 0.0f;  // 0 = off (zero regression)
    float VignetteSoftness    = 0.5f;  // radial start: darkening ramps from here to corners
    float ChromaticAberration = 0.0f;  // 0 = off; lateral RGB split strength
    bool  SSAOEnabled    = true;
    // ── Contact shadows (screen-space, directional light) ── computed in LightingPass, off by default
    bool  ContactShadowsEnabled  = false;
    float ContactShadowLength     = 0.12f; // world-space ray length (subtle supplement, not primary)
    float ContactShadowStrength   = 0.50f; // darkening amount when occluded
    int   ContactShadowSteps      = 8;     // march samples
    float ContactShadowThickness  = 0.08f; // occluder depth window (world units)
    bool  FXAAEnabled    = true;
    bool  TaaEnabled     = false; // temporal AA (motion-vector reprojection); off by default
    float TaaHistoryBlend = 0.9f; // fraction of reprojected history kept per frame
    bool  SsrEnabled        = false; // screen-space reflections; off by default
    float SsrIntensity      = 0.6f;  // overall reflection strength
    float SsrRoughnessCutoff = 0.6f; // surfaces rougher than this get no SSR
    float SsrThickness      = 0.5f;  // world-space hit tolerance
    float SsrDdgiFallback   = 1.0f;  // off-screen reflection fallback via DDGI probes (needs DDGI on)
    // ── Screen-space GI (one-bounce indirect diffuse) ──
    bool  SsgiEnabled    = false; // off by default (zero regression / zero cost)
    float SsgiIntensity  = 1.0f;  // overall bounce strength (0 => disabled look)
    float SsgiRadius     = 4.0f;  // world-space max gather distance per ray
    float SsgiThickness  = 0.5f;  // world-space hit tolerance behind a surface
    int   SsgiRayCount   = 4;     // hemisphere rays / pixel (cost scales linearly)
    int   SsgiStepCount  = 10;    // march steps / ray (cost scales linearly)
    bool  SsgiDenoise      = true;  // temporal accumulation + spatial bilateral (off => raw noisy GI)
    float SsgiHistoryBlend = 0.92f; // temporal: fraction of reprojected history kept per frame
    int   SsgiBlurRadius   = 2;     // spatial bilateral half-extent in pixels (0 = temporal only)
    float SsgiDepthSigma   = 0.002f;// bilateral depth tolerance (relative to depth)
    // ── DDGI (off-screen probe-volume GI) ──
    bool  DdgiEnabled   = false; // off by default (zero regression / zero cost)
    float DdgiIntensity = 0.25f; // probe-volume GI strength (HDR bounce is strong; keep gentle)
    float DdgiFeedback  = 0.7f;  // multi-bounce: fraction of probe irradiance re-injected (0 = single bounce)
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

    // ── SSGI (trace → temporal accumulate → composite) ─────────────────
    ComPtr<ID3D12Resource>       m_ssgiTex;             // R16G16B16A16: raw GI (trace), then scene+GI scratch (composite)
    uint32_t                     m_ssgiSRVSlot = 0;
    uint32_t                     m_ssgiUAVSlot = 0;
    D3D12_RESOURCE_STATES        m_ssgiState   = D3D12_RESOURCE_STATE_COMMON;
    ComPtr<ID3D12RootSignature>  m_ssgiRS;
    ComPtr<ID3D12PipelineState>  m_ssgiPSO;
    ComPtr<ID3D12Resource>       m_ssgiCB[NUM_FRAMES_IN_FLIGHT];
    uint8_t*                     m_ssgiCBMapped[NUM_FRAMES_IN_FLIGHT] = {};
    // accumulated GI history (ping-pong across frames)
    ComPtr<ID3D12Resource>       m_ssgiHist[2];
    uint32_t                     m_ssgiHistSRVSlot[2] = { 0, 0 };
    uint32_t                     m_ssgiHistUAVSlot[2] = { 0, 0 };
    D3D12_RESOURCE_STATES        m_ssgiHistState[2]   = { D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON };
    uint32_t                     m_ssgiHistWrite      = 0;
    bool                         m_ssgiHistValid      = false;
    // temporal-accumulation pass
    ComPtr<ID3D12RootSignature>  m_ssgiTemporalRS;
    ComPtr<ID3D12PipelineState>  m_ssgiTemporalPSO;
    ComPtr<ID3D12Resource>       m_ssgiTemporalCB[NUM_FRAMES_IN_FLIGHT];
    uint8_t*                     m_ssgiTemporalCBMapped[NUM_FRAMES_IN_FLIGHT] = {};
    // composite (bilateral denoise + add to scene) pass
    ComPtr<ID3D12RootSignature>  m_ssgiCompositeRS;
    ComPtr<ID3D12PipelineState>  m_ssgiCompositePSO;
    ComPtr<ID3D12Resource>       m_ssgiCompositeCB[NUM_FRAMES_IN_FLIGHT];
    uint8_t*                     m_ssgiCompositeCBMapped[NUM_FRAMES_IN_FLIGHT] = {};

    // ── DDGI apply ─────────────────────────────────────────────────────
    ComPtr<ID3D12Resource>       m_ddgiTex;             // R16G16B16A16, composited scene+GI scratch
    uint32_t                     m_ddgiSRVSlot = 0;
    uint32_t                     m_ddgiUAVSlot = 0;
    D3D12_RESOURCE_STATES        m_ddgiState   = D3D12_RESOURCE_STATE_COMMON;
    ComPtr<ID3D12RootSignature>  m_ddgiRS;
    ComPtr<ID3D12PipelineState>  m_ddgiPSO;
    ComPtr<ID3D12Resource>       m_ddgiCB[NUM_FRAMES_IN_FLIGHT];
    uint8_t*                     m_ddgiCBMapped[NUM_FRAMES_IN_FLIGHT] = {};
    // DDGI capture (screen-space inject + resolve into the probe SH buffer)
    ComPtr<ID3D12RootSignature>  m_ddgiInjectRS;
    ComPtr<ID3D12PipelineState>  m_ddgiInjectPSO;
    ComPtr<ID3D12Resource>       m_ddgiInjectCB[NUM_FRAMES_IN_FLIGHT];
    uint8_t*                     m_ddgiInjectCBMapped[NUM_FRAMES_IN_FLIGHT] = {};
    ComPtr<ID3D12RootSignature>  m_ddgiResolveRS;
    ComPtr<ID3D12PipelineState>  m_ddgiResolvePSO;
    ComPtr<ID3D12Resource>       m_ddgiResolveCB[NUM_FRAMES_IN_FLIGHT];
    uint8_t*                     m_ddgiResolveCBMapped[NUM_FRAMES_IN_FLIGHT] = {};
    float DdgiBlendRate = 0.1f;  // temporal blend of the resolved probe SH per frame

    // ── Height fog ─────────────────────────────────────────────────────
    ComPtr<ID3D12Resource>       m_fogTex;              // R16G16B16A16, composited scene+fog
    uint32_t                     m_fogSRVSlot = 0;
    uint32_t                     m_fogUAVSlot = 0;
    D3D12_RESOURCE_STATES        m_fogState   = D3D12_RESOURCE_STATE_COMMON;
    ComPtr<ID3D12RootSignature>  m_fogRS;
    ComPtr<ID3D12PipelineState>  m_fogPSO;
    ComPtr<ID3D12Resource>       m_fogCB[NUM_FRAMES_IN_FLIGHT];
    uint8_t*                     m_fogCBMapped[NUM_FRAMES_IN_FLIGHT] = {};

    // ── Auto exposure (GPU meter → CPU readback → adapt) ───────────────
    ComPtr<ID3D12Resource>       m_aeLumBuf;          // DEFAULT, 1× R32_FLOAT, UAV: geometric-mean luminance
    uint32_t                     m_aeLumUAVSlot = 0;
    D3D12_RESOURCE_STATES        m_aeLumState   = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    ComPtr<ID3D12Resource>       m_aeReadback[NUM_FRAMES_IN_FLIGHT]; // READBACK copies (per-frame)
    ComPtr<ID3D12RootSignature>  m_aeRS;
    ComPtr<ID3D12PipelineState>  m_aePSO;
    float                        m_adaptedLum  = 0.18f; // CPU adaptation state
    uint32_t                     m_aeFrameCount = 0;    // frames since AE started (readback validity)

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
    bool CreateAutoExposureResources(GraphicsDevice& gfx);
    bool CreateTAAResources(GraphicsDevice& gfx);
    bool CreateSSRResources(GraphicsDevice& gfx);
    bool CreateSSGIResources(GraphicsDevice& gfx);
    bool CreateDdgiResources(GraphicsDevice& gfx);
    bool CreateFogResources(GraphicsDevice& gfx);

    void ReleaseResolutionResources();

    static void Barrier(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res,
                        D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);
    static void UAVBarrier(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res);

    void DispatchCompute(ID3D12GraphicsCommandList* cmd, uint32_t w, uint32_t h) const;
};

} // namespace Fujin
