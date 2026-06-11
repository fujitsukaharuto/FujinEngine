#include "SceneRenderer.h"
#include "RenderGraph/RGTypes.h"
#include "Engine/Core/SceneManager.h"
#include "Engine/Core/Actor.h"
#include "Engine/Core/LightComponent.h"
#include "Engine/Core/TransformComponent.h"
#include "Engine/Core/MeshComponent.h"
#include "Engine/Core/AnimationComponent.h"
#include "Engine/Core/ParticleComponent.h"
#include "Engine/Core/FootIKComponent.h"
#include "Engine/Physics/PhysicsWorld.h"
#include "Engine/Asset/SkeletalMeshAsset.h"
#include <cmath>
#include <algorithm>

namespace Fujin {

static constexpr float CAMERA_NEAR = 0.1f;
static constexpr float CAMERA_FAR  = 1000.0f;

// Halton low-discrepancy sequence (for TAA sub-pixel jitter).
static float Halton(uint32_t i, uint32_t base) {
    float f = 1.0f, r = 0.0f;
    while (i > 0) { f /= (float)base; r += f * (float)(i % base); i /= base; }
    return r;
}

// World-space AABB of local bounds transformed by a matrix (transforms the 8 corners).
static Aabb TransformBounds(const float mn[3], const float mx[3], const Matrix4x4& m) {
    Aabb out;
    for (int i = 0; i < 8; ++i) {
        Vector4 c((i & 1) ? mx[0] : mn[0],
                  (i & 2) ? mx[1] : mn[1],
                  (i & 4) ? mx[2] : mn[2], 1.0f);
        Vector4 w = m * c;
        if (w.x < out.lo.x) out.lo.x = w.x;  if (w.x > out.hi.x) out.hi.x = w.x;
        if (w.y < out.lo.y) out.lo.y = w.y;  if (w.y > out.hi.y) out.hi.y = w.y;
        if (w.z < out.lo.z) out.lo.z = w.z;  if (w.z > out.hi.z) out.hi.z = w.z;
    }
    return out;
}

// Refresh the render BVH from static opaque meshes and collect the ids inside the camera frustum.
// Skeletal (animated) meshes are not culled here — their bounds vary with the pose — so they are
// always considered visible by the GBuffer pass.
void SceneRenderer::BuildVisibleSet(const SceneManager& scene, const Matrix4x4& viewProj) {
    std::unordered_set<uint64_t> seen;
    for (auto& ap : scene.GetActors()) {
        auto* mc = ap->GetComponent<MeshComponent>();
        auto* tc = ap->GetComponent<TransformComponent>();
        if (!mc || !tc) continue;
        if (m_geoMgr.IsSkeletal(mc->MeshPath)) continue;          // animated → never culled
        const MeshAsset* mesh = m_geoMgr.LoadMesh(mc->MeshPath);
        if (!mesh || mesh->SubMeshes.empty()) continue;

        uint64_t id = ap->GetId();
        seen.insert(id);
        Aabb box = TransformBounds(mesh->BoundsMin, mesh->BoundsMax, tc->GetWorldMatrix());

        auto it = m_renderProxies.find(id);
        if (it == m_renderProxies.end()) m_renderProxies[id] = m_renderBvh.CreateProxy(box, ap.get());
        else                             m_renderBvh.MoveProxy(it->second, box);
    }
    for (auto it = m_renderProxies.begin(); it != m_renderProxies.end(); ) {
        if (seen.count(it->first)) ++it;
        else { m_renderBvh.DestroyProxy(it->second); it = m_renderProxies.erase(it); }
    }

    Plane planes[6];
    ExtractFrustumPlanes(viewProj, planes);
    m_visible.clear();
    m_renderBvh.QueryFrustum(planes, [&](void* ud) {
        m_visible.insert(static_cast<Actor*>(ud)->GetId());
    });
}

bool SceneRenderer::Initialize(GraphicsDevice& gfx) {
    if (!m_geoMgr.Initialize(gfx.GetDevice(), gfx.GetCommandQueue())) return false;
    if (!m_texMgr.Initialize(gfx))                                    return false;
    m_matMgr.InitLayout(); // reflect GBufferPass.PS.hlsl before PSO creation

    uint32_t w = gfx.GetWidth();
    uint32_t h = gfx.GetHeight();

    if (!m_gbuffer.Initialize(gfx, w, h))         return false;
    if (!m_gbufferPass.Initialize(gfx, m_gbuffer)) return false;
    if (!m_lightingPass.Initialize(gfx))           return false;
    if (!m_shadowPass.Initialize(gfx))             return false;
    if (!m_ibl.Initialize(gfx))                    return false;
    if (!m_particlePass.Initialize(gfx))             return false;
    if (!m_translucencyPass.Initialize(gfx))         return false;
    if (!m_postProcess.Initialize(gfx, w, h))        return false;
    if (!m_ddgi.Initialize(gfx))                     return false;

    return true;
}

void SceneRenderer::Resize(GraphicsDevice& gfx, uint32_t width, uint32_t height) {
    // Evict old GBuffer pointers from the cross-frame tracker before they are freed.
    // Without this, the OS can reuse the same virtual address for the new resources,
    // making the tracker believe they are still in ALL_SHADER_RESOURCE (0xC0) when they
    // are actually freshly created at PIXEL_SHADER_RESOURCE (0x80) → barrier mismatch.
    for (uint32_t i = 0; i < GBuffer::RT_COUNT; ++i)
        m_rg.ForgetResource(m_gbuffer.GetResource(i));
    m_gbuffer.Resize(gfx, width, height);
    m_postProcess.Resize(gfx, width, height);
}

void SceneRenderer::Render(ID3D12GraphicsCommandList* cmd,
                            GraphicsDevice& gfx,
                            const SceneManager& scene,
                            uint32_t width, uint32_t height,
                            uint32_t frameIndex,
                            uint32_t vpX, uint32_t vpY,
                            uint32_t vpW, uint32_t vpH) {
    auto& prof = gfx.GetProfiler();

    // ---- Delta time for animation ----
    auto now = std::chrono::steady_clock::now();
    float dt = 0.0f;
    if (!m_firstFrame)
        dt = (std::min)(std::chrono::duration<float>(now - m_lastFrameTime).count(), 0.1f);
    m_lastFrameTime = now;
    m_firstFrame    = false;

    m_totalTime += dt;
    UpdateAnimations(scene, dt);
    UpdateParticles(scene, dt);

    m_gbufferPass.CameraPos    = CameraPos;
    m_gbufferPass.CameraTarget = CameraTarget;

    // --- CPU: find directional light ---
    Vector3 lightDir(0.3f, -1.0f, 0.5f);
    lightDir = lightDir.GetSafeNormal();
    Vector3 sunColor(1.0f, 1.0f, 1.0f);   // for fog inscattering
    for (auto& actorPtr : scene.GetActors()) {
        auto* lc = actorPtr->GetComponent<LightComponent>();
        if (!lc || lc->Type != LightType::Directional) continue;
        auto* tc = actorPtr->GetComponent<TransformComponent>();
        if (tc) {
            // Use the world rotation (CachedWorld), not the local one, so a parented
            // directional light casts shadows in the correct world direction.
            Matrix4x4 rotMat = tc->CachedWorld.Rotation.ToMatrix();
            lightDir = Vector3(rotMat.m[2][0], rotMat.m[2][1], rotMat.m[2][2]).GetSafeNormal();
        }
        sunColor = lc->Color * lc->Intensity;
        break;
    }

    // Use viewport rect if valid; fall back to full window.
    uint32_t evpX = (vpW > 0 && vpH > 0) ? vpX : 0;
    uint32_t evpY = (vpW > 0 && vpH > 0) ? vpY : 0;
    uint32_t evpW = (vpW > 0 && vpH > 0) ? vpW : width;
    uint32_t evpH = (vpW > 0 && vpH > 0) ? vpH : height;

    // Clamp to the render-target bounds. The editor viewport rect (from the ImGui dock layout) lags
    // resize by a frame: when the window SHRINKS, the stale larger rect would push the scissor/viewport
    // past the freshly-resized (smaller) RTs → D3D12 device-removed → crash. Clamping makes the stale
    // frame harmless. (evpX/evpY first so the width/height clamp has the correct origin.)
    evpX = (evpX < width)  ? evpX : (width  > 0 ? width  - 1 : 0);
    evpY = (evpY < height) ? evpY : (height > 0 ? height - 1 : 0);
    evpW = (std::min)(evpW, width  - evpX);
    evpH = (std::min)(evpH, height - evpY);

    // --- CPU: compute camera matrices ---
    float aspect      = (evpH > 0) ? static_cast<float>(evpW) / static_cast<float>(evpH) : 1.0f;
    Matrix4x4 view    = Matrix4x4::LookAt(CameraPos, CameraTarget, Vector3(0, 1, 0));
    Matrix4x4 proj    = Matrix4x4::Perspective(Math::ToRadians(60.0f), aspect, CAMERA_NEAR, CAMERA_FAR);
    // TAA sub-pixel jitter: nudge the projection by <1px so successive frames sample different
    // sub-pixel positions; the TAA resolve averages them. No jitter when TAA is disabled.
    float jitterUVx = 0.0f, jitterUVy = 0.0f;   // this frame's jitter in viewport-uv
    if (m_postProcess.TaaEnabled && evpW > 0 && evpH > 0) {
        // ±0.5px jitter measured in the rendered viewport (NDC spans the viewport, not the full RT).
        float jx = (Halton(m_frameCounter + 1, 2) - 0.5f) * 2.0f / (float)evpW;
        float jy = (Halton(m_frameCounter + 1, 3) - 0.5f) * 2.0f / (float)evpH;
        proj.m[0][2] += jx;
        proj.m[1][2] += jy;
        // uv = ndc*(0.5,-0.5)+0.5, so the jitter contributes (jx*0.5, -jy*0.5) to the projected uv.
        jitterUVx =  jx * 0.5f;
        jitterUVy = -jy * 0.5f;
    }
    Matrix4x4 viewProj     = proj * view;
    Matrix4x4 invViewProj  = viewProj.GetInverse();
    m_lastViewProj = viewProj;
    m_lastView     = view;
    m_lastProj     = proj;
    Vector3   cameraForward = (CameraTarget - CameraPos).GetSafeNormal();

    // Frustum culling: refresh the render BVH and the visible set for this camera.
    BuildVisibleSet(scene, viewProj);

    // --- CPU: compute shadow cascades ---
    ShadowData shadowData;
    m_shadowPass.ComputeCascades(invViewProj, CAMERA_NEAR, CAMERA_FAR, lightDir, shadowData);

    // --- CPU: shadow-casting spot/point lights (opt-in; nearest within budget) + static cache ---
    // Build the shared caster list first (Prepare* needs it for the cache dirty-hash), but only if
    // any shadow-casting non-directional light exists — otherwise the gather is pure waste.
    bool anyShadowLight = false;
    for (auto& ap : scene.GetActors()) {
        auto* lc = ap->GetComponent<LightComponent>();
        if (lc && lc->CastShadows && lc->Type != LightType::Directional) { anyShadowLight = true; break; }
    }
    if (anyShadowLight) ShadowPass::BuildCasters(scene, m_geoMgr, m_texMgr, m_matMgr, m_shadowCasters);
    else                m_shadowCasters.clear();

    m_lightingPass.PrepareSpotShadows(scene, CameraPos, m_shadowCasters);
    const SpotShadowData& spotData = m_lightingPass.GetSpotData();
    const bool hasSpotShadows = spotData.Count > 0;
    m_lightingPass.PreparePointShadows(scene, CameraPos, m_shadowCasters);
    const PointShadowData& pointData = m_lightingPass.GetPointData();
    const bool hasPointShadows = pointData.Count > 0;

    // Cache: only re-render the shadow pass when at least one slot is dirty (light/caster changed).
    bool anySpotDirty = false;
    for (uint32_t s = 0; s < spotData.Count; ++s) anySpotDirty |= spotData.NeedsRender[s];
    bool anyPointDirty = false;
    for (uint32_t s = 0; s < pointData.Count; ++s) anyPointDirty |= pointData.NeedsRender[s];

    // --- SSAO (one-frame lag): runs before RG using previous frame's GBuffer/depth ---
    prof.BeginScope(cmd, "SSAO");
    // Transition GBuffer RT1 and depth to compute-shader SRV state
    {
        D3D12_RESOURCE_BARRIER barriers[2] = {};
        barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[0].Transition.pResource   = m_gbuffer.GetResource(1);
        barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[1].Transition.pResource   = gfx.GetDepthBuffer();
        barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        barriers[1].Transition.StateAfter  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(2, barriers);
    }
    m_postProcess.ExecuteSSAO(cmd, gfx, m_gbuffer, viewProj, invViewProj, frameIndex,
                              evpX, evpY, evpW, evpH);
    // Restore for render graph
    {
        D3D12_RESOURCE_BARRIER barriers[2] = {};
        barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[0].Transition.pResource   = m_gbuffer.GetResource(1);
        barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        barriers[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[1].Transition.pResource   = gfx.GetDepthBuffer();
        barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        barriers[1].Transition.StateAfter  = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(2, barriers);
    }
    prof.EndScope(cmd);

    // --- Build render graph ---
    m_rg.BeginFrame();

    // GBuffer RTs start in PSR (created in PSR; RG tracks cross-frame state).
    RGHandle hGB0 = m_rg.Import("GBuffer0", m_gbuffer.GetResource(0),
                                 D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    RGHandle hGB1 = m_rg.Import("GBuffer1", m_gbuffer.GetResource(1),
                                 D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    RGHandle hGB2 = m_rg.Import("GBuffer2", m_gbuffer.GetResource(2),
                                 D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    RGHandle hGB3 = m_rg.Import("GBuffer3", m_gbuffer.GetResource(3),
                                 D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);   // motion vectors
    RGHandle hDepth  = m_rg.Import("Depth",     gfx.GetDepthBuffer(),
                                    D3D12_RESOURCE_STATE_DEPTH_WRITE);
    RGHandle hShadow = m_rg.Import("ShadowMap", m_shadowPass.GetShadowMap(),
                                    D3D12_RESOURCE_STATE_DEPTH_WRITE);
    const bool runSpotPass  = hasSpotShadows  && anySpotDirty;
    const bool runPointPass = hasPointShadows && anyPointDirty;
    RGHandle hSpotShadow{};
    if (runSpotPass)
        hSpotShadow = m_rg.Import("SpotShadowMap", m_shadowPass.GetSpotMap(),
                                  D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);  // created in PSR
    RGHandle hPointShadow{};
    if (runPointPass)
        hPointShadow = m_rg.Import("PointShadowMap", m_shadowPass.GetPointMap(),
                                   D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);  // created in PSR

    // GBuffer pass: writes GB0/1/2/3 as RTV, depth as DSV.
    m_rg.AddPass("GBufferPass",
        {},
        { AsDSV(hDepth), AsRTV(hGB0), AsRTV(hGB1), AsRTV(hGB2), AsRTV(hGB3) },
        [&, viewProj, evpX, evpY, evpW, evpH, frameIndex](ID3D12GraphicsCommandList* c) {
            m_gbuffer.Clear(c);
            m_gbufferPass.Execute(c, gfx, m_gbuffer, scene,
                                  evpX, evpY, evpW, evpH, frameIndex,
                                  m_geoMgr, m_texMgr, m_matMgr, viewProj, m_prevViewProjJittered, &m_visible);
        });

    // Shadow pass: writes shadow map as DSV (no reads from tracked resources).
    m_rg.AddPass("ShadowPass",
        {},
        { AsDSV(hShadow) },
        [&, shadowData, frameIndex](ID3D12GraphicsCommandList* c) {
            m_shadowPass.ExecuteGPU(c, scene, frameIndex, m_geoMgr, m_texMgr, m_matMgr, gfx, shadowData);
        });

    // Spot shadow pass: render only dirty slots; skipped entirely when all slots are cached.
    if (runSpotPass) {
        m_rg.AddPass("SpotShadowPass",
            {},
            { AsDSV(hSpotShadow) },
            [&, spotData, frameIndex](ID3D12GraphicsCommandList* c) {
                m_shadowPass.ExecuteSpotGPU(c, frameIndex, m_shadowCasters, gfx, spotData);
            });
    }

    // Point shadow pass: render only dirty cubes; skipped entirely when all are cached.
    if (runPointPass) {
        m_rg.AddPass("PointShadowPass",
            {},
            { AsDSV(hPointShadow) },
            [&, pointData, frameIndex](ID3D12GraphicsCommandList* c) {
                m_shadowPass.ExecutePointGPU(c, frameIndex, m_shadowCasters, gfx, pointData);
            });
    }

    // Both shadow maps are created in PSR and left in PSR by the RG, so binding their SRVs is always
    // safe — even on frames where the pass is skipped (Count==0 or all slots cached).
    uint32_t spotSRVSlot  = m_shadowPass.GetSpotSRVSlot();
    uint32_t pointSRVSlot = m_shadowPass.GetPointSRVSlot();

    // Lighting pass: reads GBuffer + depth + shadow maps, writes to HDR render target.
    std::vector<RGHandle> lightReads = { hGB0, hGB1, hGB2, hDepth, hShadow };
    if (runSpotPass)  lightReads.push_back(hSpotShadow);
    if (runPointPass) lightReads.push_back(hPointShadow);

    D3D12_CPU_DESCRIPTOR_HANDLE hdrRTV = m_postProcess.GetHDRRTV();
    m_rg.AddPass("LightingPass",
        lightReads,
        {},
        [&, invViewProj, cameraForward, shadowData, frameIndex, evpX, evpY, evpW, evpH, hdrRTV, spotSRVSlot, pointSRVSlot](ID3D12GraphicsCommandList* c) {
            // Clear HDR RT to sky color
            float clearColor[4] = { 0.08f, 0.08f, 0.1f, 1.0f };
            c->ClearRenderTargetView(hdrRTV, clearColor, 0, nullptr);
            ContactShadowSettings contact;
            if (m_postProcess.ContactShadowsEnabled) {
                contact.Length    = m_postProcess.ContactShadowLength;
                contact.Strength  = m_postProcess.ContactShadowStrength;
                contact.Steps     = (uint32_t)(std::max)(1, m_postProcess.ContactShadowSteps);
                contact.Thickness = m_postProcess.ContactShadowThickness;
            } else {
                contact.Strength = 0.0f; // disabled
            }
            m_lightingPass.Execute(c, gfx, m_gbuffer, scene, frameIndex,
                                   invViewProj, CameraPos, cameraForward,
                                   shadowData, m_shadowPass.GetSRVSlot(),
                                   m_ibl.GetIrradianceSlot(),
                                   m_ibl.GetPrefilteredSlot(),
                                   m_ibl.GetBRDFLUTSlot(),
                                   hdrRTV,
                                   m_postProcess.GetSSAOSRVSlot(),
                                   m_postProcess.SSAOEnabled ? 1.0f : 0.0f,
                                   evpX, evpY, evpW, evpH,
                                   CAMERA_NEAR, CAMERA_FAR,
                                   spotSRVSlot, pointSRVSlot,
                                   contact);
        });

    m_rg.Compile();
    m_rg.Execute(cmd, &prof);

    // RG's LightingPass leaves GBuffer RTs in ALL_SHADER_RESOURCE (0xC0 = PSR|NON_PIXEL).
    // The next frame's SSAO pre-transition hardcodes StateBefore = PSR (0x80).
    // Restore to PSR now so the hardcoded value is valid every frame.
    m_rg.TransitionResource(cmd, hGB0, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    m_rg.TransitionResource(cmd, hGB1, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    m_rg.TransitionResource(cmd, hGB2, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    m_rg.TransitionResource(cmd, hGB3, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    m_rg.TransitionResource(cmd, hDepth, D3D12_RESOURCE_STATE_DEPTH_WRITE);

    // Translucency pass: forward PBR, sorted back-to-front, renders into HDR RT
    prof.BeginScope(cmd, "Translucency");
    {
        D3D12_CPU_DESCRIPTOR_HANDLE hdrRtv = m_postProcess.GetHDRRTV();
        m_translucencyPass.Execute(cmd, gfx, scene, frameIndex,
                                   m_geoMgr, m_texMgr, m_matMgr,
                                   viewProj, CameraPos, cameraForward,
                                   shadowData,
                                   m_shadowPass.GetSRVSlot(),
                                   m_ibl.GetIrradianceSlot(),
                                   m_ibl.GetPrefilteredSlot(),
                                   m_ibl.GetBRDFLUTSlot(),
                                   hdrRtv,
                                   evpX, evpY, evpW, evpH);
    }
    prof.EndScope(cmd);

    // Particle pass: forward alpha-blended, renders into HDR RT
    prof.BeginScope(cmd, "Particles");
    {
        D3D12_CPU_DESCRIPTOR_HANDLE hdrRtv = m_postProcess.GetHDRRTV();
        D3D12_CPU_DESCRIPTOR_HANDLE dsv    = gfx.GetCurrentDSV();
        cmd->OMSetRenderTargets(1, &hdrRtv, FALSE, &dsv);

        Matrix4x4 particleView = Matrix4x4::LookAt(CameraPos, CameraTarget, Vector3(0, 1, 0));
        m_particlePass.Execute(cmd, gfx, scene, m_texMgr, m_geoMgr, viewProj, particleView,
                               evpX, evpY, evpW, evpH, frameIndex, m_totalTime, dt,
                               m_gbuffer.GetResource(1), m_gbuffer.GetSRVSlot(1));
    }
    prof.EndScope(cmd);

    // Height fog (no-op unless enabled): exponential height fog + sun inscatter + optional volumetric
    // god rays (directional CSM ray-march). Composited into the HDR RT.
    prof.BeginScope(cmd, "Fog");
    m_postProcess.ExecuteFog(cmd, gfx,
                             gfx.GetDepthBuffer(), gfx.GetDepthSRVSlot(),
                             invViewProj, CameraPos, cameraForward, lightDir, sunColor,
                             m_shadowPass.GetSRVSlot(), shadowData.LightViewProj, shadowData.CascadeSplits,
                             frameIndex, evpX, evpY, evpW, evpH);
    prof.EndScope(cmd);

    // DDGI (no-op unless enabled): off-screen one-bounce indirect diffuse from the probe volume.
    // Complements SSGI (on-screen near-field). Additively composited into the HDR RT.
    prof.BeginScope(cmd, "DDGI");
    {
        const float gOrigin[3]  = { m_ddgi.Origin.x, m_ddgi.Origin.y, m_ddgi.Origin.z };
        const float gSpacing[3] = { m_ddgi.Spacing.x, m_ddgi.Spacing.y, m_ddgi.Spacing.z };
        // Capture: inject the lit scene into the probes + resolve (reads HDR before GI add = 1 bounce).
        m_postProcess.ExecuteDdgiCapture(cmd, gfx, m_gbuffer,
                                         gfx.GetDepthBuffer(), gfx.GetDepthSRVSlot(),
                                         invViewProj, m_ddgi,
                                         gOrigin, gSpacing, m_ddgi.Dims,
                                         frameIndex, evpX, evpY, evpW, evpH);
        // Apply: sample the probe volume and add indirect diffuse to the HDR.
        m_postProcess.ExecuteDdgi(cmd, gfx, m_gbuffer,
                                  gfx.GetDepthBuffer(), gfx.GetDepthSRVSlot(),
                                  invViewProj, m_ddgi.GetSRVSlot(),
                                  gOrigin, gSpacing, m_ddgi.Dims,
                                  frameIndex, evpX, evpY, evpW, evpH);
    }
    prof.EndScope(cmd);

    // SSGI (no-op unless enabled): one-bounce indirect diffuse, temporally accumulated + bilateral
    // denoised, composited into the HDR RT. Runs after lighting/fog (reads the lit HDR) and before SSR.
    // Reprojects GI history with the motion-vector RT (same convention as TAA).
    prof.BeginScope(cmd, "SSGI");
    m_postProcess.ExecuteSSGI(cmd, gfx, m_gbuffer,
                              gfx.GetDepthBuffer(), gfx.GetDepthSRVSlot(),
                              m_gbuffer.GetResource(3), m_gbuffer.GetSRVSlot(3),
                              viewProj, invViewProj, m_prevViewProjJittered, CameraPos,
                              jitterUVx - m_prevJitterUVx, jitterUVy - m_prevJitterUVy,
                              frameIndex, evpX, evpY, evpW, evpH);
    prof.EndScope(cmd);

    // SSR (no-op unless enabled): composites screen-space reflections into the HDR RT. Passes the DDGI
    // probe volume so off-screen reflection rays fall back to probe radiance instead of cutting out.
    prof.BeginScope(cmd, "SSR");
    {
        const float ssrGOrigin[3]  = { m_ddgi.Origin.x, m_ddgi.Origin.y, m_ddgi.Origin.z };
        const float ssrGSpacing[3] = { m_ddgi.Spacing.x, m_ddgi.Spacing.y, m_ddgi.Spacing.z };
        m_postProcess.ExecuteSSR(cmd, gfx, m_gbuffer,
                                 gfx.GetDepthBuffer(), gfx.GetDepthSRVSlot(),
                                 viewProj, invViewProj, CameraPos,
                                 frameIndex, evpX, evpY, evpW, evpH,
                                 m_ddgi.GetSRVSlot(), ssrGOrigin, ssrGSpacing, m_ddgi.Dims);
    }
    prof.EndScope(cmd);

    // TAA resolve (no-op unless enabled): reprojects history into the HDR RT before tonemap.
    prof.BeginScope(cmd, "TAA");
    m_postProcess.ExecuteTAA(cmd, gfx, frameIndex,
                             gfx.GetDepthBuffer(), gfx.GetDepthSRVSlot(),
                             m_gbuffer.GetResource(3), m_gbuffer.GetSRVSlot(3),
                             invViewProj, m_prevViewProjJittered,
                             jitterUVx - m_prevJitterUVx, jitterUVy - m_prevJitterUVy,
                             evpX, evpY, evpW, evpH);
    prof.EndScope(cmd);
    m_prevViewProjJittered = viewProj;
    m_prevJitterUVx = jitterUVx;
    m_prevJitterUVy = jitterUVy;
    ++m_frameCounter;

    // Post-process: bloom + tonemap + FXAA → back buffer
    prof.BeginScope(cmd, "PostFinal");
    m_postProcess.ExecuteFinal(cmd, gfx, frameIndex, evpX, evpY, evpW, evpH);
    prof.EndScope(cmd);

    // Restore depth/RTV for the ImGui pass that follows
    D3D12_CPU_DESCRIPTOR_HANDLE mainRtv = gfx.GetCurrentRTV();
    D3D12_CPU_DESCRIPTOR_HANDLE dsv     = gfx.GetCurrentDSV();
    cmd->OMSetRenderTargets(1, &mainRtv, FALSE, &dsv);
}

void SceneRenderer::Shutdown() {
    m_postProcess.Shutdown();
    m_translucencyPass.Shutdown();
    m_particlePass.Shutdown();
    m_ibl.Shutdown();
    m_shadowPass.Shutdown();
    m_lightingPass.Shutdown();
    m_gbufferPass.Shutdown();
    m_gbuffer.Shutdown();
    m_texMgr.Shutdown();
    m_geoMgr.Shutdown();
}

// ---------------------------------------------------------------------------
// Animation evaluation helpers (file-local)
// ---------------------------------------------------------------------------

namespace {

Vector3 SampleVec3(const std::vector<KeyVec3>& keys, float t) {
    if (keys.empty()) return Vector3(0.0f, 0.0f, 0.0f);
    if (keys.size() == 1 || t <= keys.front().Time) return keys.front().Value;
    if (t >= keys.back().Time)  return keys.back().Value;
    for (size_t i = 0; i + 1 < keys.size(); ++i) {
        if (t <= keys[i + 1].Time) {
            float d = keys[i + 1].Time - keys[i].Time;
            float a = (d > 1e-6f) ? (t - keys[i].Time) / d : 0.0f;
            const Vector3& v0 = keys[i].Value;
            const Vector3& v1 = keys[i + 1].Value;
            return Vector3(v0.x + a*(v1.x-v0.x), v0.y + a*(v1.y-v0.y), v0.z + a*(v1.z-v0.z));
        }
    }
    return keys.back().Value;
}

Quaternion SampleQuat(const std::vector<KeyQuat>& keys, float t) {
    if (keys.empty()) return Quaternion::Identity;
    if (keys.size() == 1 || t <= keys.front().Time) return keys.front().Value;
    if (t >= keys.back().Time)  return keys.back().Value;
    for (size_t i = 0; i + 1 < keys.size(); ++i) {
        if (t <= keys[i + 1].Time) {
            float d = keys[i + 1].Time - keys[i].Time;
            float a = (d > 1e-6f) ? (t - keys[i].Time) / d : 0.0f;
            return Quaternion::Slerp(keys[i].Value, keys[i + 1].Value, a);
        }
    }
    return keys.back().Value;
}

// Sample a clip at time t into per-joint LOCAL poses (TRS). Joints the clip doesn't animate fall
// back to their rest pose. Producing local TRS (rather than baked matrices) is what lets two poses
// be blended correctly — lerp position/scale, slerp rotation — for blend spaces / state machines.
void SampleClipPose(const AnimationClip& clip, const Skeleton& skel, float t,
                    std::vector<Transform>& outPose) {
    const uint32_t n = static_cast<uint32_t>(skel.Joints.size());
    outPose.resize(n);

    // joint index → its channel in this clip (nullptr if the clip doesn't drive that joint)
    std::vector<const NodeAnim*> channelForJoint(n, nullptr);
    for (auto& ch : clip.Channels) {
        auto it = skel.JointMap.find(ch.JointName);
        if (it != skel.JointMap.end()) channelForJoint[it->second] = &ch;
    }

    for (uint32_t ji = 0; ji < n; ++ji) {
        const NodeAnim* ch = channelForJoint[ji];
        if (ch) {
            Transform tr;
            tr.Position = SampleVec3(ch->PositionKeys, t);
            tr.Rotation = SampleQuat(ch->RotationKeys, t);
            tr.Scale    = ch->ScaleKeys.empty() ? Vector3(1.0f, 1.0f, 1.0f)
                                                : SampleVec3(ch->ScaleKeys, t);
            outPose[ji] = tr;
        } else {
            outPose[ji] = skel.Joints[ji].BindLocal;   // rest pose
        }
    }
}

// Build the GPU bone palette from per-joint local poses: accumulate the world hierarchy (parent ×
// local, matching the established M·v / world = parent*local convention) then × inverse bind pose.
void BuildPaletteFromPose(const Skeleton& skel, const std::vector<Transform>& pose,
                          std::array<Matrix4x4, MAX_BONES>& outPalette) {
    const uint32_t n = static_cast<uint32_t>(skel.Joints.size());
    std::vector<Matrix4x4> globalTx(n);
    for (uint32_t ji = 0; ji < n; ++ji) {
        Matrix4x4 local = pose[ji].ToMatrix();
        int32_t parent  = skel.Joints[ji].ParentIndex;
        globalTx[ji] = (parent < 0) ? local : globalTx[parent] * local;
    }
    const uint32_t cnt = (std::min)(n, MAX_BONES);
    for (uint32_t ji = 0; ji < cnt; ++ji)
        outPalette[ji] = globalTx[ji] * skel.Joints[ji].InverseBindPose;
    for (uint32_t ji = cnt; ji < MAX_BONES; ++ji)
        outPalette[ji] = Matrix4x4::Identity;
}

// Critically-damped smoothing (Unity's SmoothDamp): eases `current` toward `target` while tracking a
// persistent velocity, so motion accelerates AND decelerates smoothly with no overshoot or velocity
// jumps. `smoothTime` ≈ time to (mostly) arrive; smaller = snappier. Frame-rate independent.
float SmoothDampF(float current, float target, float& vel, float smoothTime, float dt) {
    smoothTime = (std::max)(1e-4f, smoothTime);
    const float omega = 2.0f / smoothTime;
    const float x = omega * dt;
    const float e = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
    const float change = current - target;
    const float temp = (vel + omega * change) * dt;
    vel = (vel - omega * temp) * e;
    return target + (change + temp) * e;
}
Vector3 SmoothDampV(const Vector3& cur, const Vector3& tgt, Vector3& vel, float smoothTime, float dt) {
    return Vector3(SmoothDampF(cur.x, tgt.x, vel.x, smoothTime, dt),
                   SmoothDampF(cur.y, tgt.y, vel.y, smoothTime, dt),
                   SmoothDampF(cur.z, tgt.z, vel.z, smoothTime, dt));
}

// Rotation vector (axis × angle) of a unit quaternion — the log map, used to split a tilt into axes.
Vector3 QuatToRotVec(const Quaternion& q) {
    const float w = (std::max)(-1.0f, (std::min)(1.0f, q.w));
    const float s = std::sqrt((std::max)(0.0f, 1.0f - w * w));
    if (s < 1e-6f) return Vector3(0.0f, 0.0f, 0.0f);             // ~identity
    float angle = 2.0f * std::acos(w);
    if (angle > 3.14159265f) angle -= 2.0f * 3.14159265f;       // shortest arc
    return Vector3(q.x / s, q.y / s, q.z / s) * angle;
}

// Shortest-arc rotation taking unit-ish `from` onto `to`.
Quaternion QuatFromTo(const Vector3& from, const Vector3& to) {
    Vector3 f = from.GetSafeNormal();
    Vector3 t = to.GetSafeNormal();
    float d = Vector3::Dot(f, t);
    if (d >= 1.0f - 1e-6f) return Quaternion();                 // already aligned
    if (d <= -1.0f + 1e-6f) {                                   // opposite → 180° about any perpendicular
        Vector3 axis = Vector3::Cross(Vector3(1, 0, 0), f);
        if (axis.LengthSquared() < 1e-6f) axis = Vector3::Cross(Vector3(0, 0, 1), f);
        return Quaternion::FromAxisAngle(axis.GetSafeNormal(), 3.14159265f);
    }
    Vector3 axis = Vector3::Cross(f, t);
    float s = std::sqrt((1.0f + d) * 2.0f);
    Quaternion q(axis.x / s, axis.y / s, axis.z / s, s * 0.5f);
    q.Normalize();
    return q;
}

// Best-fit ground plane through ≥3 points: least-squares y = a·x + b·z + c (Cramer's rule). Gives
// the slope normal (-a,1,-b) for body lean AND the plane itself, so the pelvis can react only to a
// foot's deviation FROM the plane (a real bump/step) — not to the overall slope, which the body tilt
// and the legs already absorb (otherwise the lowest foot on a slope drags the hips down too far).
struct GroundPlane { float a = 0.0f, b = 0.0f, c = 0.0f; Vector3 normal = Vector3(0,1,0); bool valid = false;
                     float YAt(float x, float z) const { return a*x + b*z + c; } };
GroundPlane FitGroundPlane(const std::vector<Vector3>& pts) {
    GroundPlane gp;
    if (pts.size() < 3) return gp;
    double Sxx=0,Sxz=0,Sx=0,Szz=0,Sz=0,S1=0,Sxy=0,Szy=0,Sy=0;
    for (const Vector3& p : pts) {
        double x=p.x, z=p.z, y=p.y;
        Sxx+=x*x; Sxz+=x*z; Sx+=x; Szz+=z*z; Sz+=z; S1+=1; Sxy+=x*y; Szy+=z*y; Sy+=y;
    }
    const double det = Sxx*(Szz*S1 - Sz*Sz) - Sxz*(Sxz*S1 - Sz*Sx) + Sx*(Sxz*Sz - Szz*Sx);
    if (std::abs(det) < 1e-9) return gp;
    gp.a = (float)((Sxy*(Szz*S1 - Sz*Sz) - Sxz*(Szy*S1 - Sz*Sy) + Sx*(Szy*Sz - Szz*Sy)) / det);
    gp.b = (float)((Sxx*(Szy*S1 - Sz*Sy) - Sxy*(Sxz*S1 - Sz*Sx) + Sx*(Sxz*Sy - Szy*Sx)) / det);
    gp.c = (float)((Sxx*(Szz*Sy - Szy*Sz) - Sxz*(Sxz*Sy - Szy*Sx) + Sxy*(Sxz*Sz - Szz*Sx)) / det);
    gp.normal = Vector3(-gp.a, 1.0f, -gp.b).GetSafeNormal();
    gp.valid  = true;
    return gp;
}

// ── Foot-placement IK ──────────────────────────────────────────────────────────────────────────
// Analytic two-bone IK: rotate the chain's root + mid so `end` (foot) reaches `targetMesh` (in the
// mesh space the pose lives in). Writes the new LOCAL rotations back into `pose`; `globals` are the
// current mesh-space joint matrices (read-only). The knee keeps its current bend direction (hinge
// axis taken from the present limb plane) so it never flips to the wrong side. If `alignMesh` is
// non-null the foot (end bone) is additionally tilted by it (mesh space) to match the ground normal.
void SolveTwoBoneIK(const Skeleton& skel, std::vector<Transform>& pose,
                    const std::vector<Matrix4x4>& globals,
                    int rootI, int midI, int endI, const Vector3& targetMesh,
                    const Quaternion* alignMesh, Vector3* hingeCache) {
    const Vector3 a = globals[rootI].GetTranslation();
    const Vector3 b = globals[midI].GetTranslation();
    const Vector3 c = globals[endI].GetTranslation();

    const float lab = (b - a).Length();
    const float lbc = (c - b).Length();
    if (lab < 1e-5f || lbc < 1e-5f) return;
    const float lat = (std::max)(1e-4f, (std::min)((targetMesh - a).Length(), lab + lbc - 1e-4f));

    auto nrm   = [](const Vector3& v) { return v.GetSafeNormal(); };
    auto sacos = [](float x) { return std::acos((std::max)(-1.0f, (std::min)(1.0f, x))); };

    const float ac_ab_0 = sacos(Vector3::Dot(nrm(c - a), nrm(b - a)));
    const float ba_bc_0 = sacos(Vector3::Dot(nrm(a - b), nrm(c - b)));
    const float ac_at_0 = sacos(Vector3::Dot(nrm(c - a), nrm(targetMesh - a)));
    const float ac_ab_1 = sacos((lab * lab + lat * lat - lbc * lbc) / (2.0f * lab * lat));
    const float ba_bc_1 = sacos((lab * lab + lbc * lbc - lat * lat) / (2.0f * lab * lbc));

    // Hinge axis = current limb-plane normal (preserves bend direction). Near full extension the cross
    // collapses and would pick a random plane (a subtle knee twitch); reuse the last good axis instead
    // so the bend stays in a consistent, natural plane.
    Vector3 axis = Vector3::Cross(c - a, b - a);
    if (axis.LengthSquared() > 1e-8f) {
        axis = axis.GetSafeNormal();
        if (hingeCache) *hingeCache = axis;                                   // remember a good axis
    } else if (hingeCache && hingeCache->LengthSquared() > 1e-6f) {
        axis = *hingeCache;                                                   // reuse last good
    } else {
        axis = Vector3::Cross(c - a, Vector3(0.0f, 0.0f, 1.0f));
        if (axis.LengthSquared() < 1e-10f) axis = Vector3(1.0f, 0.0f, 0.0f);
        axis = axis.GetSafeNormal();
    }

    const Vector3 aimAxis = Vector3::Cross(c - a, targetMesh - a);
    const Quaternion Raim  = (aimAxis.LengthSquared() > 1e-10f)
                           ? Quaternion::FromAxisAngle(aimAxis.GetSafeNormal(), ac_at_0) : Quaternion();
    const Quaternion RbendA = Quaternion::FromAxisAngle(axis, ac_ab_1 - ac_ab_0);
    const Quaternion RbendB = Quaternion::FromAxisAngle(axis, ba_bc_1 - ba_bc_0);

    const Quaternion gA = Transform::FromMatrix(globals[rootI]).Rotation;
    const Quaternion gB = Transform::FromMatrix(globals[midI]).Rotation;
    const Quaternion gA1 = Raim * RbendA * gA;            // new world rotation of root (bend + aim)
    const Quaternion gB1 = Raim * RbendA * RbendB * gB;   // new world rotation of mid (root's + knee)

    const int32_t pRoot = skel.Joints[rootI].ParentIndex;
    const Quaternion parentRootRot = (pRoot < 0) ? Quaternion()
                                                 : Transform::FromMatrix(globals[pRoot]).Rotation;
    // mid's parent is normally root; use root's NEW global rotation when so.
    const int32_t pMid = skel.Joints[midI].ParentIndex;
    const Quaternion parentMidRot = (pMid == rootI) ? gA1
                                  : (pMid < 0) ? Quaternion()
                                  : Transform::FromMatrix(globals[pMid]).Rotation;

    pose[rootI].Rotation = parentRootRot.Inverse() * gA1; pose[rootI].Rotation.Normalize();
    pose[midI].Rotation  = parentMidRot.Inverse()  * gB1; pose[midI].Rotation.Normalize();

    // Preserve the foot's ANIMATED world orientation. Without this the foot keeps its local rotation
    // relative to the shin, so as the leg bends to reach the target the foot is dragged round and the
    // sole ends up pointing backward/sideways (a visible hitch each stride). Re-derive the foot's local
    // rotation so its world orientation stays the animated one (optionally tilted to the ground normal).
    // Foot's parent is mid, whose new world rotation is gB1.
    const Quaternion gFootOld = Transform::FromMatrix(globals[endI]).Rotation;   // animated foot world rot
    const Quaternion footGoal = alignMesh ? (*alignMesh) * gFootOld : gFootOld;
    pose[endI].Rotation = gB1.Inverse() * footGoal;
    pose[endI].Rotation.Normalize();
}

// Ground-plant every leg of `ik` for `actor`. Two passes: (1) trace each foot and, if any foot would
// have to drop to reach the ground, lower the PELVIS by that much (smoothed/clamped) so the leg can
// reach instead of over-extending; (2) two-bone-IK each foot onto its traced ground (+ optional tilt
// to the surface normal). Feet already above the ground (swing) are left as animated.
void ApplyFootIK(Actor* actor, const Skeleton& skel, std::vector<Transform>& pose,
                 FootIKComponent& ik, float dt) {
    auto* tc = actor->GetComponent<TransformComponent>();
    if (!tc || ik.Weight <= 0.0f) return;
    SceneManager* scene = actor->GetScene();
    PhysicsWorld* phys  = scene ? scene->GetPhysicsWorld() : nullptr;
    if (!phys) return;

    const Transform& world = tc->CachedWorld;
    const uint32_t n = static_cast<uint32_t>(skel.Joints.size());
    if (ik.m_footRaise.size() != ik.Legs.size()) {
        ik.m_footRaise.assign(ik.Legs.size(), 0.0f);
        ik.m_footRaiseVel.assign(ik.Legs.size(), 0.0f);
        ik.m_kneeAxis.assign(ik.Legs.size(), Vector3(0.0f, 0.0f, 0.0f));
        ik.m_lockPos.assign(ik.Legs.size(), Vector3(0.0f, 0.0f, 0.0f));
        ik.m_lockAlpha.assign(ik.Legs.size(), 0.0f);
        ik.m_lockAlphaVel.assign(ik.Legs.size(), 0.0f);
    }

    auto computeGlobals = [&](std::vector<Matrix4x4>& g) {
        g.resize(n);
        for (uint32_t ji = 0; ji < n; ++ji) {
            Matrix4x4 local = pose[ji].ToMatrix();
            int32_t parent  = skel.Joints[ji].ParentIndex;
            g[ji] = (parent < 0) ? local : g[parent] * local;
        }
    };
    std::vector<Matrix4x4> globals; computeGlobals(globals);

    // ── PASS 1: trace each foot. We work RELATIVE to the character's base ground plane (its feet
    // level = the actor's world Y): a foot is shifted only by how much the ground under it deviates
    // from that plane. On flat ground every deviation is 0, so the IK does nothing and the animation
    // is untouched (no per-foot offset tuning, no stride hitch); steps/slopes drive the deviation. ──
    const float referenceY = world.Position.y;
    struct Probe { size_t legIdx; int rootI, midI, endI; bool hit; Vector3 footWorld; float terrainLift; float footHeight; Vector3 normal; };
    std::vector<Probe>   probes;
    std::vector<Vector3> groundPts;   // world contact points, for the body-lean plane fit
    for (size_t li = 0; li < ik.Legs.size(); ++li) {
        const IKLeg& leg = ik.Legs[li];
        auto ri = skel.JointMap.find(leg.Root);
        auto mi = skel.JointMap.find(leg.Mid);
        auto ei = skel.JointMap.find(leg.End);
        if (ri == skel.JointMap.end() || mi == skel.JointMap.end() || ei == skel.JointMap.end()) continue;
        Probe pr; pr.legIdx = li; pr.rootI = (int)ri->second; pr.midI = (int)mi->second; pr.endI = (int)ei->second;
        pr.footWorld   = world.TransformPoint(globals[pr.endI].GetTranslation());
        pr.terrainLift = 0.0f;
        pr.footHeight  = 1e9f;   // huge = treated as airborne (no lock) until a trace says otherwise
        RayHit hit;
        const Vector3 origin(pr.footWorld.x, pr.footWorld.y + ik.TraceUp, pr.footWorld.z);
        pr.hit = phys->Raycast(origin, Vector3(0.0f, -1.0f, 0.0f), ik.TraceUp + ik.TraceDown, hit, actor);
        if (pr.hit) {
            // Deviation of the ground under this foot from the base plane (+ optional fine-tune offsets).
            const float lift = (hit.Point.y - referenceY) + ik.FootOffset + leg.Offset;
            pr.terrainLift = (std::max)(-ik.MaxRaise, (std::min)(ik.MaxRaise, lift));
            pr.footHeight  = pr.footWorld.y - hit.Point.y;   // animated foot height above its ground
            pr.normal      = hit.Normal;
            groundPts.push_back(Vector3(pr.footWorld.x, hit.Point.y, pr.footWorld.z));
        }
        probes.push_back(pr);
    }

    // ── PELVIS: lower the hip bone by the clamped/smoothed drop, then refresh globals. ──
    const GroundPlane plane = FitGroundPlane(groundPts);
    auto pbIt = ik.PelvisBone.empty() ? skel.JointMap.end() : skel.JointMap.find(ik.PelvisBone);
    if (pbIt != skel.JointMap.end()) {
        const int     pb    = (int)pbIt->second;
        const int32_t paren = skel.Joints[pb].ParentIndex;
        const Transform parentT = (paren < 0) ? Transform() : Transform::FromMatrix(globals[paren]);

        // Drop: lower the pelvis only by how far the lowest foot sits BELOW the fitted ground plane —
        // i.e. local unevenness (a step/bump), not the overall slope (the body tilt + legs handle that).
        // On a uniform ramp every foot is on the plane → ~0 drop, so the hips don't sink going downhill.
        float lowestResidual = 0.0f;
        if (plane.valid)
            for (const Vector3& p : groundPts)
                lowestResidual = (std::min)(lowestResidual, p.y - plane.YAt(p.x, p.z));
        const float targetDrop = (std::max)(-ik.MaxPelvisDrop, (std::min)(0.0f, lowestResidual)) * ik.Weight;
        ik.m_pelvisOffset = SmoothDampF(ik.m_pelvisOffset, targetDrop, ik.m_pelvisVel,
                                        ik.PelvisSmoothness > 0.0f ? 1.0f / ik.PelvisSmoothness : 0.0f, dt);

        Vector3 meshDelta = world.Rotation.Inverse() * Vector3(0.0f, ik.m_pelvisOffset, 0.0f);
        meshDelta = Vector3(meshDelta.x / world.Scale.x, meshDelta.y / world.Scale.y, meshDelta.z / world.Scale.z);
        Vector3 localDelta = parentT.Rotation.Inverse() * meshDelta;
        localDelta = Vector3(localDelta.x / parentT.Scale.x, localDelta.y / parentT.Scale.y, localDelta.z / parentT.Scale.z);
        pose[pb].Position = pose[pb].Position + localDelta;

        // Lean: rotate the pelvis (whole torso) toward the ground slope so the body follows the
        // ramp/stairs. Slope = best-fit plane normal through the contact points, smoothed, scaled by
        // BodyTiltWeight. World tilt (maps world-up → slope normal) conjugated into mesh space.
        if (ik.BodyTilt) {
            const Vector3 rawN = plane.normal;
            ik.m_bodyNormal = SmoothDampV(ik.m_bodyNormal, rawN, ik.m_bodyNormalVel,
                                          ik.BodyTiltSmoothness > 0.0f ? 1.0f / ik.BodyTiltSmoothness : 0.0f, dt)
                              .GetSafeNormal();

            // Split the full "up → slope normal" tilt into PITCH (about the body's right axis, used when
            // facing up/down the slope) and ROLL (about the body's forward axis, used when traversing
            // across it), and scale each independently — a steep climb can pitch hard while a sideways
            // traverse drops the downhill side, without one forcing the other.
            const Vector3 rv    = QuatToRotVec(QuatFromTo(Vector3(0.0f, 1.0f, 0.0f), ik.m_bodyNormal));
            const Vector3 right = world.Rotation * Vector3(1.0f, 0.0f, 0.0f);
            const Vector3 fwd   = world.Rotation * Vector3(0.0f, 0.0f, 1.0f);
            const Vector3 leanRv = right * (Vector3::Dot(rv, right) * ik.BodyTiltPitch)
                                 + fwd   * (Vector3::Dot(rv, fwd)   * ik.BodyTiltRoll);
            const float   ang   = leanRv.Length();
            const Quaternion tiltWorld = (ang > 1e-5f)
                ? Quaternion::FromAxisAngle(leanRv * (1.0f / ang), ang) : Quaternion();

            const Quaternion tiltMesh = world.Rotation.Inverse() * tiltWorld * world.Rotation;
            const Quaternion gP    = Transform::FromMatrix(globals[pb]).Rotation;
            pose[pb].Rotation = parentT.Rotation.Inverse() * (tiltMesh * gP);
            pose[pb].Rotation.Normalize();
        }

        computeGlobals(globals);
    }

    // ── PASS 2: plant each foot via two-bone IK, easing the lift in/out per foot. The raw lift snaps
    // on/off as the animated foot crosses the ground each stride (and the rear-foot offset makes that
    // jump big), which pops on flat ground; smoothing the per-foot lift toward its target removes it
    // and also keeps IK off fully-extended swing legs (where the two-bone solve is ill-conditioned). ──
    const float footST = (ik.FootSmoothness > 0.0f) ? 1.0f / ik.FootSmoothness : 0.0f;
    const float lockST = (ik.LockSmoothness > 0.0f) ? 1.0f / ik.LockSmoothness : 0.0f;
    for (const Probe& pr : probes) {
        // Critically-damped ease of the per-foot terrain deviation (×Weight). The smoothed value is the
        // foot's vertical shift from its animated position; on flat ground it stays 0 (foot = animated).
        float& lift = ik.m_footRaise[pr.legIdx];
        lift = SmoothDampF(lift, pr.terrainLift * ik.Weight, ik.m_footRaiseVel[pr.legIdx], footST, dt);

        const Vector3 footWorld = world.TransformPoint(globals[pr.endI].GetTranslation());
        // The foot's intended world position from animation + terrain shift (XZ animated, Y adjusted).
        Vector3 target(footWorld.x, pr.footWorld.y + lift, footWorld.z);

        // Foot lock: while planted (low animated height), freeze the world position so the foot doesn't
        // skate as the body moves over it; blend out for swing. lockAlpha is 1 planted → 0 lifted.
        if (ik.FootLock) {
            const float rawPlant = (ik.LockHeight > 1e-4f)
                ? (std::max)(0.0f, (std::min)(1.0f, 1.0f - pr.footHeight / ik.LockHeight)) : 0.0f;
            float& alpha = ik.m_lockAlpha[pr.legIdx];
            alpha = SmoothDampF(alpha, rawPlant, ik.m_lockAlphaVel[pr.legIdx], lockST, dt);
            if (alpha < 0.5f) ik.m_lockPos[pr.legIdx] = target;            // track while free, freeze while planted
            const Vector3& lp = ik.m_lockPos[pr.legIdx];
            target = target + (lp - target) * (alpha * ik.Weight);        // hold the planted foot in place
        }

        // Skip only when there's effectively no correction (flat ground, foot free) — no IK, no hitch.
        if ((target - footWorld).LengthSquared() < 1e-8f) continue;

        const Vector3 targetMesh = world.InverseTransformPoint(target);

        // Foot tilt to the surface normal, faded in with the shift magnitude so it never pops.
        Quaternion alignMesh;
        const Quaternion* alignPtr = nullptr;
        if (ik.AlignToNormal && pr.hit && pr.normal.y > 0.3f) {
            const float a = (std::min)(1.0f, std::abs(lift) / 0.1f) * ik.Weight;
            if (a > 1e-3f) {
                Quaternion tiltWorld = Quaternion::Slerp(Quaternion(),
                                           QuatFromTo(Vector3(0.0f, 1.0f, 0.0f), pr.normal), a);
                alignMesh = world.Rotation.Inverse() * tiltWorld * world.Rotation;
                alignPtr  = &alignMesh;
            }
        }
        SolveTwoBoneIK(skel, pose, globals, pr.rootI, pr.midI, pr.endI, targetMesh, alignPtr,
                       &ik.m_kneeAxis[pr.legIdx]);
    }
}

// Apply optional foot IK to the sampled pose, then build the GPU palette (single seam for all paths).
void FinalizePose(Actor* actor, const Skeleton& skel, std::vector<Transform>& pose,
                  std::array<Matrix4x4, MAX_BONES>& outPalette, float dt) {
    if (auto* ik = actor->GetComponent<FootIKComponent>())
        if (ik->Enabled) ApplyFootIK(actor, skel, pose, *ik, dt);
    BuildPaletteFromPose(skel, pose, outPalette);
}

const AnimationClip* FindClip(const SkeletalMeshAsset& mesh, const std::string& name) {
    for (auto& c : mesh.Clips)
        if (name.empty() || c.Name == name) return &c;   // empty → first clip
    return nullptr;
}

void FillRestPose(const Skeleton& skel, std::vector<Transform>& outPose) {
    outPose.resize(skel.Joints.size());
    for (size_t i = 0; i < skel.Joints.size(); ++i) outPose[i] = skel.Joints[i].BindLocal;
}

// Sample a clip (or rest pose if missing) at a normalised loop phase [0,1).
void SamplePoseAtPhase(const SkeletalMeshAsset& mesh, const AnimationClip* clip, float phase,
                       std::vector<Transform>& outPose) {
    if (!clip) { FillRestPose(mesh.Skel, outPose); return; }
    SampleClipPose(*clip, mesh.Skel, phase * clip->DurationSeconds, outPose);
}

// Evaluate a 1D blend space at parameter value v into a blended local pose. The two samples
// bracketing v are phase-synced (a single shared normalised phase so feet stay aligned across
// Walk/Run) and blended. `phase` is advanced by dt scaled by the blended clip duration.
void EvaluateBlendSpace1D(const SkeletalMeshAsset& mesh, const BlendSpace1D& bs, float v,
                          float& phase, float dt, std::vector<Transform>& outPose) {
    if (bs.Samples.empty()) { FillRestPose(mesh.Skel, outPose); return; }

    // Pick the two samples bracketing v (Samples assumed sorted by Threshold ascending).
    const BlendSample* a = &bs.Samples.front();
    const BlendSample* b = &bs.Samples.front();
    float w = 0.0f;
    if (v >= bs.Samples.back().Threshold) {
        a = b = &bs.Samples.back();
    } else if (v > bs.Samples.front().Threshold) {
        for (size_t i = 0; i + 1 < bs.Samples.size(); ++i) {
            if (v >= bs.Samples[i].Threshold && v <= bs.Samples[i + 1].Threshold) {
                a = &bs.Samples[i]; b = &bs.Samples[i + 1];
                float d = b->Threshold - a->Threshold;
                w = (d > 1e-6f) ? (v - a->Threshold) / d : 0.0f;
                break;
            }
        }
    }

    const AnimationClip* ca = FindClip(mesh, a->ClipName);
    const AnimationClip* cb = FindClip(mesh, b->ClipName);

    // Advance the shared phase by the blended cycle duration.
    float da = ca ? ca->DurationSeconds : 0.0f;
    float db = cb ? cb->DurationSeconds : 0.0f;
    float effDur = da + (db - da) * w;
    if (effDur > 1e-4f) { phase += dt / effDur; phase -= std::floor(phase); }

    if (a == b || !cb || w <= 0.0f) { SamplePoseAtPhase(mesh, ca, phase, outPose); return; }
    if (w >= 1.0f)                  { SamplePoseAtPhase(mesh, cb, phase, outPose); return; }

    std::vector<Transform> poseA, poseB;
    SamplePoseAtPhase(mesh, ca, phase, poseA);
    SamplePoseAtPhase(mesh, cb, phase, poseB);
    outPose.resize(poseA.size());
    for (size_t i = 0; i < outPose.size(); ++i) outPose[i] = Transform::Blend(poseA[i], poseB[i], w);
}

// Evaluate one state (a single clip or a blend space) into a local pose, advancing the shared phase.
void EvaluateState(const SkeletalMeshAsset& mesh, const AnimState& st, AnimationComponent& a,
                   float step, std::vector<Transform>& out) {
    if (st.Type == AnimNodeType::BlendSpace) {
        float v = a.GetParam(st.Blend.Param);
        EvaluateBlendSpace1D(mesh, st.Blend, v, a.Phase, step * st.Speed, out);
        return;
    }
    const AnimationClip* clip = FindClip(mesh, st.ClipName);
    if (clip && clip->DurationSeconds > 1e-4f) {
        a.Phase += (step * st.Speed) / clip->DurationSeconds;
        if (st.Loop) a.Phase -= std::floor(a.Phase);
        else         a.Phase = (std::min)(a.Phase, 1.0f);
    }
    SamplePoseAtPhase(mesh, clip, a.Phase, out);
}

bool EvalTransitionCond(const AnimTransition& tr, const AnimationComponent& a) {
    if (tr.Param.empty()) return true;   // unconditional transition
    float v = a.GetParam(tr.Param);
    switch (tr.Op) {
        case CompareOp::Greater:      return v >  tr.Value;
        case CompareOp::Less:         return v <  tr.Value;
        case CompareOp::GreaterEqual: return v >= tr.Value;
        case CompareOp::LessEqual:    return v <= tr.Value;
    }
    return false;
}

// Run the state machine: pick/start transitions, advance the current state, cross-fade from the
// pose snapshotted at the last transition. v1 freezes the from-pose during the (short) blend and
// only starts a new transition once the current cross-fade finishes — simple and pop-free.
void EvaluateStateMachine(const SkeletalMeshAsset& mesh, AnimationComponent& a, float step,
                          std::vector<Transform>& out) {
    AnimStateMachine& sm = a.StateMachine;
    const int n = static_cast<int>(sm.States.size());
    if (n == 0) { FillRestPose(mesh.Skel, out); return; }

    if (a.CurState < 0 || a.CurState >= n) {   // initialise on first evaluation
        a.CurState = (sm.DefaultState >= 0 && sm.DefaultState < n) ? sm.DefaultState : 0;
        a.StateTime = a.Phase = a.BlendElapsed = a.BlendDuration = 0.0f;
        a.PrevState = -1;
    }

    bool blending = a.BlendDuration > 1e-4f && a.BlendElapsed < a.BlendDuration;

    if (!blending) {
        for (const auto& tr : sm.Transitions) {
            if (tr.To < 0 || tr.To >= n || tr.To == a.CurState) continue;
            if (!(tr.From < 0 || tr.From == a.CurState))        continue;
            if (!EvalTransitionCond(tr, a))                     continue;
            EvaluateState(mesh, sm.States[a.CurState], a, 0.0f, a.BlendFromPose);  // freeze from-pose
            a.PrevState     = a.CurState;
            a.CurState      = tr.To;
            a.StateTime     = 0.0f;
            a.Phase         = 0.0f;
            a.BlendElapsed  = 0.0f;
            a.BlendDuration = (tr.BlendTime > 0.0f) ? tr.BlendTime : 0.0f;
            blending        = a.BlendDuration > 1e-4f;
            break;
        }
    }

    a.StateTime += step;

    std::vector<Transform> cur;
    EvaluateState(mesh, sm.States[a.CurState], a, step, cur);

    if (blending && a.BlendFromPose.size() == cur.size()) {
        a.BlendElapsed += step;
        float bw = (a.BlendDuration > 1e-4f) ? (a.BlendElapsed / a.BlendDuration) : 1.0f;
        bw = (bw < 0.0f) ? 0.0f : (bw > 1.0f ? 1.0f : bw);
        out.resize(cur.size());
        for (size_t i = 0; i < cur.size(); ++i)
            out[i] = Transform::Blend(a.BlendFromPose[i], cur[i], bw);
    } else {
        out = std::move(cur);
    }
}

} // anonymous namespace

void SceneRenderer::UpdateParticles(const SceneManager& scene, float dt) {
    for (auto& actorPtr : scene.GetActors()) {
        auto* pc = actorPtr->GetComponent<ParticleComponent>();
        if (!pc) continue;
        auto* tc = actorPtr->GetComponent<TransformComponent>();
        Vector3 worldPos = tc ? tc->GetWorldMatrix().GetTranslation() : Vector3{};
        for (auto& em : pc->GetEmitters())
            em.Update(dt, worldPos);
    }
}

void SceneRenderer::UpdateAnimations(const SceneManager& scene, float dt) {
    for (auto& actorPtr : scene.GetActors()) {
        Actor* actor = actorPtr.get();
        auto* animComp = actor->GetComponent<AnimationComponent>();
        auto* meshComp = actor->GetComponent<MeshComponent>();
        if (!animComp || !meshComp) continue;

        // Snapshot last frame's palette for skeletal motion vectors before we overwrite it.
        // On the very first frame BonePalette is identity → prev==identity, but TAA history is
        // invalid that frame anyway, so the large initial motion vector is harmless.
        animComp->PrevBonePalette = animComp->BonePalette;

        const SkeletalMeshAsset* skelMesh = m_geoMgr.LoadSkeletalMesh(meshComp->MeshPath);
        if (!skelMesh || skelMesh->Clips.empty()) {
            // No clips → identity palette so mesh renders in bind pose
            animComp->BonePalette.fill(Matrix4x4::Identity);
            animComp->PaletteReady = true;
            continue;
        }

        // ── Higher-level path: animation state machine (states = clips / blend spaces). ──
        if (animComp->UseStateMachine) {
            float step = animComp->Playing ? dt * animComp->Speed : 0.0f;
            std::vector<Transform> pose;
            EvaluateStateMachine(*skelMesh, *animComp, step, pose);
            FinalizePose(actor, skelMesh->Skel, pose, animComp->BonePalette, dt);
            animComp->PaletteReady = true;
            continue;
        }

        // ── Higher-level path: 1D blend space (e.g. Speed → Idle/Walk/Run). ──
        if (animComp->UseBlendSpace) {
            float step = animComp->Playing ? dt * animComp->Speed : 0.0f;
            float v    = animComp->GetParam(animComp->Blend.Param);
            std::vector<Transform> pose;
            EvaluateBlendSpace1D(*skelMesh, animComp->Blend, v, animComp->Phase, step, pose);
            FinalizePose(actor, skelMesh->Skel, pose, animComp->BonePalette, dt);
            animComp->PaletteReady = true;
            continue;
        }

        // Find the requested clip (or default to first)
        const AnimationClip* clip = nullptr;
        for (auto& c : skelMesh->Clips) {
            if (animComp->ClipName.empty() || c.Name == animComp->ClipName) { clip = &c; break; }
        }
        if (!clip) { animComp->BonePalette.fill(Matrix4x4::Identity); animComp->PaletteReady = true; continue; }

        // Advance time
        if (animComp->Playing && clip->DurationSeconds > 0.0f) {
            animComp->Time += dt * animComp->Speed;
            if (animComp->Loop)
                animComp->Time = std::fmod(animComp->Time, clip->DurationSeconds);
            else
                animComp->Time = (std::min)(animComp->Time, clip->DurationSeconds);
        }

        // Apply phase offset at sample point so each actor can start at a different point in the cycle.
        float t = animComp->Time + animComp->TimeOffset;
        if (clip->DurationSeconds > 0.0f) {
            t = std::fmod(t, clip->DurationSeconds);
            if (t < 0.0f) t += clip->DurationSeconds;
        }

        // Sample the clip into local poses, then build the palette. Routing through a local-pose
        // array (rather than baking matrices inline) is the seam blend spaces / the state machine
        // hook into — they sample multiple clips and Transform::Blend before BuildPaletteFromPose.
        std::vector<Transform> pose;
        SampleClipPose(*clip, skelMesh->Skel, t, pose);
        FinalizePose(actor, skelMesh->Skel, pose, animComp->BonePalette, dt);

        animComp->PaletteReady = true;
    }
}

} // namespace Fujin
