#include "LightingPass.h"
#include "Engine/Core/SceneManager.h"
#include "Engine/Core/Actor.h"
#include "Engine/Core/LightComponent.h"
#include "Engine/Core/TransformComponent.h"
#include "Engine/Graphics/DxcHelper.h"
#include <stdexcept>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cfloat>

namespace Fujin {

// Matches StructuredBuffer<LightData> in LightingPass.PS.hlsl (64-byte stride).
struct GpuLight {
    float pos[3];   float type;
    float dir[3];   float range;
    float color[3]; float intensity;
    float spotAngle; float shadowIndex; float pad[2];   // shadowIndex = -1 if no spot shadow
};
static constexpr uint32_t GPU_LIGHT_STRIDE = sizeof(GpuLight);   // 64

// CPU-side light data used only for cluster culling (world sphere + directional flag).
struct LightCullItem {
    Vector3 pos;
    float   range;
    bool    directional;
};

// FNV-1a 64-bit, used to build shadow-cache signatures (light transform + in-range caster worlds).
static inline void FnvFold(uint64_t& h, const void* p, size_t n) {
    const uint8_t* b = static_cast<const uint8_t*>(p);
    for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
}
// Signature of a light (pos/dir/range/angle/actorId) plus every caster that ACTUALLY affects its
// shadow map. The affecting set must match the render-time cull exactly, otherwise a caster that is
// rendered but excluded here can move without invalidating the cache → stale shadow. So we use the
// same predicate the passes use: spot = AabbInFrustum(spotPlanes); point = overlap with the light's
// influence box [pos±range] (which is exactly the union of the 6 cube-face frustums). Skeletal
// casters have no tight bounds, so a center±radius box stands in. dynamicOut is set when any
// affecting caster is skeletal (animated → must re-render every frame).
static uint64_t ShadowCacheSig(uint64_t actorId, const Vector3& pos, const Vector3& dir,
                               float range, float angle,
                               const std::vector<ShadowCaster>& casters,
                               const Plane* spotPlanes /* null = point light */,
                               bool& dynamicOut) {
    uint64_t h = 1469598103934665603ull;
    FnvFold(h, &actorId, sizeof(actorId));
    FnvFold(h, &pos, sizeof(pos)); FnvFold(h, &dir, sizeof(dir));
    FnvFold(h, &range, sizeof(range)); FnvFold(h, &angle, sizeof(angle));
    dynamicOut = false;

    Aabb influence(Vector3(pos.x - range, pos.y - range, pos.z - range),
                   Vector3(pos.x + range, pos.y + range, pos.z + range));   // point: cube coverage

    for (const ShadowCaster& c : casters) {
        // The caster's world AABB (skeletal meshes carry no tight bounds → center±radius stand-in).
        Aabb cb = c.skeletal
                ? Aabb(Vector3(c.center.x - c.radius, c.center.y - c.radius, c.center.z - c.radius),
                       Vector3(c.center.x + c.radius, c.center.y + c.radius, c.center.z + c.radius))
                : c.box;
        bool affects = spotPlanes ? AabbInFrustum(cb, spotPlanes) : cb.Overlaps(influence);
        if (!affects) continue;
        FnvFold(h, &c.actorId, sizeof(c.actorId));
        FnvFold(h, c.world.v, 64);
        if (c.skeletal) dynamicOut = true;
    }
    return h;
}

static ComPtr<ID3D12Resource> CreateUploadCB(ID3D12Device* device, UINT64 size, uint8_t** mapped) {
    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width              = size;
    rd.Height             = 1;
    rd.DepthOrArraySize   = 1;
    rd.MipLevels          = 1;
    rd.Format             = DXGI_FORMAT_UNKNOWN;
    rd.SampleDesc.Count   = 1;
    rd.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> res;
    D3D12_RANGE range = { 0, 0 };
    device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&res));
    res->Map(0, &range, reinterpret_cast<void**>(mapped));
    return res;
}

bool LightingPass::Initialize(GraphicsDevice& gfx) {
    try {
        if (!CreateRootSignature(gfx.GetDevice())) return false;
        if (!CreatePipelineState(gfx.GetDevice())) return false;
        if (!CreateConstantBuffers(gfx)) return false;
    } catch (...) {
        return false;
    }
    return true;
}

bool LightingPass::CreateRootSignature(ID3D12Device* device) {
    // 0: Root CBV b0 (Camera)
    // 1: Root CBV b1 (Lights)
    // 2-5: Descriptor tables t0-t3 (GBuffer RTs + Depth)
    // 6: Root CBV b2 (Shadow)
    // 7: Descriptor table t4 (Shadow map array)
    // 8: Descriptor table t5 (IBL Irradiance TextureCube)
    // 9: Descriptor table t6 (IBL PreFilteredEnv TextureCube)
    // 10: Descriptor table t7 (IBL BRDF LUT Texture2D)
    // 11: Descriptor table t8 (SSAO texture)
    D3D12_ROOT_PARAMETER params[16] = {};
    params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_PIXEL;
    // params[1]: t9 light StructuredBuffer (was the old Lights CBV b1 slot, now repurposed)
    D3D12_DESCRIPTOR_RANGE lightRange = {};
    lightRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    lightRange.NumDescriptors = 1; lightRange.BaseShaderRegister = 9;
    lightRange.OffsetInDescriptorsFromTableStart = 0;
    params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges   = &lightRange;
    params[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
    params[6].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[6].Descriptor.ShaderRegister = 2;
    params[6].ShaderVisibility          = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_DESCRIPTOR_RANGE ranges[8] = {};
    // params[2..5]: t0-t3 (GBuffer RTs + Depth)
    for (uint32_t i = 0; i < 4; ++i) {
        ranges[i].RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[i].NumDescriptors                    = 1;
        ranges[i].BaseShaderRegister                = i;
        ranges[i].RegisterSpace                     = 0;
        ranges[i].OffsetInDescriptorsFromTableStart = 0;
        params[2 + i].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[2 + i].DescriptorTable.NumDescriptorRanges = 1;
        params[2 + i].DescriptorTable.pDescriptorRanges   = &ranges[i];
        params[2 + i].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
    }
    // params[7]: t4 (Shadow map array)
    ranges[4].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[4].NumDescriptors = 1; ranges[4].BaseShaderRegister = 4;
    ranges[4].OffsetInDescriptorsFromTableStart = 0;
    params[7].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[7].DescriptorTable.NumDescriptorRanges = 1;
    params[7].DescriptorTable.pDescriptorRanges   = &ranges[4];
    params[7].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
    // params[8..10]: t5-t7 (IBL irradiance, prefiltered, BRDF LUT)
    for (uint32_t i = 0; i < 3; ++i) {
        ranges[5 + i].RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[5 + i].NumDescriptors                    = 1;
        ranges[5 + i].BaseShaderRegister                = 5 + i;
        ranges[5 + i].RegisterSpace                     = 0;
        ranges[5 + i].OffsetInDescriptorsFromTableStart = 0;
        params[8 + i].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[8 + i].DescriptorTable.NumDescriptorRanges = 1;
        params[8 + i].DescriptorTable.pDescriptorRanges   = &ranges[5 + i];
        params[8 + i].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
    }
    // params[11]: t8 (SSAO texture)
    D3D12_DESCRIPTOR_RANGE ssaoRange = {};
    ssaoRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ssaoRange.NumDescriptors = 1; ssaoRange.BaseShaderRegister = 8;
    ssaoRange.OffsetInDescriptorsFromTableStart = 0;
    params[11].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[11].DescriptorTable.NumDescriptorRanges = 1;
    params[11].DescriptorTable.pDescriptorRanges   = &ssaoRange;
    params[11].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
    // params[12]: t10 cluster light-index StructuredBuffer (root SRV by GPU VA)
    params[12].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[12].Descriptor.ShaderRegister = 10;
    params[12].ShaderVisibility          = D3D12_SHADER_VISIBILITY_PIXEL;
    // params[13]: t11 spot shadow atlas (Texture2DArray)
    D3D12_DESCRIPTOR_RANGE spotRange = {};
    spotRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    spotRange.NumDescriptors = 1; spotRange.BaseShaderRegister = 11;
    spotRange.OffsetInDescriptorsFromTableStart = 0;
    params[13].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[13].DescriptorTable.NumDescriptorRanges = 1;
    params[13].DescriptorTable.pDescriptorRanges   = &spotRange;
    params[13].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
    // params[14]: b3 SpotShadow CB (SpotVP[MAX] + count)
    params[14].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[14].Descriptor.ShaderRegister = 3;
    params[14].ShaderVisibility          = D3D12_SHADER_VISIBILITY_PIXEL;
    // params[15]: t12 point shadow cube atlas (TextureCubeArray)
    D3D12_DESCRIPTOR_RANGE pointRange = {};
    pointRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    pointRange.NumDescriptors = 1; pointRange.BaseShaderRegister = 12;
    pointRange.OffsetInDescriptorsFromTableStart = 0;
    params[15].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[15].DescriptorTable.NumDescriptorRanges = 1;
    params[15].DescriptorTable.pDescriptorRanges   = &pointRange;
    params[15].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC samplers[4] = {};
    // s0: point clamp for GBuffer
    samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    samplers[0].AddressU = samplers[0].AddressV = samplers[0].AddressW =
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[0].ShaderRegister = 0; samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    // s1: comparison LESS_EQUAL for PCF shadow
    samplers[1].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    samplers[1].AddressU = samplers[1].AddressV = samplers[1].AddressW =
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    samplers[1].ShaderRegister = 1; samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    // s2: linear mip clamp for IBL env (prefiltered + irradiance)
    samplers[2].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[2].AddressU = samplers[2].AddressV = samplers[2].AddressW =
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[2].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[2].ShaderRegister = 2; samplers[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    // s3: bilinear clamp for BRDF LUT (no mipmaps)
    samplers[3].Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    samplers[3].AddressU = samplers[3].AddressV = samplers[3].AddressW =
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[3].MaxLOD = 0;
    samplers[3].ShaderRegister = 3; samplers[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters     = 16;
    rsDesc.pParameters       = params;
    rsDesc.NumStaticSamplers = 4;
    rsDesc.pStaticSamplers   = samplers;
    rsDesc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> rsBlob, rsErr;
    D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr);
    return SUCCEEDED(device->CreateRootSignature(0, rsBlob->GetBufferPointer(),
                                                  rsBlob->GetBufferSize(),
                                                  IID_PPV_ARGS(&m_rootSignature)));
}

bool LightingPass::CreatePipelineState(ID3D12Device* device) {
    auto vs = LoadOrCompileShader(L"Resource/Shaders/LightingPass.VS.hlsl", L"vs_6_0");
    auto ps = LoadOrCompileShader(L"Resource/Shaders/LightingPass.PS.hlsl", L"ps_6_0");

    D3D12_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_RASTERIZER_DESC rastDesc = {};
    rastDesc.FillMode = D3D12_FILL_MODE_SOLID;
    rastDesc.CullMode = D3D12_CULL_MODE_NONE;

    D3D12_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable = FALSE;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature        = m_rootSignature.Get();
    psoDesc.VS                    = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS                    = { ps->GetBufferPointer(), ps->GetBufferSize() };
    psoDesc.InputLayout           = { nullptr, 0 };
    psoDesc.RasterizerState       = rastDesc;
    psoDesc.BlendState            = blendDesc;
    psoDesc.DepthStencilState     = dsDesc;
    psoDesc.SampleMask            = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets      = 1;
    psoDesc.RTVFormats[0]         = DXGI_FORMAT_R16G16B16A16_FLOAT;
    psoDesc.SampleDesc            = { 1, 0 };

    return SUCCEEDED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pso)));
}

bool LightingPass::CreateConstantBuffers(GraphicsDevice& gfx) {
    ID3D12Device* device = gfx.GetDevice();
    for (uint32_t i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i) {
        m_cameraCB[i]     = CreateUploadCB(device, CAMERA_CB_SIZE, &m_cameraMapped[i]);
        m_shadowCB[i]     = CreateUploadCB(device, SHADOW_CB_SIZE, &m_shadowMapped[i]);
        m_spotShadowCB[i] = CreateUploadCB(device, SPOT_CB_SIZE,   &m_spotShadowMapped[i]);
        if (!m_spotShadowCB[i]) return false;
        // Light StructuredBuffer (upload heap, mapped, rewritten each frame).
        m_lightsSB[i] = CreateUploadCB(device, (UINT64)GPU_LIGHT_STRIDE * MAX_LIGHTS, &m_lightsMapped[i]);
        if (!m_cameraCB[i] || !m_shadowCB[i] || !m_lightsSB[i]) return false;

        m_lightSRVSlot[i] = gfx.AllocateSRVSlot();
        D3D12_SHADER_RESOURCE_VIEW_DESC srvd = {};
        srvd.Format                     = DXGI_FORMAT_UNKNOWN;
        srvd.ViewDimension              = D3D12_SRV_DIMENSION_BUFFER;
        srvd.Shader4ComponentMapping    = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvd.Buffer.FirstElement        = 0;
        srvd.Buffer.NumElements         = MAX_LIGHTS;
        srvd.Buffer.StructureByteStride = GPU_LIGHT_STRIDE;
        srvd.Buffer.Flags               = D3D12_BUFFER_SRV_FLAG_NONE;
        device->CreateShaderResourceView(m_lightsSB[i].Get(), &srvd, gfx.GetSRVCPUHandle(m_lightSRVSlot[i]));

        // Per-cluster light index list (StructuredBuffer<uint>), filled on the CPU each frame and
        // bound to the PS as a root SRV (no descriptor needed).
        m_clusterSB[i] = CreateUploadCB(device, (UINT64)CLUSTER_ELEMS * sizeof(uint32_t), &m_clusterMapped[i]);
        if (!m_clusterSB[i]) return false;
    }
    return true;
}

// CPU clustered light culling. For each screen tile it unprojects the 4 corners at the near plane
// (NDC z=0) and far plane (NDC z=1) with InvViewProj, then for each LOGARITHMIC view-Z slice it
// lerps those corner rays by t=(viewZ-near)/(far-near) (view-Z is linear in world along a ray) to
// get the slice's 8 world corners and their AABB. Point lights whose sphere overlaps the AABB are
// assigned; directional lights go in every cluster. Using only z∈{0,1} keeps it independent of the
// projection's exact NDC-z convention; log slices spread depth so clusters stay small (no cap drop).
void LightingPass::CullLightsToClusters(uint32_t fi, const Matrix4x4& invViewProj,
                                        float nearZ, float farZ,
                                        const LightCullItem* lights, uint32_t lightCount) {
    uint32_t* out = reinterpret_cast<uint32_t*>(m_clusterMapped[fi]);
    auto fmin = [](float a, float b) { return a < b ? a : b; };
    auto fmax = [](float a, float b) { return a > b ? a : b; };

    auto toWorld = [&](float u, float v, float z) -> Vector3 {
        float ndcx = u * 2.0f - 1.0f;
        float ndcy = 1.0f - v * 2.0f;
        Vector4 w  = invViewProj * Vector4(ndcx, ndcy, z, 1.0f);
        float   iw = (std::fabs(w.w) > 1e-8f) ? 1.0f / w.w : 0.0f;
        return Vector3(w.x * iw, w.y * iw, w.z * iw);
    };

    const float ratio = farZ / nearZ;
    const float invRange = (farZ - nearZ > 1e-6f) ? 1.0f / (farZ - nearZ) : 0.0f;

    for (uint32_t cy = 0; cy < CLUSTER_Y; ++cy)
    for (uint32_t cx = 0; cx < CLUSTER_X; ++cx) {
        const float us[4] = { (float)cx / CLUSTER_X, (float)(cx + 1) / CLUSTER_X,
                              (float)(cx + 1) / CLUSTER_X, (float)cx / CLUSTER_X };
        const float vs[4] = { (float)cy / CLUSTER_Y, (float)cy / CLUSTER_Y,
                              (float)(cy + 1) / CLUSTER_Y, (float)(cy + 1) / CLUSTER_Y };
        Vector3 nc[4], fc[4];
        for (int i = 0; i < 4; ++i) { nc[i] = toWorld(us[i], vs[i], 0.0f); fc[i] = toWorld(us[i], vs[i], 1.0f); }

        for (uint32_t cz = 0; cz < CLUSTER_Z; ++cz) {
            float zN = nearZ * std::pow(ratio, (float)cz       / CLUSTER_Z);
            float zF = nearZ * std::pow(ratio, (float)(cz + 1) / CLUSTER_Z);
            float tN = (zN - nearZ) * invRange, tF = (zF - nearZ) * invRange;

            Vector3 mn( FLT_MAX,  FLT_MAX,  FLT_MAX);
            Vector3 mx(-FLT_MAX, -FLT_MAX, -FLT_MAX);
            for (int i = 0; i < 4; ++i) {
                Vector3 d  = fc[i] - nc[i];
                Vector3 a  = nc[i] + d * tN;
                Vector3 b  = nc[i] + d * tF;
                mn.x = fmin(mn.x, fmin(a.x, b.x)); mn.y = fmin(mn.y, fmin(a.y, b.y)); mn.z = fmin(mn.z, fmin(a.z, b.z));
                mx.x = fmax(mx.x, fmax(a.x, b.x)); mx.y = fmax(mx.y, fmax(a.y, b.y)); mx.z = fmax(mx.z, fmax(a.z, b.z));
            }

            uint32_t cluster = (cz * CLUSTER_Y + cy) * CLUSTER_X + cx;
            uint32_t base    = cluster * CLUSTER_STRIDE;
            uint32_t n       = 0;
            for (uint32_t i = 0; i < lightCount && n < MAX_PER_CLUSTER; ++i) {
                const LightCullItem& L = lights[i];
                bool add;
                if (L.directional) {
                    add = true;
                } else {
                    float px = fmax(mn.x, fmin(L.pos.x, mx.x));
                    float py = fmax(mn.y, fmin(L.pos.y, mx.y));
                    float pz = fmax(mn.z, fmin(L.pos.z, mx.z));
                    float dx = L.pos.x - px, dy = L.pos.y - py, dz = L.pos.z - pz;
                    add = (dx * dx + dy * dy + dz * dz) <= L.range * L.range;
                }
                if (add) out[base + 1 + n++] = i;
            }
            out[base] = n;
        }
    }
}

void LightingPass::PrepareSpotShadows(const SceneManager& scene, const Vector3& cameraPos,
                                      const std::vector<ShadowCaster>& casters) {
    struct Cand { uint64_t actorId; Vector3 pos, dir; float range, angle; float dist; Matrix4x4 vp; };
    std::vector<Cand> cands;
    for (auto& actorPtr : scene.GetActors()) {
        Actor* actor = actorPtr.get();
        auto* lc = actor->GetComponent<LightComponent>();
        if (!lc || lc->Type != LightType::Spot || !lc->CastShadows) continue;
        auto* tc = actor->GetComponent<TransformComponent>();
        if (!tc) continue;
        const Transform& w = tc->CachedWorld;
        Matrix4x4 rotMat = w.Rotation.ToMatrix();
        Vector3 dir(rotMat.m[2][0], rotMat.m[2][1], rotMat.m[2][2]);
        Vector3 d  = w.Position - cameraPos;
        float   dist = d.x * d.x + d.y * d.y + d.z * d.z;
        cands.push_back({ actor->GetId(), w.Position, dir, lc->Range, lc->SpotAngle, dist,
                          ShadowPass::ComputeSpotMatrix(w.Position, dir, lc->SpotAngle, lc->Range) });
    }
    // Keep the nearest MAX, then order that set by actorId so slot assignment is stable across frames
    // (a stable slot is what makes the shadow cache hit).
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.dist < b.dist; });
    uint32_t n = (uint32_t)cands.size();
    if (n > SpotShadowData::MAX) n = SpotShadowData::MAX;
    std::sort(cands.begin(), cands.begin() + n,
              [](const Cand& a, const Cand& b) { return a.actorId < b.actorId; });

    m_spotData.Count = n;
    for (uint32_t i = 0; i < n; ++i) {
        const Cand& c = cands[i];
        Plane planes[6];
        ExtractFrustumPlanes(c.vp, planes);   // same cull the spot shadow pass uses
        bool dyn = false;
        uint64_t sig = ShadowCacheSig(c.actorId, c.pos, c.dir, c.range, c.angle, casters, planes, dyn);
        bool dirty = dyn || (sig != m_spotSig[i]);
        m_spotSig[i]              = sig;
        m_spotData.ViewProj[i]    = c.vp;
        m_spotData.NeedsRender[i] = dirty;
        m_spotActorId[i]          = c.actorId;
    }
    for (uint32_t i = n; i < SpotShadowData::MAX; ++i) {
        m_spotActorId[i] = 0; m_spotData.NeedsRender[i] = false; m_spotSig[i] = 0;
    }
}

void LightingPass::PreparePointShadows(const SceneManager& scene, const Vector3& cameraPos,
                                       const std::vector<ShadowCaster>& casters) {
    struct Cand { uint64_t actorId; Vector3 pos; float range; float dist; };
    std::vector<Cand> cands;
    for (auto& actorPtr : scene.GetActors()) {
        Actor* actor = actorPtr.get();
        auto* lc = actor->GetComponent<LightComponent>();
        if (!lc || lc->Type != LightType::Point || !lc->CastShadows) continue;
        auto* tc = actor->GetComponent<TransformComponent>();
        if (!tc) continue;
        const Vector3 wpos = tc->CachedWorld.Position;
        Vector3 d  = wpos - cameraPos;
        float   dist = d.x * d.x + d.y * d.y + d.z * d.z;
        cands.push_back({ actor->GetId(), wpos, lc->Range, dist });
    }
    // Nearest MAX, then stable actorId order for slot caching.
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.dist < b.dist; });
    uint32_t n = (uint32_t)cands.size();
    if (n > PointShadowData::MAX) n = PointShadowData::MAX;
    std::sort(cands.begin(), cands.begin() + n,
              [](const Cand& a, const Cand& b) { return a.actorId < b.actorId; });

    m_pointData.Count = n;
    const Vector3 noDir(0, 0, 0);
    for (uint32_t i = 0; i < n; ++i) {
        const Cand& c = cands[i];
        bool dyn = false;
        uint64_t sig = ShadowCacheSig(c.actorId, c.pos, noDir, c.range, 0.0f, casters, nullptr, dyn);
        bool dirty = dyn || (sig != m_pointSig[i]);
        m_pointSig[i]              = sig;
        m_pointData.Pos[i]         = c.pos;
        m_pointData.Range[i]       = c.range;
        m_pointData.NeedsRender[i] = dirty;
        m_pointActorId[i]          = c.actorId;
    }
    for (uint32_t i = n; i < PointShadowData::MAX; ++i) {
        m_pointActorId[i] = 0; m_pointData.NeedsRender[i] = false; m_pointSig[i] = 0;
    }
}

void LightingPass::Execute(ID3D12GraphicsCommandList* cmd,
                            GraphicsDevice& gfx,
                            const GBuffer& gbuffer,
                            const SceneManager& scene,
                            uint32_t frameIndex,
                            const Matrix4x4& invViewProj,
                            const Vector3& cameraPos,
                            const Vector3& cameraForward,
                            const ShadowData& shadowData,
                            uint32_t shadowSRVSlot,
                            uint32_t iblIrrSlot,
                            uint32_t iblPrefSlot,
                            uint32_t iblBRDFSlot,
                            D3D12_CPU_DESCRIPTOR_HANDLE targetRTV,
                            uint32_t ssaoSRVSlot,
                            float    ssaoStrength,
                            uint32_t scissorX, uint32_t scissorY,
                            uint32_t scissorW, uint32_t scissorH,
                            float nearZ, float farZ,
                            uint32_t spotShadowSRVSlot,
                            uint32_t pointShadowSRVSlot) {
    uint32_t fi = frameIndex % NUM_FRAMES_IN_FLIGHT;

    // Camera CB layout:
    //   [  0] InvViewProj     64B
    //   [ 64] CameraPos       12B + pad 4B
    //   [ 80] CameraForward   12B + pad 4B
    //   [ 96] VpOffX/Y/SclX/Y 16B
    //   [112] SSAOStrength     4B  + pad 12B
    memcpy(m_cameraMapped[fi],      invViewProj.v,   64);
    memcpy(m_cameraMapped[fi] + 64, &cameraPos,      12);
    memcpy(m_cameraMapped[fi] + 80, &cameraForward,  12);
    float vpParams[4] = {
        (scissorW > 0) ? static_cast<float>(scissorX) / static_cast<float>(gfx.GetWidth())  : 0.0f,
        (scissorH > 0) ? static_cast<float>(scissorY) / static_cast<float>(gfx.GetHeight()) : 0.0f,
        (scissorW > 0) ? static_cast<float>(scissorW) / static_cast<float>(gfx.GetWidth())  : 1.0f,
        (scissorH > 0) ? static_cast<float>(scissorH) / static_cast<float>(gfx.GetHeight()) : 1.0f,
    };
    memcpy(m_cameraMapped[fi] + 96,  vpParams,    16);
    memcpy(m_cameraMapped[fi] + 112, &ssaoStrength, 4);

    // Lights → StructuredBuffer (written in place, no fixed cap beyond MAX_LIGHTS).
    uint32_t lightCount = 0;
    uint8_t* sb = m_lightsMapped[fi];
    std::vector<LightCullItem> cullItems;
    cullItems.reserve(64);
    for (auto& actorPtr : scene.GetActors()) {
        if (lightCount >= MAX_LIGHTS) break;
        Actor* actor = actorPtr.get();
        auto* lc = actor->GetComponent<LightComponent>();
        if (!lc) continue;
        auto* tc = actor->GetComponent<TransformComponent>();
        GpuLight gl = {};

        Vector3 worldPos{};
        if (tc) {
            const Transform& w = tc->CachedWorld;   // parent-resolved world transform
            worldPos = w.Position;
            gl.pos[0] = w.Position.x; gl.pos[1] = w.Position.y; gl.pos[2] = w.Position.z;
            Matrix4x4 rotMat = w.Rotation.ToMatrix();
            gl.dir[0] = rotMat.m[2][0]; gl.dir[1] = rotMat.m[2][1]; gl.dir[2] = rotMat.m[2][2];
        }
        gl.type      = static_cast<float>(lc->Type);
        gl.range     = lc->Range;
        gl.color[0]  = lc->Color.x; gl.color[1] = lc->Color.y; gl.color[2] = lc->Color.z;
        gl.intensity = lc->Intensity;
        gl.spotAngle = lc->SpotAngle;
        // Map this light to a shadow slice (matches Prepare*Shadows selection), else -1.
        // For spots it indexes the spot atlas; for points it indexes the point cube atlas.
        gl.shadowIndex = -1.0f;
        if (lc->Type == LightType::Spot) {
            for (uint32_t s = 0; s < m_spotData.Count; ++s)
                if (m_spotActorId[s] == actor->GetId()) { gl.shadowIndex = static_cast<float>(s); break; }
        } else if (lc->Type == LightType::Point) {
            for (uint32_t s = 0; s < m_pointData.Count; ++s)
                if (m_pointActorId[s] == actor->GetId()) { gl.shadowIndex = static_cast<float>(s); break; }
        }
        memcpy(sb + (size_t)lightCount * GPU_LIGHT_STRIDE, &gl, GPU_LIGHT_STRIDE);

        cullItems.push_back({ worldPos, lc->Range, gl.type < 0.5f /* directional */ });
        ++lightCount;
    }
    memcpy(m_cameraMapped[fi] + 116, &lightCount, 4);   // LightCount lives in the Camera CB now

    // Cluster grid dims (read by the PS) + CPU light culling into the cluster buffer.
    uint32_t clusterDims[4] = { CLUSTER_X, CLUSTER_Y, CLUSTER_Z, MAX_PER_CLUSTER };
    memcpy(m_cameraMapped[fi] + 120, clusterDims, 16);   // ClusterGX/GY/GZ/MaxPerCluster
    float clusterNF[2] = { nearZ, farZ };
    memcpy(m_cameraMapped[fi] + 136, clusterNF, 8);      // ClusterNear/ClusterFar
    CullLightsToClusters(fi, invViewProj, nearZ, farZ, cullItems.data(), lightCount);

    // Shadow CB: [LightViewProj[4](256B)][CascadeSplits(16B)]
    for (uint32_t c = 0; c < ShadowPass::CASCADE_COUNT; ++c)
        memcpy(m_shadowMapped[fi] + c * 64, shadowData.LightViewProj[c].v, 64);
    memcpy(m_shadowMapped[fi] + 256, shadowData.CascadeSplits, 16);

    // Spot shadow CB (b3): [SpotVP[MAX](MAX*64B)][SpotCount(4B)]
    for (uint32_t s = 0; s < SpotShadowData::MAX; ++s)
        memcpy(m_spotShadowMapped[fi] + s * 64, m_spotData.ViewProj[s].v, 64);
    uint32_t spotCount = m_spotData.Count;
    memcpy(m_spotShadowMapped[fi] + SpotShadowData::MAX * 64, &spotCount, 4);

    // Bind HDR render target
    cmd->OMSetRenderTargets(1, &targetRTV, FALSE, nullptr);

    D3D12_VIEWPORT vp = { 0, 0, static_cast<float>(gfx.GetWidth()),
                          static_cast<float>(gfx.GetHeight()), 0, 1 };
    // If a central viewport rect is provided, scissor to it; otherwise full screen.
    D3D12_RECT scissor;
    if (scissorW > 0 && scissorH > 0) {
        scissor = { static_cast<LONG>(scissorX), static_cast<LONG>(scissorY),
                    static_cast<LONG>(scissorX + scissorW), static_cast<LONG>(scissorY + scissorH) };
    } else {
        scissor = { 0, 0, static_cast<LONG>(gfx.GetWidth()), static_cast<LONG>(gfx.GetHeight()) };
    }
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &scissor);
    cmd->SetPipelineState(m_pso.Get());
    cmd->SetGraphicsRootSignature(m_rootSignature.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    cmd->SetGraphicsRootConstantBufferView(0, m_cameraCB[fi]->GetGPUVirtualAddress());
    cmd->SetGraphicsRootDescriptorTable(1, gfx.GetSRVGPUHandle(m_lightSRVSlot[fi]));   // t9 light SB
    cmd->SetGraphicsRootConstantBufferView(6, m_shadowCB[fi]->GetGPUVirtualAddress());

    for (uint32_t i = 0; i < GBuffer::RT_COUNT; ++i)
        cmd->SetGraphicsRootDescriptorTable(2 + i, gfx.GetSRVGPUHandle(gbuffer.GetSRVSlot(i)));
    cmd->SetGraphicsRootDescriptorTable(5, gfx.GetSRVGPUHandle(gfx.GetDepthSRVSlot()));
    cmd->SetGraphicsRootDescriptorTable(7, gfx.GetSRVGPUHandle(shadowSRVSlot));
    cmd->SetGraphicsRootDescriptorTable(8,  gfx.GetSRVGPUHandle(iblIrrSlot));
    cmd->SetGraphicsRootDescriptorTable(9,  gfx.GetSRVGPUHandle(iblPrefSlot));
    cmd->SetGraphicsRootDescriptorTable(10, gfx.GetSRVGPUHandle(iblBRDFSlot));
    // SSAO: use slot 0 (no occlusion) if not provided
    uint32_t effectiveSSAO = (ssaoSRVSlot != 0) ? ssaoSRVSlot : gfx.GetDepthSRVSlot();
    cmd->SetGraphicsRootDescriptorTable(11, gfx.GetSRVGPUHandle(effectiveSSAO));
    cmd->SetGraphicsRootShaderResourceView(12, m_clusterSB[fi]->GetGPUVirtualAddress());  // t10 cluster lists
    cmd->SetGraphicsRootDescriptorTable(13, gfx.GetSRVGPUHandle(spotShadowSRVSlot));       // t11 spot shadow atlas
    cmd->SetGraphicsRootConstantBufferView(14, m_spotShadowCB[fi]->GetGPUVirtualAddress()); // b3 SpotShadow CB
    cmd->SetGraphicsRootDescriptorTable(15, gfx.GetSRVGPUHandle(pointShadowSRVSlot));       // t12 point shadow cube atlas

    cmd->DrawInstanced(3, 1, 0, 0);
}

void LightingPass::Shutdown() {
    for (uint32_t i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i) {
        if (m_cameraMapped[i]) { m_cameraCB[i]->Unmap(0, nullptr); m_cameraMapped[i] = nullptr; }
        if (m_lightsMapped[i]) { m_lightsSB[i]->Unmap(0, nullptr); m_lightsMapped[i] = nullptr; }
        if (m_shadowMapped[i]) { m_shadowCB[i]->Unmap(0, nullptr); m_shadowMapped[i] = nullptr; }
        if (m_spotShadowMapped[i]) { m_spotShadowCB[i]->Unmap(0, nullptr); m_spotShadowMapped[i] = nullptr; }
        if (m_clusterMapped[i]) { m_clusterSB[i]->Unmap(0, nullptr); m_clusterMapped[i] = nullptr; }
        m_cameraCB[i].Reset();
        m_lightsSB[i].Reset();
        m_clusterSB[i].Reset();
        m_shadowCB[i].Reset();
        m_spotShadowCB[i].Reset();
    }
    m_pso.Reset();
    m_rootSignature.Reset();
}

} // namespace Fujin
