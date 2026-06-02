#include "SceneRenderer.h"
#include "RenderGraph/RGTypes.h"
#include "Engine/Core/SceneManager.h"
#include "Engine/Core/Actor.h"
#include "Engine/Core/LightComponent.h"
#include "Engine/Core/TransformComponent.h"
#include "Engine/Core/MeshComponent.h"
#include "Engine/Core/AnimationComponent.h"
#include "Engine/Core/ParticleComponent.h"
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
                                   spotSRVSlot, pointSRVSlot);
        });

    m_rg.Compile();
    m_rg.Execute(cmd);

    // RG's LightingPass leaves GBuffer RTs in ALL_SHADER_RESOURCE (0xC0 = PSR|NON_PIXEL).
    // The next frame's SSAO pre-transition hardcodes StateBefore = PSR (0x80).
    // Restore to PSR now so the hardcoded value is valid every frame.
    m_rg.TransitionResource(cmd, hGB0, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    m_rg.TransitionResource(cmd, hGB1, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    m_rg.TransitionResource(cmd, hGB2, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    m_rg.TransitionResource(cmd, hGB3, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    m_rg.TransitionResource(cmd, hDepth, D3D12_RESOURCE_STATE_DEPTH_WRITE);

    // Translucency pass: forward PBR, sorted back-to-front, renders into HDR RT
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

    // Particle pass: forward alpha-blended, renders into HDR RT
    {
        D3D12_CPU_DESCRIPTOR_HANDLE hdrRtv = m_postProcess.GetHDRRTV();
        D3D12_CPU_DESCRIPTOR_HANDLE dsv    = gfx.GetCurrentDSV();
        cmd->OMSetRenderTargets(1, &hdrRtv, FALSE, &dsv);

        Matrix4x4 particleView = Matrix4x4::LookAt(CameraPos, CameraTarget, Vector3(0, 1, 0));
        m_particlePass.Execute(cmd, gfx, scene, viewProj, particleView,
                               evpX, evpY, evpW, evpH, frameIndex, m_totalTime, dt);
    }

    // Height fog (no-op unless enabled): exponential height fog + sun inscatter + optional volumetric
    // god rays (directional CSM ray-march). Composited into the HDR RT.
    m_postProcess.ExecuteFog(cmd, gfx,
                             gfx.GetDepthBuffer(), gfx.GetDepthSRVSlot(),
                             invViewProj, CameraPos, cameraForward, lightDir, sunColor,
                             m_shadowPass.GetSRVSlot(), shadowData.LightViewProj, shadowData.CascadeSplits,
                             frameIndex, evpX, evpY, evpW, evpH);

    // SSR (no-op unless enabled): composites screen-space reflections into the HDR RT.
    m_postProcess.ExecuteSSR(cmd, gfx, m_gbuffer,
                             gfx.GetDepthBuffer(), gfx.GetDepthSRVSlot(),
                             viewProj, invViewProj, CameraPos,
                             frameIndex, evpX, evpY, evpW, evpH);

    // TAA resolve (no-op unless enabled): reprojects history into the HDR RT before tonemap.
    m_postProcess.ExecuteTAA(cmd, gfx, frameIndex,
                             gfx.GetDepthBuffer(), gfx.GetDepthSRVSlot(),
                             m_gbuffer.GetResource(3), m_gbuffer.GetSRVSlot(3),
                             invViewProj, m_prevViewProjJittered,
                             jitterUVx - m_prevJitterUVx, jitterUVy - m_prevJitterUVy,
                             evpX, evpY, evpW, evpH);
    m_prevViewProjJittered = viewProj;
    m_prevJitterUVx = jitterUVx;
    m_prevJitterUVy = jitterUVy;
    ++m_frameCounter;

    // Post-process: bloom + tonemap + FXAA → back buffer
    m_postProcess.ExecuteFinal(cmd, gfx, frameIndex, evpX, evpY, evpW, evpH);

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

// Build a TRS local matrix from three sampled channels.
Matrix4x4 BuildLocalTRS(const Vector3& pos, const Quaternion& rot, const Vector3& scale) {
    Matrix4x4 T = Matrix4x4::Translation(pos);
    Matrix4x4 R = rot.ToMatrix();
    Matrix4x4 S = Matrix4x4::Scale(scale);
    return T * R * S;
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

        const Skeleton& skel = skelMesh->Skel;
        uint32_t numJoints = static_cast<uint32_t>(skel.Joints.size());

        // Build a fast lookup: joint name → channel index
        std::vector<const NodeAnim*> channelForJoint(numJoints, nullptr);
        for (auto& ch : clip->Channels) {
            auto it = skel.JointMap.find(ch.JointName);
            if (it != skel.JointMap.end())
                channelForJoint[it->second] = &ch;
        }

        // Compute local and global transforms
        std::vector<Matrix4x4> globalTx(numJoints);
        for (uint32_t ji = 0; ji < numJoints; ++ji) {
            Matrix4x4 local;
            const NodeAnim* ch = channelForJoint[ji];
            if (ch) {
                Vector3    pos   = SampleVec3(ch->PositionKeys, t);
                Quaternion rot   = SampleQuat(ch->RotationKeys, t);
                Vector3    scale = SampleVec3(ch->ScaleKeys,    t);
                if (ch->ScaleKeys.empty()) scale = Vector3(1.0f, 1.0f, 1.0f);
                local = BuildLocalTRS(pos, rot, scale);
            } else {
                local = skel.Joints[ji].BindPoseLocal;
            }

            int32_t parent = skel.Joints[ji].ParentIndex;
            globalTx[ji] = (parent < 0) ? local : globalTx[parent] * local;
        }

        // Bone palette = globalTransform × inverseBindPose
        uint32_t paletteCount = (std::min)(numJoints, MAX_BONES);
        for (uint32_t ji = 0; ji < paletteCount; ++ji)
            animComp->BonePalette[ji] = globalTx[ji] * skel.Joints[ji].InverseBindPose;
        for (uint32_t ji = paletteCount; ji < MAX_BONES; ++ji)
            animComp->BonePalette[ji] = Matrix4x4::Identity;

        animComp->PaletteReady = true;
    }
}

} // namespace Fujin
