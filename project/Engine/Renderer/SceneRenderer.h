#pragma once
#include "GBuffer.h"
#include "GBufferPass.h"
#include "LightingPass.h"
#include "ShadowPass.h"
#include "IBLPreprocessor.h"
#include "RenderGraph/RenderGraph.h"
#include "Material/MaterialManager.h"
#include "Effect/ParticlePass.h"
#include "PostProcess/PostProcessPass.h"
#include "GI/DdgiVolume.h"
#include "TranslucencyPass.h"
#include "Engine/Asset/GeometryManager.h"
#include "Engine/Asset/TextureManager.h"
#include "Engine/Graphics/GraphicsDevice.h"
#include "Engine/Math/Math.h"
#include "Engine/Spatial/Bvh.h"
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>

namespace Fujin {

class SceneManager;
class AnimationComponent;
struct SkeletalMeshAsset;

class SceneRenderer {
public:
    Vector3 CameraPos    = Vector3(0.0f, 3.0f, -5.0f);
    Vector3 CameraTarget = Vector3(0.0f, 0.0f,  5.0f);

    bool Initialize(GraphicsDevice& gfx);
    void Render(ID3D12GraphicsCommandList* cmd,
                GraphicsDevice& gfx,
                const SceneManager& scene,
                uint32_t width, uint32_t height,
                uint32_t frameIndex,
                uint32_t vpX = 0, uint32_t vpY = 0,
                uint32_t vpW = 0, uint32_t vpH = 0);
    void Resize(GraphicsDevice& gfx, uint32_t width, uint32_t height);
    void Shutdown();
    const Matrix4x4&  GetLastViewProj()     const { return m_lastViewProj; }
    const Matrix4x4&  GetLastView()         const { return m_lastView; }
    const Matrix4x4&  GetLastProj()         const { return m_lastProj; }
    MaterialManager&  GetMaterialManager()        { return m_matMgr; }
    const RenderGraph& GetRenderGraph()   const { return m_rg; }
    PostProcessPass&  GetPostProcess()          { return m_postProcess; }

private:
    GeometryManager  m_geoMgr;
    TextureManager   m_texMgr;
    MaterialManager  m_matMgr;
    GBuffer          m_gbuffer;
    GBufferPass      m_gbufferPass;
    LightingPass     m_lightingPass;
    ShadowPass       m_shadowPass;
    IBLPreprocessor  m_ibl;
    ParticlePass       m_particlePass;
    TranslucencyPass   m_translucencyPass;
    PostProcessPass    m_postProcess;
    DdgiVolume         m_ddgi;          // DDGI probe volume (off-screen GI)
    RenderGraph      m_rg;
    float            m_totalTime = 0.0f;

    Matrix4x4 m_lastViewProj;
    Matrix4x4 m_lastView;
    Matrix4x4 m_lastProj;

    // TAA: sub-pixel jitter sequence index + previous frame's (jittered) view-proj for reprojection.
    uint32_t  m_frameCounter = 0;
    Matrix4x4 m_prevViewProjJittered;
    // Previous frame's jitter in viewport-uv; the delta (cur-prev) cancels jitter from reprojection.
    float     m_prevJitterUVx = 0.0f;
    float     m_prevJitterUVy = 0.0f;

    std::chrono::steady_clock::time_point m_lastFrameTime;
    bool                                  m_firstFrame = true;

    // Frustum culling: a persistent BVH of static opaque meshes (camera-only movement = no rebuild).
    Bvh                               m_renderBvh;
    std::unordered_map<uint64_t, int> m_renderProxies;   // actor id → proxy
    std::unordered_set<uint64_t>      m_visible;          // ids passing the camera frustum

    std::vector<ShadowCaster>         m_shadowCasters;    // gathered once per frame, shared by shadow passes
    std::vector<ParticleLight>        m_particleLights;   // per-particle Light Renderer lights, rebuilt in UpdateParticles

    void BuildVisibleSet(const SceneManager& scene, const Matrix4x4& viewProj);

    void UpdateAnimations(const SceneManager& scene, float dt);
    void UpdateParticles (const SceneManager& scene, float dt);
};

} // namespace Fujin
