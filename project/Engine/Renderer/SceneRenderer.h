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
#include "Engine/Asset/GeometryManager.h"
#include "Engine/Asset/TextureManager.h"
#include "Engine/Graphics/GraphicsDevice.h"
#include "Engine/Math/Math.h"
#include <chrono>

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
    ParticlePass     m_particlePass;
    PostProcessPass  m_postProcess;
    RenderGraph      m_rg;
    float            m_totalTime = 0.0f;

    Matrix4x4 m_lastViewProj;
    Matrix4x4 m_lastView;
    Matrix4x4 m_lastProj;

    std::chrono::steady_clock::time_point m_lastFrameTime;
    bool                                  m_firstFrame = true;

    void UpdateAnimations(const SceneManager& scene, float dt);
    void UpdateParticles (const SceneManager& scene, float dt);
};

} // namespace Fujin
