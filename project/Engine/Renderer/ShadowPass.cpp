#include "ShadowPass.h"
#include "Material/MaterialManager.h"
#include "Engine/Core/SceneManager.h"
#include "Engine/Core/Actor.h"
#include "Engine/Core/TransformComponent.h"
#include "Engine/Core/MeshComponent.h"
#include "Engine/Core/AnimationComponent.h"
#include "Engine/Asset/GeometryManager.h"
#include "Engine/Asset/SkeletalMeshAsset.h"
#include "Engine/Animation/AnimationTypes.h"
#include "Engine/Core/LightComponent.h"
#include "Engine/Spatial/Bvh.h"
#include "Engine/Graphics/DxcHelper.h"
#include <algorithm>
#include <cmath>
#include <cfloat>
#include <cctype>
#include <string>
#include <vector>

namespace Fujin {

// World-space AABB of a mesh's local bounds (matches SceneRenderer::TransformBounds).
static Aabb TransformBoundsToWorld(const float mn[3], const float mx[3], const Matrix4x4& m) {
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

static Matrix4x4 OrthoProjection(float l, float r, float b, float t, float n, float f) {
    Matrix4x4 result;
    memset(result.v, 0, sizeof(result.v));
    result.m[0][0] = 2.0f / (r - l);
    result.m[1][1] = 2.0f / (t - b);
    result.m[2][2] = 1.0f / (f - n);
    result.m[0][3] = -(r + l) / (r - l);
    result.m[1][3] = -(t + b) / (t - b);
    result.m[2][3] = -n / (f - n);
    result.m[3][3] = 1.0f;
    return result;
}

bool ShadowPass::Initialize(GraphicsDevice& gfx) {
    try {
        if (!CreateRootSignature(gfx.GetDevice()))            return false;
        if (!CreateAlphaClipRootSignature(gfx.GetDevice()))   return false;
        if (!CreatePipelineState(gfx.GetDevice()))            return false;
        if (!CreateConstantBuffers(gfx.GetDevice()))          return false;
        if (!CreateShadowMap(gfx))                            return false;
        if (!CreateSkinnedRootSignature(gfx.GetDevice()))     return false;
        if (!CreateSkinnedPipelineState(gfx.GetDevice()))     return false;
        if (!CreateSkinnedShadowBuffers(gfx.GetDevice()))     return false;
        if (!CreateSpotShadowMap(gfx))                        return false;
        if (!CreateSpotConstantBuffers(gfx.GetDevice()))      return false;
        if (!CreatePointShadowMap(gfx))                       return false;
        if (!CreatePointConstantBuffers(gfx.GetDevice()))     return false;
    } catch (...) {
        return false;
    }
    return true;
}

bool ShadowPass::CreateRootSignature(ID3D12Device* device) {
    D3D12_ROOT_PARAMETER param = {};
    param.ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    param.Descriptor.ShaderRegister = 0;
    param.Descriptor.RegisterSpace  = 0;
    param.ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters = 1;
    rsDesc.pParameters   = &param;
    rsDesc.Flags         = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> rsBlob, rsErr;
    D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr);
    return SUCCEEDED(device->CreateRootSignature(0, rsBlob->GetBufferPointer(),
                                                  rsBlob->GetBufferSize(),
                                                  IID_PPV_ARGS(&m_rootSignature)));
}

bool ShadowPass::CreateAlphaClipRootSignature(ID3D12Device* device) {
    D3D12_ROOT_PARAMETER params[2] = {};

    // b0: LightWVP (VS)
    params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].Descriptor.RegisterSpace  = 0;
    params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;

    // t0: albedo texture (PS)
    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors                    = 1;
    range.BaseShaderRegister                = 0;
    range.RegisterSpace                     = 0;
    range.OffsetInDescriptorsFromTableStart = 0;
    params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges   = &range;
    params[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter         = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.MaxLOD         = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters     = 2;
    rsDesc.pParameters       = params;
    rsDesc.NumStaticSamplers = 1;
    rsDesc.pStaticSamplers   = &sampler;
    rsDesc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> rsBlob, rsErr;
    D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr);
    return SUCCEEDED(device->CreateRootSignature(0, rsBlob->GetBufferPointer(),
                                                  rsBlob->GetBufferSize(),
                                                  IID_PPV_ARGS(&m_rootSignatureAlphaClip)));
}

bool ShadowPass::CreatePipelineState(ID3D12Device* device) {
    // --- Shared state for all static shadow PSO variants ---
    D3D12_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].RenderTargetWriteMask = 0;

    D3D12_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable    = TRUE;
    dsDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    dsDesc.DepthFunc      = D3D12_COMPARISON_FUNC_LESS;

    // --- Variant [0]: CullBack, no alpha clip (position-only VS, no PS) ---
    {
        auto vs = LoadOrCompileShader(L"Resource/Shaders/ShadowPass.VS.hlsl", L"vs_6_0");

        D3D12_INPUT_ELEMENT_DESC il[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };
        D3D12_RASTERIZER_DESC rast = {};
        rast.FillMode = D3D12_FILL_MODE_SOLID; rast.CullMode = D3D12_CULL_MODE_BACK;
        rast.DepthClipEnable = TRUE; rast.DepthBias = 1000; rast.SlopeScaledDepthBias = 1.0f;

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
        pso.pRootSignature = m_rootSignature.Get();
        pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
        pso.PS = { nullptr, 0 };
        pso.InputLayout = { il, 1 };
        pso.RasterizerState = rast; pso.BlendState = blendDesc; pso.DepthStencilState = dsDesc;
        pso.DSVFormat = DXGI_FORMAT_D32_FLOAT; pso.SampleMask = UINT_MAX;
        pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso.NumRenderTargets = 0; pso.SampleDesc = { 1, 0 };
        if (FAILED(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_psoVariants[0])))) return false;
    }

    // --- Variant [2]: CullNone, no alpha clip ---
    {
        auto vs = LoadOrCompileShader(L"Resource/Shaders/ShadowPass.VS.hlsl", L"vs_6_0");

        D3D12_INPUT_ELEMENT_DESC il[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };
        D3D12_RASTERIZER_DESC rast = {};
        rast.FillMode = D3D12_FILL_MODE_SOLID; rast.CullMode = D3D12_CULL_MODE_NONE;
        rast.DepthClipEnable = TRUE; rast.DepthBias = 1000; rast.SlopeScaledDepthBias = 1.0f;

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
        pso.pRootSignature = m_rootSignature.Get();
        pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
        pso.PS = { nullptr, 0 };
        pso.InputLayout = { il, 1 };
        pso.RasterizerState = rast; pso.BlendState = blendDesc; pso.DepthStencilState = dsDesc;
        pso.DSVFormat = DXGI_FORMAT_D32_FLOAT; pso.SampleMask = UINT_MAX;
        pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso.NumRenderTargets = 0; pso.SampleDesc = { 1, 0 };
        if (FAILED(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_psoVariants[2])))) return false;
    }

    // --- Variant [1]: CullBack, alpha clip (position+uv VS, alpha-test PS) ---
    {
        auto vs = LoadOrCompileShader(L"Resource/Shaders/ShadowPass.AlphaClip.VS.hlsl", L"vs_6_0");
        auto ps = LoadOrCompileShader(L"Resource/Shaders/ShadowPass.AlphaClip.PS.hlsl", L"ps_6_0");

        D3D12_INPUT_ELEMENT_DESC il[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };
        D3D12_RASTERIZER_DESC rast = {};
        rast.FillMode = D3D12_FILL_MODE_SOLID; rast.CullMode = D3D12_CULL_MODE_BACK;
        rast.DepthClipEnable = TRUE; rast.DepthBias = 1000; rast.SlopeScaledDepthBias = 1.0f;

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
        pso.pRootSignature = m_rootSignatureAlphaClip.Get();
        pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
        pso.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
        pso.InputLayout = { il, 2 };
        pso.RasterizerState = rast; pso.BlendState = blendDesc; pso.DepthStencilState = dsDesc;
        pso.DSVFormat = DXGI_FORMAT_D32_FLOAT; pso.SampleMask = UINT_MAX;
        pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso.NumRenderTargets = 0; pso.SampleDesc = { 1, 0 };
        if (FAILED(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_psoVariants[1])))) return false;
    }

    // --- Variant [3]: CullNone, alpha clip ---
    {
        auto vs = LoadOrCompileShader(L"Resource/Shaders/ShadowPass.AlphaClip.VS.hlsl", L"vs_6_0");
        auto ps = LoadOrCompileShader(L"Resource/Shaders/ShadowPass.AlphaClip.PS.hlsl", L"ps_6_0");

        D3D12_INPUT_ELEMENT_DESC il[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };
        D3D12_RASTERIZER_DESC rast = {};
        rast.FillMode = D3D12_FILL_MODE_SOLID; rast.CullMode = D3D12_CULL_MODE_NONE;
        rast.DepthClipEnable = TRUE; rast.DepthBias = 1000; rast.SlopeScaledDepthBias = 1.0f;

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
        pso.pRootSignature = m_rootSignatureAlphaClip.Get();
        pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
        pso.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
        pso.InputLayout = { il, 2 };
        pso.RasterizerState = rast; pso.BlendState = blendDesc; pso.DepthStencilState = dsDesc;
        pso.DSVFormat = DXGI_FORMAT_D32_FLOAT; pso.SampleMask = UINT_MAX;
        pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso.NumRenderTargets = 0; pso.SampleDesc = { 1, 0 };
        if (FAILED(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_psoVariants[3])))) return false;
    }

    return true;
}

bool ShadowPass::CreateConstantBuffers(ID3D12Device* device) {
    const UINT64 totalSize = (UINT64)CASCADE_COUNT * MAX_OBJECTS * CB_SLOT_SIZE;
    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width            = totalSize;
    rd.Height           = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels        = 1;
    rd.Format           = DXGI_FORMAT_UNKNOWN;
    rd.SampleDesc.Count = 1;
    rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_RANGE range = { 0, 0 };
    for (uint32_t i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i) {
        if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_cbuffer[i]))))
            return false;
        m_cbuffer[i]->Map(0, &range, reinterpret_cast<void**>(&m_cbMapped[i]));
    }
    return true;
}

bool ShadowPass::CreateShadowMap(GraphicsDevice& gfx) {
    ID3D12Device* device = gfx.GetDevice();

    // Texture2DArray: CASCADE_COUNT slices, R32_TYPELESS
    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width            = SHADOW_MAP_SIZE;
    rd.Height           = SHADOW_MAP_SIZE;
    rd.DepthOrArraySize = CASCADE_COUNT;
    rd.MipLevels        = 1;
    rd.Format           = DXGI_FORMAT_R32_TYPELESS;
    rd.SampleDesc.Count = 1;
    rd.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rd.Flags            = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearVal = {};
    clearVal.Format               = DXGI_FORMAT_D32_FLOAT;
    clearVal.DepthStencil.Depth   = 1.0f;
    clearVal.DepthStencil.Stencil = 0;

    if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearVal, IID_PPV_ARGS(&m_shadowMap))))
        return false;

    // DSV heap for CASCADE_COUNT slices
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.NumDescriptors = CASCADE_COUNT;
    if (FAILED(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap))))
        return false;

    uint32_t dsvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    D3D12_CPU_DESCRIPTOR_HANDLE dsvBase = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
    for (uint32_t c = 0; c < CASCADE_COUNT; ++c) {
        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format                         = DXGI_FORMAT_D32_FLOAT;
        dsvDesc.ViewDimension                  = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
        dsvDesc.Texture2DArray.MipSlice        = 0;
        dsvDesc.Texture2DArray.FirstArraySlice = c;
        dsvDesc.Texture2DArray.ArraySize       = 1;
        m_dsvHandles[c].ptr = dsvBase.ptr + (SIZE_T)c * dsvSize;
        device->CreateDepthStencilView(m_shadowMap.Get(), &dsvDesc, m_dsvHandles[c]);
    }

    // SRV: Texture2DArray, R32_FLOAT, all 4 slices
    m_srvSlot = gfx.AllocateSRVSlot();
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                          = DXGI_FORMAT_R32_FLOAT;
    srvDesc.ViewDimension                   = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    srvDesc.Shader4ComponentMapping         = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2DArray.MostDetailedMip  = 0;
    srvDesc.Texture2DArray.MipLevels        = 1;
    srvDesc.Texture2DArray.FirstArraySlice  = 0;
    srvDesc.Texture2DArray.ArraySize        = CASCADE_COUNT;
    device->CreateShaderResourceView(m_shadowMap.Get(), &srvDesc, gfx.GetSRVCPUHandle(m_srvSlot));

    m_mapState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    return true;
}

void ShadowPass::ComputeCascades(const Matrix4x4& invViewProj,
                                  float nearZ, float farZ,
                                  const Vector3& lightDir,
                                  ShadowData& outData) {
    // Practical split scheme (λ = 0.75)
    constexpr float LAMBDA = 0.75f;
    float splits[CASCADE_COUNT + 1];
    splits[0] = nearZ;
    float ratio = farZ / nearZ;
    for (uint32_t i = 1; i < CASCADE_COUNT; ++i) {
        float p        = float(i) / float(CASCADE_COUNT);
        float logSplit = nearZ * powf(ratio, p);
        float uniSplit = nearZ + (farZ - nearZ) * p;
        splits[i] = LAMBDA * logSplit + (1.0f - LAMBDA) * uniSplit;
    }
    splits[CASCADE_COUNT] = farZ;

    for (uint32_t i = 0; i < CASCADE_COUNT; ++i)
        outData.CascadeSplits[i] = splits[i + 1];

    // Unproject 8 NDC corners to world space (DX12: z in [0,1])
    static const float ndcXY[4][2] = {{-1,-1},{1,-1},{-1,1},{1,1}};
    Vector3 worldCorners[8];
    for (int i = 0; i < 4; ++i) {
        Vector4 clipN(ndcXY[i][0], ndcXY[i][1], 0.0f, 1.0f);
        Vector4 clipF(ndcXY[i][0], ndcXY[i][1], 1.0f, 1.0f);
        Vector4 wn = invViewProj * clipN;
        Vector4 wf = invViewProj * clipF;
        worldCorners[i]   = Vector3(wn.x/wn.w, wn.y/wn.w, wn.z/wn.w);
        worldCorners[i+4] = Vector3(wf.x/wf.w, wf.y/wf.w, wf.z/wf.w);
    }

    Vector3 ld = lightDir.GetSafeNormal();
    Vector3 worldUp = fabsf(ld.y) < 0.99f ? Vector3(0,1,0) : Vector3(1,0,0);

    for (uint32_t c = 0; c < CASCADE_COUNT; ++c) {
        float tNear = (splits[c]   - nearZ) / (farZ - nearZ);
        float tFar  = (splits[c+1] - nearZ) / (farZ - nearZ);

        Vector3 cc[8];
        for (int i = 0; i < 4; ++i) {
            Vector3 d = worldCorners[i+4] - worldCorners[i];
            cc[i]   = worldCorners[i] + d * tNear;
            cc[i+4] = worldCorners[i] + d * tFar;
        }

        // Place light 500 units behind the cascade center
        Vector3 center(0,0,0);
        for (auto& cv : cc) center += cv;
        center /= 8.0f;
        Vector3 lightPos    = center - ld * 500.0f;
        Vector3 lightTarget = center;
        Matrix4x4 lightView = Matrix4x4::LookAt(lightPos, lightTarget, worldUp);

        // AABB of cascade corners in light view space
        float minX = FLT_MAX, maxX = -FLT_MAX;
        float minY = FLT_MAX, maxY = -FLT_MAX;
        float maxZ = -FLT_MAX;
        for (auto& cv : cc) {
            Vector4 lv = lightView * Vector4(cv, 1.0f);
            minX = (std::min)(minX, lv.x); maxX = (std::max)(maxX, lv.x);
            minY = (std::min)(minY, lv.y); maxY = (std::max)(maxY, lv.y);
            maxZ = (std::max)(maxZ, lv.z);
        }
        // minZ = 0: shadow camera near plane at light position, captures all casters
        float minZ = 0.0f;
        maxZ += 50.0f; // small padding for casters beyond the frustum

        Matrix4x4 lightProj = OrthoProjection(minX, maxX, minY, maxY, minZ, maxZ);
        outData.LightViewProj[c] = lightProj * lightView;
    }
}

void ShadowPass::Execute(ID3D12GraphicsCommandList* cmd,
                          GraphicsDevice& gfx,
                          const SceneManager& scene,
                          uint32_t frameIndex,
                          GeometryManager& geoMgr,
                          TextureManager& texMgr,
                          MaterialManager& matMgr,
                          const Matrix4x4& invViewProj,
                          float nearZ, float farZ,
                          const Vector3& lightDir,
                          ShadowData& outData) {
    ComputeCascades(invViewProj, nearZ, farZ, lightDir, outData);

    if (m_mapState != D3D12_RESOURCE_STATE_DEPTH_WRITE) {
        D3D12_RESOURCE_BARRIER barrier      = {};
        barrier.Type                        = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource        = m_shadowMap.Get();
        barrier.Transition.StateBefore      = m_mapState;
        barrier.Transition.StateAfter       = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        barrier.Transition.Subresource      = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1, &barrier);
        m_mapState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    }

    ExecuteGPU(cmd, scene, frameIndex, geoMgr, texMgr, matMgr, gfx, outData);

    D3D12_RESOURCE_BARRIER toSRV           = {};
    toSRV.Type                             = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toSRV.Transition.pResource             = m_shadowMap.Get();
    toSRV.Transition.StateBefore           = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    toSRV.Transition.StateAfter            = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    toSRV.Transition.Subresource           = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd->ResourceBarrier(1, &toSRV);
    m_mapState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
}

void ShadowPass::ExecuteGPU(ID3D12GraphicsCommandList* cmd,
                              const SceneManager& scene,
                              uint32_t frameIndex,
                              GeometryManager& geoMgr,
                              TextureManager& texMgr,
                              MaterialManager& matMgr,
                              GraphicsDevice& gfx,
                              const ShadowData& data) {
    uint32_t fi     = frameIndex % NUM_FRAMES_IN_FLIGHT;
    uint8_t* cbBase = m_cbMapped[fi];

    D3D12_VIEWPORT vp      = { 0, 0, float(SHADOW_MAP_SIZE), float(SHADOW_MAP_SIZE), 0, 1 };
    D3D12_RECT     scissor = { 0, 0, SHADOW_MAP_SIZE, SHADOW_MAP_SIZE };
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &scissor);

    // Pre-upload bone palettes once (shared across all cascades)
    static const Matrix4x4 s_identityPalette[MAX_BONES] = {};
    {
        uint32_t palSlot = 0;
        for (auto& actorPtr : scene.GetActors()) {
            if (palSlot >= MAX_SKINNED_OBJECTS) break;
            Actor* actor = actorPtr.get();
            auto*  mc    = actor->GetComponent<MeshComponent>();
            if (!mc || !geoMgr.IsSkeletal(mc->MeshPath)) continue;
            const SkeletalMeshAsset* mesh = geoMgr.LoadSkeletalMesh(mc->MeshPath);
            if (!mesh || mesh->SubMeshes.empty()) { ++palSlot; continue; }
            auto*            ac  = actor->GetComponent<AnimationComponent>();
            const Matrix4x4* pal = (ac && ac->PaletteReady) ? ac->BonePalette.data() : s_identityPalette;
            memcpy(m_bonePaletteMapped[fi] + palSlot * BONE_PALETTE_STRIDE, pal, BONE_PALETTE_STRIDE);
            ++palSlot;
        }
    }

    for (uint32_t c = 0; c < CASCADE_COUNT; ++c) {
        cmd->OMSetRenderTargets(0, nullptr, FALSE, &m_dsvHandles[c]);
        cmd->ClearDepthStencilView(m_dsvHandles[c], D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

        // --- Static shadow: non-alpha-clip actors (use m_rootSignature) ---
        cmd->SetGraphicsRootSignature(m_rootSignature.Get());
        ID3D12PipelineState* curPso = nullptr;

        uint32_t slotIdx = 0;
        for (auto& actorPtr : scene.GetActors()) {
            if (slotIdx >= MAX_OBJECTS) break;
            Actor* actor = actorPtr.get();
            if (!actor->HasComponent<MeshComponent>()) continue;
            auto* mc = actor->GetComponent<MeshComponent>();
            if (geoMgr.IsSkeletal(mc->MeshPath)) continue;
            if (mc->Blend == MeshBlendMode::AlphaClip) continue;  // handled in alpha-clip pass below
            if (mc->Blend == MeshBlendMode::Translucent) continue;
            if (!mc->CastShadow) continue;

            const MeshAsset* mesh = geoMgr.LoadMesh(mc->MeshPath);
            if (!mesh || mesh->SubMeshes.empty()) continue;

            // Switch PSO variant (DoubleSided only, no alpha clip: index 0 or 2)
            ID3D12PipelineState* pso = m_psoVariants[mc->DoubleSided ? 2 : 0].Get();
            if (pso != curPso) { cmd->SetPipelineState(pso); curPso = pso; }

            auto* t = actor->GetComponent<TransformComponent>();
            Matrix4x4 world    = t ? t->GetWorldMatrix() : Matrix4x4::Identity;
            Matrix4x4 lightWVP = data.LightViewProj[c] * world;

            uint8_t* slot = cbBase + ((UINT64)c * MAX_OBJECTS + slotIdx) * CB_SLOT_SIZE;
            memcpy(slot, lightWVP.v, 64);

            D3D12_GPU_VIRTUAL_ADDRESS cbAddr =
                m_cbuffer[fi]->GetGPUVirtualAddress() +
                ((UINT64)c * MAX_OBJECTS + slotIdx) * CB_SLOT_SIZE;
            cmd->SetGraphicsRootConstantBufferView(0, cbAddr);

            cmd->IASetVertexBuffers(0, 1, &mesh->VBView);
            cmd->IASetIndexBuffer(&mesh->IBView);
            for (const SubMesh& sm : mesh->SubMeshes)
                cmd->DrawIndexedInstanced(sm.IndexCount, 1, sm.StartIndex, sm.BaseVertex, 0);
            ++slotIdx;
        }

        // --- Static shadow: alpha-clip actors (use m_rootSignatureAlphaClip) ---
        cmd->SetGraphicsRootSignature(m_rootSignatureAlphaClip.Get());
        curPso = nullptr;

        uint32_t acSlotIdx = slotIdx;  // continue filling the same CB slots
        for (auto& actorPtr : scene.GetActors()) {
            if (acSlotIdx >= MAX_OBJECTS) break;
            Actor* actor = actorPtr.get();
            if (!actor->HasComponent<MeshComponent>()) continue;
            auto* mc = actor->GetComponent<MeshComponent>();
            if (geoMgr.IsSkeletal(mc->MeshPath)) continue;
            if (mc->Blend != MeshBlendMode::AlphaClip) continue;
            if (!mc->CastShadow) continue;

            const MeshAsset* mesh = geoMgr.LoadMesh(mc->MeshPath);
            if (!mesh || mesh->SubMeshes.empty()) continue;

            // Switch PSO variant (DoubleSided + alpha clip: index 1 or 3)
            ID3D12PipelineState* pso = m_psoVariants[mc->DoubleSided ? 3 : 1].Get();
            if (pso != curPso) { cmd->SetPipelineState(pso); curPso = pso; }

            auto* t = actor->GetComponent<TransformComponent>();
            Matrix4x4 world    = t ? t->GetWorldMatrix() : Matrix4x4::Identity;
            Matrix4x4 lightWVP = data.LightViewProj[c] * world;

            uint8_t* slot = cbBase + ((UINT64)c * MAX_OBJECTS + acSlotIdx) * CB_SLOT_SIZE;
            memcpy(slot, lightWVP.v, 64);

            D3D12_GPU_VIRTUAL_ADDRESS cbAddr =
                m_cbuffer[fi]->GetGPUVirtualAddress() +
                ((UINT64)c * MAX_OBJECTS + acSlotIdx) * CB_SLOT_SIZE;
            cmd->SetGraphicsRootConstantBufferView(0, cbAddr);

            // Resolve albedo texture for alpha test
            const Material* mat = mc->MaterialPath.empty() ? nullptr : matMgr.LoadOrCreate(mc->MaterialPath);
            const SubMesh& firstSm = mesh->SubMeshes[0];
            const std::string& albedoPath = (mat && !mat->AlbedoTexturePath.empty())
                                             ? mat->AlbedoTexturePath : firstSm.DiffusePath;
            uint32_t albedoSlot = texMgr.LoadTexture(albedoPath);
            cmd->SetGraphicsRootDescriptorTable(1, gfx.GetSRVGPUHandle(albedoSlot));

            cmd->IASetVertexBuffers(0, 1, &mesh->VBView);
            cmd->IASetIndexBuffer(&mesh->IBView);
            for (const SubMesh& sm : mesh->SubMeshes)
                cmd->DrawIndexedInstanced(sm.IndexCount, 1, sm.StartIndex, sm.BaseVertex, 0);
            ++acSlotIdx;
        }

        // --- Skinned shadow pass ---
        cmd->SetPipelineState(m_skinnedPso.Get());
        cmd->SetGraphicsRootSignature(m_skinnedRootSig.Get());

        uint32_t skinSlot = 0;
        for (auto& actorPtr : scene.GetActors()) {
            if (skinSlot >= MAX_SKINNED_OBJECTS) break;
            Actor* actor = actorPtr.get();
            auto*  mc    = actor->GetComponent<MeshComponent>();
            if (!mc || !geoMgr.IsSkeletal(mc->MeshPath)) continue;

            const SkeletalMeshAsset* mesh = geoMgr.LoadSkeletalMesh(mc->MeshPath);
            if (!mesh || mesh->SubMeshes.empty()) { ++skinSlot; continue; }
            if (!mc->CastShadow) { ++skinSlot; continue; }

            auto* t = actor->GetComponent<TransformComponent>();
            Matrix4x4 world    = t ? t->GetWorldMatrix() : Matrix4x4::Identity;
            Matrix4x4 lightWVP = data.LightViewProj[c] * world;

            uint8_t* slot =
                m_skinnedCBMapped[fi] +
                ((UINT64)c * MAX_SKINNED_OBJECTS + skinSlot) * CB_SLOT_SIZE;
            memcpy(slot, lightWVP.v, 64);

            D3D12_GPU_VIRTUAL_ADDRESS cbAddr =
                m_skinnedCB[fi]->GetGPUVirtualAddress() +
                ((UINT64)c * MAX_SKINNED_OBJECTS + skinSlot) * CB_SLOT_SIZE;
            D3D12_GPU_VIRTUAL_ADDRESS palAddr =
                m_bonePaletteCB[fi]->GetGPUVirtualAddress() +
                (UINT64)skinSlot * BONE_PALETTE_STRIDE;

            cmd->SetGraphicsRootConstantBufferView(0, cbAddr);
            cmd->SetGraphicsRootConstantBufferView(1, palAddr);

            cmd->IASetVertexBuffers(0, 1, &mesh->VBView);
            cmd->IASetIndexBuffer(&mesh->IBView);
            for (const SubMesh& sm : mesh->SubMeshes)
                cmd->DrawIndexedInstanced(sm.IndexCount, 1, sm.StartIndex, sm.BaseVertex, 0);
            ++skinSlot;
        }
    }
}

void ShadowPass::Shutdown() {
    for (uint32_t i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i) {
        if (m_cbMapped[i])           { m_cbuffer[i]->Unmap(0, nullptr);       m_cbMapped[i] = nullptr; }
        if (m_skinnedCBMapped[i])    { m_skinnedCB[i]->Unmap(0, nullptr);     m_skinnedCBMapped[i] = nullptr; }
        if (m_bonePaletteMapped[i])  { m_bonePaletteCB[i]->Unmap(0, nullptr); m_bonePaletteMapped[i] = nullptr; }
        if (m_spotCBMapped[i])       { m_spotCB[i]->Unmap(0, nullptr);        m_spotCBMapped[i] = nullptr; }
        if (m_spotSkinnedMapped[i])  { m_spotSkinnedCB[i]->Unmap(0, nullptr); m_spotSkinnedMapped[i] = nullptr; }
        if (m_pointCBMapped[i])      { m_pointCB[i]->Unmap(0, nullptr);        m_pointCBMapped[i] = nullptr; }
        if (m_pointSkinnedMapped[i]) { m_pointSkinnedCB[i]->Unmap(0, nullptr); m_pointSkinnedMapped[i] = nullptr; }
        m_cbuffer[i].Reset();
        m_skinnedCB[i].Reset();
        m_bonePaletteCB[i].Reset();
        m_spotCB[i].Reset();
        m_spotSkinnedCB[i].Reset();
        m_pointCB[i].Reset();
        m_pointSkinnedCB[i].Reset();
    }
    m_shadowMap.Reset();
    m_dsvHeap.Reset();
    m_spotMap.Reset();
    m_spotDsvHeap.Reset();
    m_pointMap.Reset();
    m_pointDsvHeap.Reset();
    for (auto& pso : m_psoVariants) pso.Reset();
    m_rootSignature.Reset();
    m_rootSignatureAlphaClip.Reset();
    m_skinnedPso.Reset();
    m_skinnedRootSig.Reset();
}

bool ShadowPass::CreateSkinnedRootSignature(ID3D12Device* device) {
    D3D12_ROOT_PARAMETER params[2] = {};

    // b0: per-object LightWVP (VS only)
    params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;

    // b1: bone palette (VS only)
    params[1].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].Descriptor.ShaderRegister = 1;
    params[1].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters = 2;
    rsDesc.pParameters   = params;
    rsDesc.Flags         = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> rsBlob, rsErr;
    D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr);
    return SUCCEEDED(device->CreateRootSignature(0, rsBlob->GetBufferPointer(),
                                                  rsBlob->GetBufferSize(),
                                                  IID_PPV_ARGS(&m_skinnedRootSig)));
}

bool ShadowPass::CreateSkinnedPipelineState(ID3D12Device* device) {
    auto vs = LoadOrCompileShader(L"Resource/Shaders/ShadowPassSkinned.VS.hlsl", L"vs_6_0");

    // Only the elements the skinned shadow shader reads (position + blend data)
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT",      0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT,       0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_UINT,  0, 44, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 60, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_RASTERIZER_DESC rastDesc = {};
    rastDesc.FillMode             = D3D12_FILL_MODE_SOLID;
    rastDesc.CullMode             = D3D12_CULL_MODE_BACK;
    rastDesc.DepthClipEnable      = TRUE;
    rastDesc.DepthBias            = 1000;
    rastDesc.SlopeScaledDepthBias = 1.0f;

    D3D12_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].RenderTargetWriteMask = 0;

    D3D12_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable    = TRUE;
    dsDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    dsDesc.DepthFunc      = D3D12_COMPARISON_FUNC_LESS;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature        = m_skinnedRootSig.Get();
    psoDesc.VS                    = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS                    = { nullptr, 0 };
    psoDesc.InputLayout           = { inputLayout, 6 };
    psoDesc.RasterizerState       = rastDesc;
    psoDesc.BlendState            = blendDesc;
    psoDesc.DepthStencilState     = dsDesc;
    psoDesc.DSVFormat             = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleMask            = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets      = 0;
    psoDesc.SampleDesc            = { 1, 0 };

    return SUCCEEDED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_skinnedPso)));
}

// ============================ Spot light shadows (Stage A) ============================

Matrix4x4 ShadowPass::ComputeSpotMatrix(const Vector3& pos, const Vector3& dir,
                                        float spotAngleDeg, float range) {
    Vector3 d  = dir.GetSafeNormal();
    Vector3 up = (fabsf(d.y) < 0.99f) ? Vector3(0, 1, 0) : Vector3(1, 0, 0);
    Matrix4x4 view = Matrix4x4::LookAt(pos, pos + d, up);
    // Use the full cone aperture as the (square) vertical FOV, plus a small margin so the
    // circular cone edge isn't clipped by the square shadow frustum.
    float fovDeg = spotAngleDeg * 1.05f;
    if (fovDeg > 179.0f) fovDeg = 179.0f;
    const float nearZ = 0.05f;
    float       farZ  = (range > nearZ + 0.01f) ? range : (nearZ + 1.0f);
    Matrix4x4 proj = Matrix4x4::Perspective(Math::ToRadians(fovDeg), 1.0f, nearZ, farZ);
    return proj * view;
}

bool ShadowPass::CreateSpotShadowMap(GraphicsDevice& gfx) {
    ID3D12Device* device = gfx.GetDevice();

    D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width            = SPOT_MAP_SIZE;
    rd.Height           = SPOT_MAP_SIZE;
    rd.DepthOrArraySize = (UINT16)MAX_SHADOW_SPOTS;
    rd.MipLevels        = 1;
    rd.Format           = DXGI_FORMAT_R32_TYPELESS;
    rd.SampleDesc.Count = 1;
    rd.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rd.Flags            = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearVal = {};
    clearVal.Format             = DXGI_FORMAT_D32_FLOAT;
    clearVal.DepthStencil.Depth = 1.0f;

    if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearVal, IID_PPV_ARGS(&m_spotMap))))
        return false;

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.NumDescriptors = MAX_SHADOW_SPOTS;
    if (FAILED(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_spotDsvHeap))))
        return false;

    uint32_t dsvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    D3D12_CPU_DESCRIPTOR_HANDLE dsvBase = m_spotDsvHeap->GetCPUDescriptorHandleForHeapStart();
    for (uint32_t s = 0; s < MAX_SHADOW_SPOTS; ++s) {
        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format                         = DXGI_FORMAT_D32_FLOAT;
        dsvDesc.ViewDimension                  = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
        dsvDesc.Texture2DArray.MipSlice        = 0;
        dsvDesc.Texture2DArray.FirstArraySlice = s;
        dsvDesc.Texture2DArray.ArraySize       = 1;
        m_spotDsvHandles[s].ptr = dsvBase.ptr + (SIZE_T)s * dsvSize;
        device->CreateDepthStencilView(m_spotMap.Get(), &dsvDesc, m_spotDsvHandles[s]);
    }

    // SRV: Texture2DArray, R32_FLOAT, all slices (sampled with the comparison sampler in the PS).
    m_spotSrvSlot = gfx.AllocateSRVSlot();
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                         = DXGI_FORMAT_R32_FLOAT;
    srvDesc.ViewDimension                  = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    srvDesc.Shader4ComponentMapping        = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2DArray.MostDetailedMip = 0;
    srvDesc.Texture2DArray.MipLevels       = 1;
    srvDesc.Texture2DArray.FirstArraySlice = 0;
    srvDesc.Texture2DArray.ArraySize       = MAX_SHADOW_SPOTS;
    device->CreateShaderResourceView(m_spotMap.Get(), &srvDesc, gfx.GetSRVCPUHandle(m_spotSrvSlot));
    return true;
}

bool ShadowPass::CreateSpotConstantBuffers(ID3D12Device* device) {
    D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RANGE range = { 0, 0 };
    auto makeCB = [&](UINT64 size, ComPtr<ID3D12Resource>& res, uint8_t** mapped) -> bool {
        D3D12_RESOURCE_DESC rd = {};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width = size;
        rd.Height = rd.DepthOrArraySize = rd.MipLevels = 1;
        rd.Format = DXGI_FORMAT_UNKNOWN; rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&res))))
            return false;
        res->Map(0, &range, reinterpret_cast<void**>(mapped));
        return true;
    };
    for (uint32_t i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i) {
        if (!makeCB((UINT64)MAX_SHADOW_SPOTS * MAX_OBJECTS         * CB_SLOT_SIZE, m_spotCB[i],        &m_spotCBMapped[i]))        return false;
        if (!makeCB((UINT64)MAX_SHADOW_SPOTS * MAX_SKINNED_OBJECTS * CB_SLOT_SIZE, m_spotSkinnedCB[i], &m_spotSkinnedMapped[i]))   return false;
    }
    return true;
}

void ShadowPass::ExecuteSpotGPU(ID3D12GraphicsCommandList* cmd,
                                const SceneManager& scene,
                                uint32_t frameIndex,
                                GeometryManager& geoMgr,
                                TextureManager& texMgr,
                                MaterialManager& matMgr,
                                GraphicsDevice& gfx,
                                const SpotShadowData& data) {
    if (data.Count == 0) return;
    uint32_t fi = frameIndex % NUM_FRAMES_IN_FLIGHT;
    const uint32_t count = (data.Count < MAX_SHADOW_SPOTS) ? data.Count : MAX_SHADOW_SPOTS;

    D3D12_VIEWPORT vp      = { 0, 0, float(SPOT_MAP_SIZE), float(SPOT_MAP_SIZE), 0, 1 };
    D3D12_RECT     scissor = { 0, 0, SPOT_MAP_SIZE, SPOT_MAP_SIZE };
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &scissor);

    // Bone palettes are shared with the cascade pass (per-actor, not per-light); upload here too
    // so this pass is correct regardless of RG ordering relative to ExecuteGPU.
    static const Matrix4x4 s_identityPalette[MAX_BONES] = {};
    {
        uint32_t palSlot = 0;
        for (auto& actorPtr : scene.GetActors()) {
            if (palSlot >= MAX_SKINNED_OBJECTS) break;
            Actor* actor = actorPtr.get();
            auto*  mc    = actor->GetComponent<MeshComponent>();
            if (!mc || !geoMgr.IsSkeletal(mc->MeshPath)) continue;
            const SkeletalMeshAsset* mesh = geoMgr.LoadSkeletalMesh(mc->MeshPath);
            if (!mesh || mesh->SubMeshes.empty()) { ++palSlot; continue; }
            auto*            ac  = actor->GetComponent<AnimationComponent>();
            const Matrix4x4* pal = (ac && ac->PaletteReady) ? ac->BonePalette.data() : s_identityPalette;
            memcpy(m_bonePaletteMapped[fi] + palSlot * BONE_PALETTE_STRIDE, pal, BONE_PALETTE_STRIDE);
            ++palSlot;
        }
    }

    for (uint32_t s = 0; s < count; ++s) {
        cmd->OMSetRenderTargets(0, nullptr, FALSE, &m_spotDsvHandles[s]);
        cmd->ClearDepthStencilView(m_spotDsvHandles[s], D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

        Plane planes[6];
        ExtractFrustumPlanes(data.ViewProj[s], planes);

        // --- Static (non-alpha-clip) casters ---
        cmd->SetGraphicsRootSignature(m_rootSignature.Get());
        ID3D12PipelineState* curPso = nullptr;
        uint32_t slotIdx = 0;
        for (auto& actorPtr : scene.GetActors()) {
            if (slotIdx >= MAX_OBJECTS) break;
            Actor* actor = actorPtr.get();
            if (!actor->HasComponent<MeshComponent>()) continue;
            auto* mc = actor->GetComponent<MeshComponent>();
            if (geoMgr.IsSkeletal(mc->MeshPath)) continue;
            if (mc->Blend == MeshBlendMode::AlphaClip) continue;
            if (mc->Blend == MeshBlendMode::Translucent) continue;
            if (!mc->CastShadow) continue;

            const MeshAsset* mesh = geoMgr.LoadMesh(mc->MeshPath);
            if (!mesh || mesh->SubMeshes.empty()) continue;

            auto* t = actor->GetComponent<TransformComponent>();
            Matrix4x4 world = t ? t->GetWorldMatrix() : Matrix4x4::Identity;
            if (!AabbInFrustum(TransformBoundsToWorld(mesh->BoundsMin, mesh->BoundsMax, world), planes))
                continue;

            ID3D12PipelineState* pso = m_psoVariants[mc->DoubleSided ? 2 : 0].Get();
            if (pso != curPso) { cmd->SetPipelineState(pso); curPso = pso; }

            Matrix4x4 lightWVP = data.ViewProj[s] * world;
            uint8_t* slot = m_spotCBMapped[fi] + ((UINT64)s * MAX_OBJECTS + slotIdx) * CB_SLOT_SIZE;
            memcpy(slot, lightWVP.v, 64);
            cmd->SetGraphicsRootConstantBufferView(0,
                m_spotCB[fi]->GetGPUVirtualAddress() + ((UINT64)s * MAX_OBJECTS + slotIdx) * CB_SLOT_SIZE);

            cmd->IASetVertexBuffers(0, 1, &mesh->VBView);
            cmd->IASetIndexBuffer(&mesh->IBView);
            for (const SubMesh& sm : mesh->SubMeshes)
                cmd->DrawIndexedInstanced(sm.IndexCount, 1, sm.StartIndex, sm.BaseVertex, 0);
            ++slotIdx;
        }

        // --- Alpha-clip casters ---
        cmd->SetGraphicsRootSignature(m_rootSignatureAlphaClip.Get());
        curPso = nullptr;
        uint32_t acSlotIdx = slotIdx;
        for (auto& actorPtr : scene.GetActors()) {
            if (acSlotIdx >= MAX_OBJECTS) break;
            Actor* actor = actorPtr.get();
            if (!actor->HasComponent<MeshComponent>()) continue;
            auto* mc = actor->GetComponent<MeshComponent>();
            if (geoMgr.IsSkeletal(mc->MeshPath)) continue;
            if (mc->Blend != MeshBlendMode::AlphaClip) continue;
            if (!mc->CastShadow) continue;

            const MeshAsset* mesh = geoMgr.LoadMesh(mc->MeshPath);
            if (!mesh || mesh->SubMeshes.empty()) continue;

            auto* t = actor->GetComponent<TransformComponent>();
            Matrix4x4 world = t ? t->GetWorldMatrix() : Matrix4x4::Identity;
            if (!AabbInFrustum(TransformBoundsToWorld(mesh->BoundsMin, mesh->BoundsMax, world), planes))
                continue;

            ID3D12PipelineState* pso = m_psoVariants[mc->DoubleSided ? 3 : 1].Get();
            if (pso != curPso) { cmd->SetPipelineState(pso); curPso = pso; }

            Matrix4x4 lightWVP = data.ViewProj[s] * world;
            uint8_t* slot = m_spotCBMapped[fi] + ((UINT64)s * MAX_OBJECTS + acSlotIdx) * CB_SLOT_SIZE;
            memcpy(slot, lightWVP.v, 64);
            cmd->SetGraphicsRootConstantBufferView(0,
                m_spotCB[fi]->GetGPUVirtualAddress() + ((UINT64)s * MAX_OBJECTS + acSlotIdx) * CB_SLOT_SIZE);

            const Material* mat = mc->MaterialPath.empty() ? nullptr : matMgr.LoadOrCreate(mc->MaterialPath);
            const SubMesh& firstSm = mesh->SubMeshes[0];
            const std::string& albedoPath = (mat && !mat->AlbedoTexturePath.empty())
                                             ? mat->AlbedoTexturePath : firstSm.DiffusePath;
            uint32_t albedoSlot = texMgr.LoadTexture(albedoPath);
            cmd->SetGraphicsRootDescriptorTable(1, gfx.GetSRVGPUHandle(albedoSlot));

            cmd->IASetVertexBuffers(0, 1, &mesh->VBView);
            cmd->IASetIndexBuffer(&mesh->IBView);
            for (const SubMesh& sm : mesh->SubMeshes)
                cmd->DrawIndexedInstanced(sm.IndexCount, 1, sm.StartIndex, sm.BaseVertex, 0);
            ++acSlotIdx;
        }

        // --- Skinned casters (pose-varying bounds → not frustum-culled) ---
        cmd->SetPipelineState(m_skinnedPso.Get());
        cmd->SetGraphicsRootSignature(m_skinnedRootSig.Get());
        uint32_t skinSlot = 0;
        for (auto& actorPtr : scene.GetActors()) {
            if (skinSlot >= MAX_SKINNED_OBJECTS) break;
            Actor* actor = actorPtr.get();
            auto*  mc    = actor->GetComponent<MeshComponent>();
            if (!mc || !geoMgr.IsSkeletal(mc->MeshPath)) continue;
            const SkeletalMeshAsset* mesh = geoMgr.LoadSkeletalMesh(mc->MeshPath);
            if (!mesh || mesh->SubMeshes.empty()) { ++skinSlot; continue; }
            if (!mc->CastShadow) { ++skinSlot; continue; }

            auto* t = actor->GetComponent<TransformComponent>();
            Matrix4x4 world    = t ? t->GetWorldMatrix() : Matrix4x4::Identity;
            Matrix4x4 lightWVP = data.ViewProj[s] * world;

            uint8_t* slot = m_spotSkinnedMapped[fi] +
                ((UINT64)s * MAX_SKINNED_OBJECTS + skinSlot) * CB_SLOT_SIZE;
            memcpy(slot, lightWVP.v, 64);
            cmd->SetGraphicsRootConstantBufferView(0,
                m_spotSkinnedCB[fi]->GetGPUVirtualAddress() +
                ((UINT64)s * MAX_SKINNED_OBJECTS + skinSlot) * CB_SLOT_SIZE);
            cmd->SetGraphicsRootConstantBufferView(1,
                m_bonePaletteCB[fi]->GetGPUVirtualAddress() + (UINT64)skinSlot * BONE_PALETTE_STRIDE);

            cmd->IASetVertexBuffers(0, 1, &mesh->VBView);
            cmd->IASetIndexBuffer(&mesh->IBView);
            for (const SubMesh& sm : mesh->SubMeshes)
                cmd->DrawIndexedInstanced(sm.IndexCount, 1, sm.StartIndex, sm.BaseVertex, 0);
            ++skinSlot;
        }
    }
}

// ============================ Point light shadows (Stage B) ============================

// Standard D3D cubemap face cameras (left-handed, matching HW cube sampling).
static const Vector3 kCubeFaceDir[6] = {
    { 1, 0, 0}, {-1, 0, 0}, { 0, 1, 0}, { 0,-1, 0}, { 0, 0, 1}, { 0, 0,-1},
};
static const Vector3 kCubeFaceUp[6] = {
    { 0, 1, 0}, { 0, 1, 0}, { 0, 0,-1}, { 0, 0, 1}, { 0, 1, 0}, { 0, 1, 0},
};

bool ShadowPass::CreatePointShadowMap(GraphicsDevice& gfx) {
    ID3D12Device* device = gfx.GetDevice();

    D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width            = POINT_MAP_SIZE;
    rd.Height           = POINT_MAP_SIZE;
    rd.DepthOrArraySize = (UINT16)POINT_FACES;       // 6 × MAX_SHADOW_POINTS
    rd.MipLevels        = 1;
    rd.Format           = DXGI_FORMAT_R32_TYPELESS;
    rd.SampleDesc.Count = 1;
    rd.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rd.Flags            = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearVal = {};
    clearVal.Format             = DXGI_FORMAT_D32_FLOAT;
    clearVal.DepthStencil.Depth = 1.0f;

    // Created in PIXEL_SHADER_RESOURCE so it is always safe to bind as an SRV even on frames where
    // no point shadow pass runs (Count==0). The RenderGraph transitions PSR→DEPTH_WRITE→PSR when used.
    if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearVal, IID_PPV_ARGS(&m_pointMap))))
        return false;

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.NumDescriptors = POINT_FACES;
    if (FAILED(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_pointDsvHeap))))
        return false;

    uint32_t dsvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    D3D12_CPU_DESCRIPTOR_HANDLE dsvBase = m_pointDsvHeap->GetCPUDescriptorHandleForHeapStart();
    for (uint32_t s = 0; s < POINT_FACES; ++s) {
        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format                         = DXGI_FORMAT_D32_FLOAT;
        dsvDesc.ViewDimension                  = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
        dsvDesc.Texture2DArray.MipSlice        = 0;
        dsvDesc.Texture2DArray.FirstArraySlice = s;
        dsvDesc.Texture2DArray.ArraySize       = 1;
        m_pointDsvHandles[s].ptr = dsvBase.ptr + (SIZE_T)s * dsvSize;
        device->CreateDepthStencilView(m_pointMap.Get(), &dsvDesc, m_pointDsvHandles[s]);
    }

    // SRV: TextureCubeArray, R32_FLOAT (sampled with the comparison sampler in the PS).
    m_pointSrvSlot = gfx.AllocateSRVSlot();
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                            = DXGI_FORMAT_R32_FLOAT;
    srvDesc.ViewDimension                     = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
    srvDesc.Shader4ComponentMapping           = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.TextureCubeArray.MostDetailedMip  = 0;
    srvDesc.TextureCubeArray.MipLevels        = 1;
    srvDesc.TextureCubeArray.First2DArrayFace = 0;
    srvDesc.TextureCubeArray.NumCubes         = MAX_SHADOW_POINTS;
    device->CreateShaderResourceView(m_pointMap.Get(), &srvDesc, gfx.GetSRVCPUHandle(m_pointSrvSlot));
    return true;
}

bool ShadowPass::CreatePointConstantBuffers(ID3D12Device* device) {
    D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RANGE range = { 0, 0 };
    auto makeCB = [&](UINT64 size, ComPtr<ID3D12Resource>& res, uint8_t** mapped) -> bool {
        D3D12_RESOURCE_DESC rd = {};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width = size;
        rd.Height = rd.DepthOrArraySize = rd.MipLevels = 1;
        rd.Format = DXGI_FORMAT_UNKNOWN; rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&res))))
            return false;
        res->Map(0, &range, reinterpret_cast<void**>(mapped));
        return true;
    };
    for (uint32_t i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i) {
        if (!makeCB((UINT64)POINT_FACES * MAX_OBJECTS         * CB_SLOT_SIZE, m_pointCB[i],        &m_pointCBMapped[i]))        return false;
        if (!makeCB((UINT64)POINT_FACES * MAX_SKINNED_OBJECTS * CB_SLOT_SIZE, m_pointSkinnedCB[i], &m_pointSkinnedMapped[i]))   return false;
    }
    return true;
}

void ShadowPass::ExecutePointGPU(ID3D12GraphicsCommandList* cmd,
                                 const SceneManager& scene,
                                 uint32_t frameIndex,
                                 GeometryManager& geoMgr,
                                 TextureManager& texMgr,
                                 MaterialManager& matMgr,
                                 GraphicsDevice& gfx,
                                 const PointShadowData& data) {
    if (data.Count == 0) return;
    uint32_t fi = frameIndex % NUM_FRAMES_IN_FLIGHT;
    const uint32_t count = (data.Count < MAX_SHADOW_POINTS) ? data.Count : MAX_SHADOW_POINTS;

    D3D12_VIEWPORT vp      = { 0, 0, float(POINT_MAP_SIZE), float(POINT_MAP_SIZE), 0, 1 };
    D3D12_RECT     scissor = { 0, 0, POINT_MAP_SIZE, POINT_MAP_SIZE };
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &scissor);

    // Shared bone palettes (per-actor), uploaded here so this pass is correct regardless of RG order.
    static const Matrix4x4 s_identityPalette[MAX_BONES] = {};
    {
        uint32_t palSlot = 0;
        for (auto& actorPtr : scene.GetActors()) {
            if (palSlot >= MAX_SKINNED_OBJECTS) break;
            Actor* actor = actorPtr.get();
            auto*  mc    = actor->GetComponent<MeshComponent>();
            if (!mc || !geoMgr.IsSkeletal(mc->MeshPath)) continue;
            const SkeletalMeshAsset* mesh = geoMgr.LoadSkeletalMesh(mc->MeshPath);
            if (!mesh || mesh->SubMeshes.empty()) { ++palSlot; continue; }
            auto*            ac  = actor->GetComponent<AnimationComponent>();
            const Matrix4x4* pal = (ac && ac->PaletteReady) ? ac->BonePalette.data() : s_identityPalette;
            memcpy(m_bonePaletteMapped[fi] + palSlot * BONE_PALETTE_STRIDE, pal, BONE_PALETTE_STRIDE);
            ++palSlot;
        }
    }

    // Gather the caster list ONCE. Component/asset resolution (GetComponent dynamic_cast, LoadMesh,
    // LoadTexture) is expensive and was previously redone for all 6 faces of every light. Now each
    // face only does a frustum test + draw from this cache. Skinned slots match the palette upload
    // order above (every skeletal actor consumes a slot, capped at MAX_SKINNED_OBJECTS).
    struct PtCaster {
        const MeshAsset*         mesh  = nullptr;   // non-skinned
        const SkeletalMeshAsset* smesh = nullptr;   // skinned
        Matrix4x4 world;
        Aabb      box;                              // world AABB (non-skinned only)
        Vector3   center;                           // box center (non-skinned)
        float     radius = 0.0f;                    // box bounding radius (non-skinned)
        int       kind = 0;                         // 0=static, 1=alphaClip, 2=skinned
        bool      doubleSided = false;
        uint32_t  albedoSlot  = 0;                  // alphaClip
        uint32_t  skinSlot    = 0;                  // skinned
    };
    std::vector<PtCaster> casters;
    casters.reserve(64);
    uint32_t skinCounter = 0;
    for (auto& actorPtr : scene.GetActors()) {
        Actor* actor = actorPtr.get();
        auto* mc = actor->GetComponent<MeshComponent>();
        if (!mc) continue;
        auto* t = actor->GetComponent<TransformComponent>();
        Matrix4x4 world = t ? t->GetWorldMatrix() : Matrix4x4::Identity;

        if (geoMgr.IsSkeletal(mc->MeshPath)) {
            if (skinCounter >= MAX_SKINNED_OBJECTS) continue;
            const SkeletalMeshAsset* sm = geoMgr.LoadSkeletalMesh(mc->MeshPath);
            uint32_t slot = skinCounter++;                 // consume a palette slot regardless
            if (!sm || sm->SubMeshes.empty() || !mc->CastShadow) continue;
            PtCaster c; c.smesh = sm; c.world = world; c.kind = 2; c.skinSlot = slot;
            casters.push_back(c);
            continue;
        }
        if (mc->Blend == MeshBlendMode::Translucent) continue;
        if (!mc->CastShadow) continue;                     // per-primitive shadow opt-out
        const MeshAsset* mesh = geoMgr.LoadMesh(mc->MeshPath);
        if (!mesh || mesh->SubMeshes.empty()) continue;
        PtCaster c;
        c.mesh        = mesh;
        c.world       = world;
        c.box         = TransformBoundsToWorld(mesh->BoundsMin, mesh->BoundsMax, world);
        c.center      = (c.box.lo + c.box.hi) * 0.5f;
        c.radius      = (c.box.hi - c.box.lo).Length() * 0.5f;
        c.doubleSided = mc->DoubleSided;
        if (mc->Blend == MeshBlendMode::AlphaClip) {
            c.kind = 1;
            const Material* mat = mc->MaterialPath.empty() ? nullptr : matMgr.LoadOrCreate(mc->MaterialPath);
            const SubMesh& firstSm = mesh->SubMeshes[0];
            const std::string& albedoPath = (mat && !mat->AlbedoTexturePath.empty())
                                             ? mat->AlbedoTexturePath : firstSm.DiffusePath;
            c.albedoSlot = texMgr.LoadTexture(albedoPath);
        } else {
            c.kind = 0;
        }
        casters.push_back(c);
    }

    // Conservative angular-size cull: a caster whose bounding sphere subtends less than this ratio
    // of its distance to the light casts a negligible shadow — skip it (UE5 MinScreenRadius idea).
    const float kMinShadowRatio = 0.03f;

    const float nearZ = 0.05f;
    for (uint32_t p = 0; p < count; ++p) {
        Vector3   pos  = data.Pos[p];
        float     farZ = (data.Range[p] > nearZ + 0.01f) ? data.Range[p] : (nearZ + 1.0f);
        Matrix4x4 proj = Matrix4x4::Perspective(Math::ToRadians(90.0f), 1.0f, nearZ, farZ);

        for (uint32_t f = 0; f < 6; ++f) {
            uint32_t  slice    = p * 6 + f;
            Matrix4x4 view     = Matrix4x4::LookAt(pos, pos + kCubeFaceDir[f], kCubeFaceUp[f]);
            Matrix4x4 viewProj = proj * view;

            cmd->OMSetRenderTargets(0, nullptr, FALSE, &m_pointDsvHandles[slice]);
            cmd->ClearDepthStencilView(m_pointDsvHandles[slice], D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

            Plane planes[6];
            ExtractFrustumPlanes(viewProj, planes);

            // --- Static casters ---
            cmd->SetGraphicsRootSignature(m_rootSignature.Get());
            ID3D12PipelineState* curPso = nullptr;
            uint32_t slotIdx = 0;
            for (const PtCaster& c : casters) {
                if (c.kind != 0) continue;
                if (slotIdx >= MAX_OBJECTS) break;
                if (c.radius < (c.center - pos).Length() * kMinShadowRatio) continue;
                if (!AabbInFrustum(c.box, planes)) continue;
                ID3D12PipelineState* pso = m_psoVariants[c.doubleSided ? 2 : 0].Get();
                if (pso != curPso) { cmd->SetPipelineState(pso); curPso = pso; }
                Matrix4x4 lightWVP = viewProj * c.world;
                uint8_t* slot = m_pointCBMapped[fi] + ((UINT64)slice * MAX_OBJECTS + slotIdx) * CB_SLOT_SIZE;
                memcpy(slot, lightWVP.v, 64);
                cmd->SetGraphicsRootConstantBufferView(0,
                    m_pointCB[fi]->GetGPUVirtualAddress() + ((UINT64)slice * MAX_OBJECTS + slotIdx) * CB_SLOT_SIZE);
                cmd->IASetVertexBuffers(0, 1, &c.mesh->VBView);
                cmd->IASetIndexBuffer(&c.mesh->IBView);
                for (const SubMesh& sm : c.mesh->SubMeshes)
                    cmd->DrawIndexedInstanced(sm.IndexCount, 1, sm.StartIndex, sm.BaseVertex, 0);
                ++slotIdx;
            }

            // --- Alpha-clip casters ---
            cmd->SetGraphicsRootSignature(m_rootSignatureAlphaClip.Get());
            curPso = nullptr;
            uint32_t acSlotIdx = slotIdx;
            for (const PtCaster& c : casters) {
                if (c.kind != 1) continue;
                if (acSlotIdx >= MAX_OBJECTS) break;
                if (c.radius < (c.center - pos).Length() * kMinShadowRatio) continue;
                if (!AabbInFrustum(c.box, planes)) continue;
                ID3D12PipelineState* pso = m_psoVariants[c.doubleSided ? 3 : 1].Get();
                if (pso != curPso) { cmd->SetPipelineState(pso); curPso = pso; }
                Matrix4x4 lightWVP = viewProj * c.world;
                uint8_t* slot = m_pointCBMapped[fi] + ((UINT64)slice * MAX_OBJECTS + acSlotIdx) * CB_SLOT_SIZE;
                memcpy(slot, lightWVP.v, 64);
                cmd->SetGraphicsRootConstantBufferView(0,
                    m_pointCB[fi]->GetGPUVirtualAddress() + ((UINT64)slice * MAX_OBJECTS + acSlotIdx) * CB_SLOT_SIZE);
                cmd->SetGraphicsRootDescriptorTable(1, gfx.GetSRVGPUHandle(c.albedoSlot));
                cmd->IASetVertexBuffers(0, 1, &c.mesh->VBView);
                cmd->IASetIndexBuffer(&c.mesh->IBView);
                for (const SubMesh& sm : c.mesh->SubMeshes)
                    cmd->DrawIndexedInstanced(sm.IndexCount, 1, sm.StartIndex, sm.BaseVertex, 0);
                ++acSlotIdx;
            }

            // --- Skinned casters (pose-varying bounds → not frustum-culled) ---
            cmd->SetPipelineState(m_skinnedPso.Get());
            cmd->SetGraphicsRootSignature(m_skinnedRootSig.Get());
            for (const PtCaster& c : casters) {
                if (c.kind != 2) continue;
                Matrix4x4 lightWVP = viewProj * c.world;
                uint8_t* slot = m_pointSkinnedMapped[fi] +
                    ((UINT64)slice * MAX_SKINNED_OBJECTS + c.skinSlot) * CB_SLOT_SIZE;
                memcpy(slot, lightWVP.v, 64);
                cmd->SetGraphicsRootConstantBufferView(0,
                    m_pointSkinnedCB[fi]->GetGPUVirtualAddress() +
                    ((UINT64)slice * MAX_SKINNED_OBJECTS + c.skinSlot) * CB_SLOT_SIZE);
                cmd->SetGraphicsRootConstantBufferView(1,
                    m_bonePaletteCB[fi]->GetGPUVirtualAddress() + (UINT64)c.skinSlot * BONE_PALETTE_STRIDE);
                cmd->IASetVertexBuffers(0, 1, &c.smesh->VBView);
                cmd->IASetIndexBuffer(&c.smesh->IBView);
                for (const SubMesh& sm : c.smesh->SubMeshes)
                    cmd->DrawIndexedInstanced(sm.IndexCount, 1, sm.StartIndex, sm.BaseVertex, 0);
            }
        }
    }
}

bool ShadowPass::CreateSkinnedShadowBuffers(ID3D12Device* device) {
    D3D12_RANGE range = { 0, 0 };
    // Per-object CB: CASCADE_COUNT × MAX_SKINNED_OBJECTS slots
    {
        const UINT64 size = (UINT64)CASCADE_COUNT * MAX_SKINNED_OBJECTS * CB_SLOT_SIZE;
        D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC rd = {};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width = size;
        rd.Height = rd.DepthOrArraySize = rd.MipLevels = 1;
        rd.Format = DXGI_FORMAT_UNKNOWN; rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        for (uint32_t i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i) {
            if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_skinnedCB[i]))))
                return false;
            m_skinnedCB[i]->Map(0, &range, reinterpret_cast<void**>(&m_skinnedCBMapped[i]));
        }
    }
    // Bone palette CB: MAX_SKINNED_OBJECTS slots
    {
        const UINT64 size = (UINT64)MAX_SKINNED_OBJECTS * BONE_PALETTE_STRIDE;
        D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC rd = {};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width = size;
        rd.Height = rd.DepthOrArraySize = rd.MipLevels = 1;
        rd.Format = DXGI_FORMAT_UNKNOWN; rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        for (uint32_t i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i) {
            if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_bonePaletteCB[i]))))
                return false;
            m_bonePaletteCB[i]->Map(0, &range, reinterpret_cast<void**>(&m_bonePaletteMapped[i]));
        }
    }
    return true;
}

} // namespace Fujin
