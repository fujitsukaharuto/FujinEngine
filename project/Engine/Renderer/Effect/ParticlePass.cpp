#include "ParticlePass.h"
#include "Engine/Core/SceneManager.h"
#include "Engine/Core/Actor.h"
#include "Engine/Core/TransformComponent.h"
#include "Engine/Core/ParticleComponent.h"
#include "Engine/Asset/TextureManager.h"
#include "Engine/Asset/GeometryManager.h"
#include "Engine/Asset/MeshAsset.h"
#include "Engine/Graphics/DxcHelper.h"
#include <cmath>
#include <algorithm>
#include <vector>

namespace Fujin {

static constexpr float PI = 3.14159265f;

// Transform a point (w=1) by a world matrix — used to bring local-space particle positions to world.
static inline Vector3 XformPoint(const Matrix4x4& m, const Vector3& p) {
    Vector4 r = m * Vector4(p.x, p.y, p.z, 1.0f);
    return Vector3(r.x, r.y, r.z);
}

// ── Utility: create committed upload buffer and return persistent map ─────────
static bool CreateUploadBuffer(ID3D12Device* dev, UINT64 size,
                               ComPtr<ID3D12Resource>& buf, void** mapped) {
    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width            = size;
    rd.Height           = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels        = 1;
    rd.Format           = DXGI_FORMAT_UNKNOWN;
    rd.SampleDesc.Count = 1;
    rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&buf))))
        return false;
    D3D12_RANGE r = {};
    return SUCCEEDED(buf->Map(0, &r, mapped));
}

// ── Root signature (shared by sprite and beam — both only need b0) ────────────
static bool MakeRS(ID3D12Device* dev, ComPtr<ID3D12RootSignature>& rs) {
    // 0: CBV b0 (PerPass, VS)  1: 32-bit constants b1 (SubUV+HasTexture, VS+PS)  2: table t1 (sprite tex, PS)
    D3D12_DESCRIPTOR_RANGE texRange = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, 0 }; // t1
    D3D12_ROOT_PARAMETER params[3] = {};
    params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;
    params[1].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[1].Constants.ShaderRegister  = 1;
    params[1].Constants.Num32BitValues  = 4;
    params[1].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
    params[2].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].DescriptorTable.NumDescriptorRanges = 1;
    params[2].DescriptorTable.pDescriptorRanges   = &texRange;
    params[2].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC samp = {};
    samp.Filter   = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samp.AddressU = samp.AddressV = samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samp.MaxLOD   = D3D12_FLOAT32_MAX;
    samp.ShaderRegister   = 0;
    samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters     = 3;
    desc.pParameters       = params;
    desc.NumStaticSamplers = 1;
    desc.pStaticSamplers   = &samp;
    desc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> blob, err;
    D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
    return SUCCEEDED(dev->CreateRootSignature(0, blob->GetBufferPointer(),
                                              blob->GetBufferSize(), IID_PPV_ARGS(&rs)));
}

// ── DEFAULT heap buffer (UAV capable) ─────────────────────────────────────────
static bool CreateDefaultBuffer(ID3D12Device* dev, UINT64 size,
                                ComPtr<ID3D12Resource>& buf) {
    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width            = size;
    rd.Height           = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels        = 1;
    rd.Format           = DXGI_FORMAT_UNKNOWN;
    rd.SampleDesc.Count = 1;
    rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    rd.Flags            = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    return SUCCEEDED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&buf)));
}

static void TransitionResource(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res,
                               D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER b = {};
    b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource   = res;
    b.Transition.StateBefore = from;
    b.Transition.StateAfter  = to;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd->ResourceBarrier(1, &b);
}

static float RandfGPU() { return rand() / (float)RAND_MAX; }
static float RandRangeGPU(float lo, float hi) { return lo + RandfGPU() * (hi - lo); }

static Vector3 SampleGPUSpawnPos(const SpawnModule& sp, const Vector3& worldPos) {
    switch (sp.Shape) {
    case EmitterShape::Sphere: {
        float u = RandfGPU(), v = RandfGPU();
        float theta = 2.0f * PI * u;
        float phi   = std::acos(2.0f * v - 1.0f);
        float r     = sp.ShapeRadius * std::cbrtf(RandfGPU());
        return worldPos + Vector3(
            r * std::sin(phi) * std::cos(theta),
            r * std::cos(phi),
            r * std::sin(phi) * std::sin(theta));
    }
    case EmitterShape::Box:
        return worldPos + Vector3(
            RandRangeGPU(-sp.ShapeExtent.x, sp.ShapeExtent.x),
            RandRangeGPU(-sp.ShapeExtent.y, sp.ShapeExtent.y),
            RandRangeGPU(-sp.ShapeExtent.z, sp.ShapeExtent.z));
    case EmitterShape::Cone: {
        float t        = RandfGPU() * sp.ShapeRadius;
        float phiAngle = RandfGPU() * 2.0f * PI;
        float halfAng  = sp.ConeAngleDeg * PI / 180.0f;
        float r        = t * std::tanf(halfAng);
        Vector3 up     = sp.EmitDir.GetSafeNormal();
        Vector3 tang   = (std::abs(up.x) < 0.9f)
            ? Vector3::Cross(up, Vector3(1,0,0)).GetSafeNormal()
            : Vector3::Cross(up, Vector3(0,1,0)).GetSafeNormal();
        Vector3 bitan  = Vector3::Cross(up, tang);
        return worldPos + up * t + (tang * std::cos(phiAngle) + bitan * std::sin(phiAngle)) * r;
    }
    default:
        return worldPos;
    }
}

static bool InitParamsEqual(const InitModule& a, const InitModule& b) {
    return a.SizeMin == b.SizeMin && a.SizeMax == b.SizeMax &&
           a.LifeMin == b.LifeMin && a.LifeMax == b.LifeMax &&
           a.VelMin.x == b.VelMin.x && a.VelMin.y == b.VelMin.y && a.VelMin.z == b.VelMin.z &&
           a.VelMax.x == b.VelMax.x && a.VelMax.y == b.VelMax.y && a.VelMax.z == b.VelMax.z &&
           a.ColorStart.x == b.ColorStart.x && a.ColorStart.y == b.ColorStart.y &&
           a.ColorStart.z == b.ColorStart.z && a.ColorStart.w == b.ColorStart.w &&
           a.ColorEnd.x == b.ColorEnd.x && a.ColorEnd.y == b.ColorEnd.y &&
           a.ColorEnd.z == b.ColorEnd.z && a.ColorEnd.w == b.ColorEnd.w &&
           a.ColorMid.x == b.ColorMid.x && a.ColorMid.y == b.ColorMid.y &&
           a.ColorMid.z == b.ColorMid.z && a.ColorMid.w == b.ColorMid.w &&
           a.UseColorMid == b.UseColorMid &&
           a.RotRateMin == b.RotRateMin && a.RotRateMax == b.RotRateMax;
}

static void UAVBarrier(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res) {
    D3D12_RESOURCE_BARRIER b = {};
    b.Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    b.UAV.pResource = res;
    cmd->ResourceBarrier(1, &b);
}

// ── Blend state: standard alpha blend ────────────────────────────────────────
static D3D12_BLEND_DESC AlphaBlendDesc() {
    D3D12_BLEND_DESC d = {};
    d.RenderTarget[0].BlendEnable           = TRUE;
    d.RenderTarget[0].SrcBlend              = D3D12_BLEND_SRC_ALPHA;
    d.RenderTarget[0].DestBlend             = D3D12_BLEND_INV_SRC_ALPHA;
    d.RenderTarget[0].BlendOp               = D3D12_BLEND_OP_ADD;
    d.RenderTarget[0].SrcBlendAlpha         = D3D12_BLEND_ONE;
    d.RenderTarget[0].DestBlendAlpha        = D3D12_BLEND_INV_SRC_ALPHA;
    d.RenderTarget[0].BlendOpAlpha          = D3D12_BLEND_OP_ADD;
    d.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    return d;
}

// ── Blend state: additive ─────────────────────────────────────────────────────
static D3D12_BLEND_DESC AdditiveBlendDesc() {
    D3D12_BLEND_DESC d = {};
    d.RenderTarget[0].BlendEnable           = TRUE;
    d.RenderTarget[0].SrcBlend              = D3D12_BLEND_SRC_ALPHA;
    d.RenderTarget[0].DestBlend             = D3D12_BLEND_ONE;
    d.RenderTarget[0].BlendOp               = D3D12_BLEND_OP_ADD;
    d.RenderTarget[0].SrcBlendAlpha         = D3D12_BLEND_ONE;
    d.RenderTarget[0].DestBlendAlpha        = D3D12_BLEND_ONE;
    d.RenderTarget[0].BlendOpAlpha          = D3D12_BLEND_OP_ADD;
    d.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    return d;
}

// ── PSO helpers ──────────────────────────────────────────────────────────────
bool ParticlePass::CreateSpritePipeline(GraphicsDevice& gfx) {
    auto* dev = gfx.GetDevice();
    if (!MakeRS(dev, m_spriteRS)) return false;

    auto vs = LoadOrCompileShader(L"Resource/Shaders/Particle.VS.hlsl", L"vs_6_0");
    auto ps = LoadOrCompileShader(L"Resource/Shaders/Particle.PS.hlsl", L"ps_6_0");

    D3D12_INPUT_ELEMENT_DESC layout[] = {
        // Slot 0: per-vertex
        { "POSITION",  0, DXGI_FORMAT_R32G32_FLOAT,          0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,   0 },
        { "TEXCOORD",  0, DXGI_FORMAT_R32G32_FLOAT,          0,  8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,   0 },
        // Slot 1: per-instance (InstanceVert: 48 bytes)
        { "INST_POS",  0, DXGI_FORMAT_R32G32B32_FLOAT,       1,  0, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        { "INST_SIZE", 0, DXGI_FORMAT_R32_FLOAT,             1, 12, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        { "INST_ROT",  0, DXGI_FORMAT_R32_FLOAT,             1, 16, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        { "INST_PAD",  0, DXGI_FORMAT_R32G32B32_FLOAT,       1, 20, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        { "INST_COL",  0, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 32, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
    };

    D3D12_RASTERIZER_DESC rast = {};
    rast.FillMode        = D3D12_FILL_MODE_SOLID;
    rast.CullMode        = D3D12_CULL_MODE_NONE;   // billboards: no cull
    rast.DepthClipEnable = TRUE;

    D3D12_DEPTH_STENCIL_DESC ds = {};
    ds.DepthEnable    = TRUE;
    ds.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; // transparent: no depth write
    ds.DepthFunc      = D3D12_COMPARISON_FUNC_LESS;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
    pso.pRootSignature        = m_spriteRS.Get();
    pso.VS                    = { vs->GetBufferPointer(), vs->GetBufferSize() };
    pso.PS                    = { ps->GetBufferPointer(), ps->GetBufferSize() };
    pso.InputLayout           = { layout, 7 };
    pso.RasterizerState       = rast;
    pso.BlendState            = AlphaBlendDesc();
    pso.DepthStencilState     = ds;
    pso.DSVFormat             = DXGI_FORMAT_D32_FLOAT;
    pso.SampleMask            = UINT_MAX;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets      = 1;
    pso.RTVFormats[0]         = DXGI_FORMAT_R16G16B16A16_FLOAT;
    pso.SampleDesc            = { 1, 0 };

    if (FAILED(dev->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_spritePSO)))) return false;
    pso.BlendState = AdditiveBlendDesc();
    return SUCCEEDED(dev->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_spriteAddPSO)));
}

bool ParticlePass::CreateMeshPipeline(GraphicsDevice& gfx) {
    auto* dev = gfx.GetDevice();

    // Minimal root sig: CBV b0 (PerPass, VS). No texture (unlit/emissive, color from instance).
    {
        D3D12_ROOT_PARAMETER p = {};
        p.ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
        p.Descriptor.ShaderRegister = 0;
        p.ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;
        D3D12_ROOT_SIGNATURE_DESC d = {};
        d.NumParameters = 1; d.pParameters = &p;
        d.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        ComPtr<ID3DBlob> blob, err;
        D3D12SerializeRootSignature(&d, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
        if (FAILED(dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                            IID_PPV_ARGS(&m_meshRS)))) return false;
    }

    auto vs = LoadOrCompileShader(L"Resource/Shaders/MeshParticle.VS.hlsl", L"vs_6_0");
    auto ps = LoadOrCompileShader(L"Resource/Shaders/MeshParticle.PS.hlsl", L"ps_6_0");

    D3D12_INPUT_ELEMENT_DESC layout[] = {
        // Slot 0: mesh vertex (MeshVertex: pos@0, normal@12, tangent@24, uv@36; stride 44)
        { "POSITION",  0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,   0 },
        { "NORMAL",    0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,   0 },
        { "TANGENT",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,   0 },
        { "TEXCOORD",  0, DXGI_FORMAT_R32G32_FLOAT,       0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,   0 },
        // Slot 1: per-instance (InstanceVert: 48 bytes)
        { "INST_POS",  0, DXGI_FORMAT_R32G32B32_FLOAT,    1,  0, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        { "INST_SIZE", 0, DXGI_FORMAT_R32_FLOAT,          1, 12, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        { "INST_ROT",  0, DXGI_FORMAT_R32_FLOAT,          1, 16, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        { "INST_PAD",  0, DXGI_FORMAT_R32G32B32_FLOAT,    1, 20, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        { "INST_COL",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
    };

    D3D12_RASTERIZER_DESC rast = {};
    rast.FillMode        = D3D12_FILL_MODE_SOLID;
    rast.CullMode        = D3D12_CULL_MODE_BACK;
    rast.DepthClipEnable = TRUE;

    D3D12_DEPTH_STENCIL_DESC ds = {};
    ds.DepthEnable    = TRUE;
    ds.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;   // solid meshes: write depth
    ds.DepthFunc      = D3D12_COMPARISON_FUNC_LESS;

    D3D12_BLEND_DESC blend = {};
    blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;  // opaque

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
    pso.pRootSignature        = m_meshRS.Get();
    pso.VS                    = { vs->GetBufferPointer(), vs->GetBufferSize() };
    pso.PS                    = { ps->GetBufferPointer(), ps->GetBufferSize() };
    pso.InputLayout           = { layout, 9 };
    pso.RasterizerState       = rast;
    pso.BlendState            = blend;
    pso.DepthStencilState     = ds;
    pso.DSVFormat             = DXGI_FORMAT_D32_FLOAT;
    pso.SampleMask            = UINT_MAX;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets      = 1;
    pso.RTVFormats[0]         = DXGI_FORMAT_R16G16B16A16_FLOAT;
    pso.SampleDesc            = { 1, 0 };
    return SUCCEEDED(dev->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_meshPSO)));
}

bool ParticlePass::CreateBeamPipeline(GraphicsDevice& gfx) {
    auto* dev = gfx.GetDevice();
    if (!MakeRS(dev, m_beamRS)) return false;

    auto vs = LoadOrCompileShader(L"Resource/Shaders/Beam.VS.hlsl", L"vs_6_0");
    auto ps = LoadOrCompileShader(L"Resource/Shaders/Beam.PS.hlsl", L"ps_6_0");

    D3D12_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_RASTERIZER_DESC rast = {};
    rast.FillMode        = D3D12_FILL_MODE_SOLID;
    rast.CullMode        = D3D12_CULL_MODE_NONE;
    rast.DepthClipEnable = TRUE;

    D3D12_DEPTH_STENCIL_DESC ds = {};
    ds.DepthEnable    = TRUE;
    ds.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    ds.DepthFunc      = D3D12_COMPARISON_FUNC_LESS;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
    pso.pRootSignature        = m_beamRS.Get();
    pso.VS                    = { vs->GetBufferPointer(), vs->GetBufferSize() };
    pso.PS                    = { ps->GetBufferPointer(), ps->GetBufferSize() };
    pso.InputLayout           = { layout, 3 };
    pso.RasterizerState       = rast;
    pso.BlendState            = AlphaBlendDesc();
    pso.DepthStencilState     = ds;
    pso.DSVFormat             = DXGI_FORMAT_D32_FLOAT;
    pso.SampleMask            = UINT_MAX;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets      = 1;
    pso.RTVFormats[0]         = DXGI_FORMAT_R16G16B16A16_FLOAT;
    pso.SampleDesc            = { 1, 0 };

    if (FAILED(dev->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_beamPSO)))) return false;
    pso.BlendState = AdditiveBlendDesc();
    return SUCCEEDED(dev->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_beamAddPSO)));
}

bool ParticlePass::CreateBuffers(GraphicsDevice& gfx) {
    auto* dev = gfx.GetDevice();

    // ── Static quad: 4 verts, 6 indices ─────────────────────────────────────
    struct QuadVert { float xy[2]; float uv[2]; };
    QuadVert quad[4] = {
        {{-0.5f,  0.5f}, {0.0f, 0.0f}},   // TL
        {{ 0.5f,  0.5f}, {1.0f, 0.0f}},   // TR
        {{ 0.5f, -0.5f}, {1.0f, 1.0f}},   // BR
        {{-0.5f, -0.5f}, {0.0f, 1.0f}},   // BL
    };
    uint16_t idx[6] = { 0, 1, 2, 0, 2, 3 };

    void* tmp = nullptr;
    if (!CreateUploadBuffer(dev, sizeof(quad), m_quadVB, &tmp)) return false;
    memcpy(tmp, quad, sizeof(quad));

    if (!CreateUploadBuffer(dev, sizeof(idx), m_quadIB, &tmp)) return false;
    memcpy(tmp, idx, sizeof(idx));

    // ── Dynamic instance / beam buffers per frame ────────────────────────────
    for (uint32_t i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i) {
        void* p = nullptr;
        if (!CreateUploadBuffer(dev, sizeof(InstanceVert) * MAX_SPRITES,
                                m_instanceVB[i], &p)) return false;
        m_instanceMapped[i] = reinterpret_cast<InstanceVert*>(p);

        if (!CreateUploadBuffer(dev, sizeof(InstanceVert) * MAX_SPRITES,
                                m_meshInstanceVB[i], &p)) return false;
        m_meshInstanceMapped[i] = reinterpret_cast<InstanceVert*>(p);

        if (!CreateUploadBuffer(dev, sizeof(BeamVert) * MAX_BEAM_VERTS,
                                m_beamVB_buf[i], &p)) return false;
        m_beamMapped[i] = reinterpret_cast<BeamVert*>(p);

        if (!CreateUploadBuffer(dev, PASS_CB_SIZE,
                                m_passCB[i], &p)) return false;
        m_passMapped[i] = reinterpret_cast<uint8_t*>(p);
    }
    return true;
}

// ── GPU Compute pipelines ─────────────────────────────────────────────────────

bool ParticlePass::CreateGPUComputePipelines(GraphicsDevice& gfx) {
    auto* dev = gfx.GetDevice();

    // Update CS root signature: [0]=CBV b0, [1]=UAV u0 (particles), [2]=t0 depth, [3]=t1 normal
    {
        D3D12_DESCRIPTOR_RANGE depthRange  = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0 }; // t0
        D3D12_DESCRIPTOR_RANGE normalRange = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, 0 }; // t1
        D3D12_ROOT_PARAMETER params[4] = {};
        params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[0].Descriptor.ShaderRegister = 0;
        params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        params[1].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[1].Descriptor.ShaderRegister = 0;
        params[1].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        params[2].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[2].DescriptorTable.NumDescriptorRanges = 1;
        params[2].DescriptorTable.pDescriptorRanges   = &depthRange;
        params[2].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;
        params[3].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[3].DescriptorTable.NumDescriptorRanges = 1;
        params[3].DescriptorTable.pDescriptorRanges   = &normalRange;
        params[3].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;
        D3D12_STATIC_SAMPLER_DESC samp = {};
        samp.Filter   = D3D12_FILTER_MIN_MAG_MIP_POINT;
        samp.AddressU = samp.AddressV = samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samp.MaxLOD   = D3D12_FLOAT32_MAX;
        samp.ShaderRegister = 0;
        samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        D3D12_ROOT_SIGNATURE_DESC desc = {};
        desc.NumParameters     = 4;
        desc.pParameters       = params;
        desc.NumStaticSamplers = 1;
        desc.pStaticSamplers   = &samp;
        ComPtr<ID3DBlob> blob, err;
        D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
        if (FAILED(dev->CreateRootSignature(0, blob->GetBufferPointer(),
                blob->GetBufferSize(), IID_PPV_ARGS(&m_gpuUpdateRS)))) return false;
        auto cs = LoadOrCompileShader(L"Resource/Shaders/ParticleGPU_Update.CS.hlsl", L"cs_6_0");
        D3D12_COMPUTE_PIPELINE_STATE_DESC pso = {};
        pso.pRootSignature = m_gpuUpdateRS.Get();
        pso.CS             = { cs->GetBufferPointer(), cs->GetBufferSize() };
        if (FAILED(dev->CreateComputePipelineState(&pso, IID_PPV_ARGS(&m_gpuUpdatePSO)))) return false;
    }

    // Spawn CS root signature: [0]=CBV b0, [1]=UAV u0 (particles), [2]=UAV u1 (writeHead), [3]=SRV t0 (spawnData)
    {
        D3D12_ROOT_PARAMETER params[4] = {};
        params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[0].Descriptor.ShaderRegister = 0;
        params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        params[1].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[1].Descriptor.ShaderRegister = 0;
        params[1].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        params[2].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[2].Descriptor.ShaderRegister = 1;
        params[2].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        params[3].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[3].Descriptor.ShaderRegister = 0;
        params[3].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        D3D12_ROOT_SIGNATURE_DESC desc = {};
        desc.NumParameters = 4;
        desc.pParameters   = params;
        ComPtr<ID3DBlob> blob, err;
        D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
        if (FAILED(dev->CreateRootSignature(0, blob->GetBufferPointer(),
                blob->GetBufferSize(), IID_PPV_ARGS(&m_gpuSpawnRS)))) return false;
        auto cs = LoadOrCompileShader(L"Resource/Shaders/ParticleGPU_Spawn.CS.hlsl", L"cs_6_0");
        D3D12_COMPUTE_PIPELINE_STATE_DESC pso = {};
        pso.pRootSignature = m_gpuSpawnRS.Get();
        pso.CS             = { cs->GetBufferPointer(), cs->GetBufferSize() };
        if (FAILED(dev->CreateComputePipelineState(&pso, IID_PPV_ARGS(&m_gpuSpawnPSO)))) return false;
    }
    return true;
}

bool ParticlePass::CreateGPUDrawPipeline(GraphicsDevice& gfx) {
    auto* dev = gfx.GetDevice();

    // [0]=CBV b0 (VS), [1]=SRV t0 particle buffer (VS), [2]=32-bit consts b1 (SubUV, VS+PS),
    // [3]=table t1 sprite texture (PS)
    D3D12_DESCRIPTOR_RANGE texRange = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, 0 }; // t1
    D3D12_ROOT_PARAMETER params[4] = {};
    params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;
    params[1].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[1].Descriptor.ShaderRegister = 0;
    params[1].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;
    params[2].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[2].Constants.ShaderRegister  = 1;
    params[2].Constants.Num32BitValues  = 4;
    params[2].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
    params[3].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[3].DescriptorTable.NumDescriptorRanges = 1;
    params[3].DescriptorTable.pDescriptorRanges   = &texRange;
    params[3].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC samp = {};
    samp.Filter   = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samp.AddressU = samp.AddressV = samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samp.MaxLOD   = D3D12_FLOAT32_MAX;
    samp.ShaderRegister   = 0;
    samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters     = 4;
    rsDesc.pParameters       = params;
    rsDesc.NumStaticSamplers = 1;
    rsDesc.pStaticSamplers   = &samp;
    ComPtr<ID3DBlob> blob, err;
    D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
    if (FAILED(dev->CreateRootSignature(0, blob->GetBufferPointer(),
            blob->GetBufferSize(), IID_PPV_ARGS(&m_gpuDrawRS)))) return false;

    auto vs = LoadOrCompileShader(L"Resource/Shaders/ParticleGPU.VS.hlsl",  L"vs_6_0");
    auto ps = LoadOrCompileShader(L"Resource/Shaders/Particle.PS.hlsl",      L"ps_6_0");

    D3D12_RASTERIZER_DESC rast = {};
    rast.FillMode        = D3D12_FILL_MODE_SOLID;
    rast.CullMode        = D3D12_CULL_MODE_NONE;
    rast.DepthClipEnable = TRUE;

    D3D12_DEPTH_STENCIL_DESC ds = {};
    ds.DepthEnable    = TRUE;
    ds.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    ds.DepthFunc      = D3D12_COMPARISON_FUNC_LESS;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
    pso.pRootSignature        = m_gpuDrawRS.Get();
    pso.VS                    = { vs->GetBufferPointer(), vs->GetBufferSize() };
    pso.PS                    = { ps->GetBufferPointer(), ps->GetBufferSize() };
    pso.InputLayout           = { nullptr, 0 };  // no IA — VS reads from SRV
    pso.RasterizerState       = rast;
    pso.BlendState            = AlphaBlendDesc();
    pso.DepthStencilState     = ds;
    pso.DSVFormat             = DXGI_FORMAT_D32_FLOAT;
    pso.SampleMask            = UINT_MAX;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets      = 1;
    pso.RTVFormats[0]         = DXGI_FORMAT_R16G16B16A16_FLOAT;
    pso.SampleDesc            = { 1, 0 };
    if (FAILED(dev->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_gpuDrawPSO)))) return false;
    pso.BlendState = AdditiveBlendDesc();
    return SUCCEEDED(dev->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_gpuDrawAddPSO)));
}

// ── GPU emitter lazy init ─────────────────────────────────────────────────────

bool ParticlePass::InitGPUEmitter(ID3D12Device* dev, uint64_t key, uint32_t maxParticles) {
    GPUEmitterState& s = m_gpuEmitters[key];
    s.maxParticles = maxParticles;
    s.particleState = D3D12_RESOURCE_STATE_COMMON;

    // Particle buffer: maxParticles × 96 bytes
    if (!CreateDefaultBuffer(dev, (UINT64)maxParticles * 96, s.particleBuf)) return false;
    // Write head: single uint (4 bytes) for ring buffer pointer
    if (!CreateDefaultBuffer(dev, 4, s.writeHead)) return false;

    UINT64 spawnUploadSize = (UINT64)maxParticles * sizeof(GPUSpawnData);
    for (uint32_t i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i) {
        void* p = nullptr;
        // 768B = SpawnParams at offset 0 (256B region) + UpdateParams at offset 256 (304B, padded).
        if (!CreateUploadBuffer(dev, 768, s.computeCB[i], &p)) return false;
        s.computeCBMapped[i] = reinterpret_cast<uint8_t*>(p);

        if (!CreateUploadBuffer(dev, spawnUploadSize, s.spawnUpload[i], &p)) return false;
        s.spawnMapped[i] = reinterpret_cast<GPUSpawnData*>(p);
    }
    return true;
}

// ── Initialize / Shutdown ────────────────────────────────────────────────────

bool ParticlePass::Initialize(GraphicsDevice& gfx) {
    if (!CreateSpritePipeline(gfx))       return false;
    if (!CreateBeamPipeline(gfx))         return false;
    if (!CreateMeshPipeline(gfx))         return false;
    if (!CreateBuffers(gfx))              return false;
    if (!CreateGPUComputePipelines(gfx))  return false;
    if (!CreateGPUDrawPipeline(gfx))      return false;
    return true;
}

void ParticlePass::Shutdown() {
    m_spritePSO.Reset(); m_spriteAddPSO.Reset(); m_spriteRS.Reset();
    m_beamPSO.Reset();   m_beamAddPSO.Reset();   m_beamRS.Reset();
    m_meshPSO.Reset();   m_meshRS.Reset();
    m_gpuSpawnPSO.Reset();  m_gpuSpawnRS.Reset();
    m_gpuUpdatePSO.Reset(); m_gpuUpdateRS.Reset();
    m_gpuDrawPSO.Reset();   m_gpuDrawAddPSO.Reset(); m_gpuDrawRS.Reset();
    m_quadVB.Reset();    m_quadIB.Reset();
    for (uint32_t i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i) {
        m_instanceVB[i].Reset();
        m_meshInstanceVB[i].Reset();
        m_beamVB_buf[i].Reset();
        m_passCB[i].Reset();
    }
    m_gpuEmitters.clear();
}

// ── Execute ──────────────────────────────────────────────────────────────────

void ParticlePass::Execute(ID3D12GraphicsCommandList* cmd,
                            GraphicsDevice& gfx,
                            const SceneManager& scene,
                            TextureManager& texMgr,
                            GeometryManager& geoMgr,
                            const Matrix4x4& viewProj,
                            const Matrix4x4& view,
                            uint32_t vpX, uint32_t vpY,
                            uint32_t vpW, uint32_t vpH,
                            uint32_t frameIndex,
                            float elapsed,
                            float dt,
                            ID3D12Resource* normalRes,
                            uint32_t normalSRVSlot)
{
    if (vpW == 0 || vpH == 0) return;

    // Extract camera right/up from view matrix (row-major: rows are camera axes)
    Vector3 camRight(view.m[0][0], view.m[0][1], view.m[0][2]);
    Vector3 camUp   (view.m[1][0], view.m[1][1], view.m[1][2]);
    Vector3 camPos  (view.m[3][0], view.m[3][1], view.m[3][2]);

    // Upload pass CB
    PassCB cb = {};
    memcpy(cb.viewProj, &viewProj.m[0][0], 64);
    cb.camRight[0] = camRight.x; cb.camRight[1] = camRight.y; cb.camRight[2] = camRight.z;
    cb.camUp[0]    = camUp.x;    cb.camUp[1]    = camUp.y;    cb.camUp[2]    = camUp.z;
    memcpy(m_passMapped[frameIndex], &cb, sizeof(cb));

    // Viewport + scissor
    D3D12_VIEWPORT vp = { (float)vpX, (float)vpY, (float)vpW, (float)vpH, 0, 1 };
    D3D12_RECT     sc = { (LONG)vpX, (LONG)vpY, (LONG)(vpX+vpW), (LONG)(vpY+vpH) };
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &sc);

    // GPU particles first (compute dispatches happen before graphics)
    DrawGPUSprites(cmd, gfx, scene, texMgr, frameIndex, dt, elapsed,
                   viewProj, vpX, vpY, vpW, vpH, normalRes, normalSRVSlot);

    // CPU particles
    DrawSprites(cmd, gfx, frameIndex, scene, texMgr);
    DrawMeshParticles(cmd, frameIndex, scene, geoMgr);
    uint32_t beamVtxUsed = DrawBeams(cmd, frameIndex, scene, camPos, elapsed);
    DrawRibbons(cmd, frameIndex, scene, camPos, beamVtxUsed);
}

// ── Sprite draw ──────────────────────────────────────────────────────────────

void ParticlePass::DrawSprites(ID3D12GraphicsCommandList* cmd, GraphicsDevice& gfx, uint32_t fi,
                                const SceneManager& scene, TextureManager& texMgr) {
    InstanceVert* dst = m_instanceMapped[fi];

    // Per-emitter draw items: each emitter has its own texture + SubUV grid, so it can't be batched
    // with others into a single draw. We pack all emitters' instances into one buffer (running
    // offset) and issue one DrawIndexedInstanced per emitter with its texture + SubUV root constants.
    struct Item { uint32_t offset, count; BlendMode blend; uint32_t texSlot; int cols, rows, hasTex; };
    std::vector<Item> items;
    uint32_t total = 0;
    for (auto& actorPtr : scene.GetActors()) {
        auto* pc = actorPtr->GetComponent<ParticleComponent>();
        if (!pc) continue;
        auto* tc = actorPtr->GetComponent<TransformComponent>();
        Matrix4x4 world = tc ? tc->GetWorldMatrix() : Matrix4x4::Identity;
        for (auto& em : pc->GetEmitters()) {
            const EmitterDesc& d = em.GetDesc();
            if (d.RenderMode != EmitterRenderMode::Sprite) continue;
            float ei = d.EmissiveIntensity;
            uint32_t start = total, n = 0;
            for (auto& p : em.GetParticles()) {
                if (!p.Active || total >= MAX_SPRITES) continue;
                // Local-space emitters store positions relative to the emitter; transform to world.
                Vector3 wp = d.LocalSpace ? XformPoint(world, p.Position) : p.Position;
                auto& iv  = dst[total];
                iv.pos[0] = wp.x; iv.pos[1] = wp.y; iv.pos[2] = wp.z;
                iv.size   = p.Size;
                iv.rot    = p.Rotation * 3.14159f / 180.0f;
                iv.pad[0] = (p.Lifetime > 1e-4f) ? (p.Age / p.Lifetime) : 0.0f;   // ageFrac → flipbook
                iv.pad[1] = iv.pad[2] = 0.0f;
                iv.color[0] = p.Color.x * ei; iv.color[1] = p.Color.y * ei;
                iv.color[2] = p.Color.z * ei; iv.color[3] = p.Color.w;
                ++total; ++n;
            }
            if (n == 0) continue;
            bool hasTex = !d.SpriteTexturePath.empty();
            uint32_t texSlot = hasTex ? texMgr.LoadTexture(d.SpriteTexturePath) : texMgr.GetFallbackSlot();
            items.push_back({ start, n, d.Blend, texSlot, d.SubUVCols, d.SubUVRows, hasTex ? 1 : 0 });
        }
    }
    if (items.empty()) return;

    D3D12_VERTEX_BUFFER_VIEW vbvs[2] = {};
    vbvs[0].BufferLocation = m_quadVB->GetGPUVirtualAddress();
    vbvs[0].SizeInBytes    = sizeof(float) * 4 * 4;
    vbvs[0].StrideInBytes  = sizeof(float) * 4;
    vbvs[1].BufferLocation = m_instanceVB[fi]->GetGPUVirtualAddress();
    vbvs[1].SizeInBytes    = sizeof(InstanceVert) * total;
    vbvs[1].StrideInBytes  = sizeof(InstanceVert);

    D3D12_INDEX_BUFFER_VIEW ibv = {};
    ibv.BufferLocation = m_quadIB->GetGPUVirtualAddress();
    ibv.SizeInBytes    = sizeof(uint16_t) * 6;
    ibv.Format         = DXGI_FORMAT_R16_UINT;

    ID3D12DescriptorHeap* heaps[] = { gfx.GetSRVHeap() };
    cmd->SetDescriptorHeaps(1, heaps);
    cmd->SetGraphicsRootSignature(m_spriteRS.Get());
    cmd->SetGraphicsRootConstantBufferView(0, m_passCB[fi]->GetGPUVirtualAddress());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->IASetVertexBuffers(0, 2, vbvs);
    cmd->IASetIndexBuffer(&ibv);

    for (const Item& it : items) {
        cmd->SetPipelineState(it.blend == BlendMode::Additive ? m_spriteAddPSO.Get() : m_spritePSO.Get());
        int subuv[4] = { it.cols, it.rows, it.hasTex, 0 };
        cmd->SetGraphicsRoot32BitConstants(1, 4, subuv, 0);
        cmd->SetGraphicsRootDescriptorTable(2, gfx.GetSRVGPUHandle(it.texSlot));
        cmd->DrawIndexedInstanced(6, it.count, 0, 0, it.offset);
    }
}

// ── Mesh-particle draw ───────────────────────────────────────────────────────

void ParticlePass::DrawMeshParticles(ID3D12GraphicsCommandList* cmd, uint32_t fi,
                                      const SceneManager& scene, GeometryManager& geoMgr) {
    InstanceVert* dst = m_meshInstanceMapped[fi];
    struct Item { const MeshAsset* mesh; uint32_t offset, count; };
    std::vector<Item> items;
    uint32_t total = 0;
    for (auto& actorPtr : scene.GetActors()) {
        auto* pc = actorPtr->GetComponent<ParticleComponent>();
        if (!pc) continue;
        auto* tc = actorPtr->GetComponent<TransformComponent>();
        Matrix4x4 world = tc ? tc->GetWorldMatrix() : Matrix4x4::Identity;
        for (auto& em : pc->GetEmitters()) {
            const EmitterDesc& d = em.GetDesc();
            if (d.RenderMode != EmitterRenderMode::Mesh || d.MeshPath.empty()) continue;
            const MeshAsset* mesh = geoMgr.LoadMesh(d.MeshPath);
            if (!mesh || mesh->SubMeshes.empty()) continue;
            float ei = d.EmissiveIntensity;
            uint32_t start = total, n = 0;
            for (auto& p : em.GetParticles()) {
                if (!p.Active || total >= MAX_SPRITES) continue;
                Vector3 wp = d.LocalSpace ? XformPoint(world, p.Position) : p.Position;
                auto& iv  = dst[total];
                iv.pos[0] = wp.x; iv.pos[1] = wp.y; iv.pos[2] = wp.z;
                iv.size   = p.Size;
                iv.rot    = p.Rotation * 3.14159f / 180.0f;
                iv.pad[0] = iv.pad[1] = iv.pad[2] = 0.0f;
                iv.color[0] = p.Color.x * ei; iv.color[1] = p.Color.y * ei;
                iv.color[2] = p.Color.z * ei; iv.color[3] = p.Color.w;
                ++total; ++n;
            }
            if (n > 0) items.push_back({ mesh, start, n });
        }
    }
    if (items.empty()) return;

    cmd->SetGraphicsRootSignature(m_meshRS.Get());
    cmd->SetGraphicsRootConstantBufferView(0, m_passCB[fi]->GetGPUVirtualAddress());
    cmd->SetPipelineState(m_meshPSO.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    D3D12_VERTEX_BUFFER_VIEW instVbv = {};
    instVbv.BufferLocation = m_meshInstanceVB[fi]->GetGPUVirtualAddress();
    instVbv.SizeInBytes    = sizeof(InstanceVert) * total;
    instVbv.StrideInBytes  = sizeof(InstanceVert);

    for (const Item& it : items) {
        D3D12_VERTEX_BUFFER_VIEW vbvs[2] = { it.mesh->VBView, instVbv };
        cmd->IASetVertexBuffers(0, 2, vbvs);
        cmd->IASetIndexBuffer(&it.mesh->IBView);
        for (const SubMesh& sm : it.mesh->SubMeshes)
            cmd->DrawIndexedInstanced(sm.IndexCount, it.count, sm.StartIndex, sm.BaseVertex, it.offset);
    }
}

// ── Beam draw ────────────────────────────────────────────────────────────────

uint32_t ParticlePass::DrawBeams(ID3D12GraphicsCommandList* cmd, uint32_t fi,
                                  const SceneManager& scene,
                                  const Vector3& camPos, float elapsed) {
    BeamVert* dst = m_beamMapped[fi];

    auto FillBeams = [&](BlendMode targetBlend, uint32_t startOffset) -> uint32_t {
        uint32_t vtxCount = 0;
        for (auto& actorPtr : scene.GetActors()) {
            auto* pc = actorPtr->GetComponent<ParticleComponent>();
            if (!pc) continue;
            auto* tc = actorPtr->GetComponent<TransformComponent>();
            Vector3 origin = tc ? tc->GetWorldMatrix().GetTranslation() : Vector3{};

            for (auto& em : pc->GetEmitters()) {
                if (em.GetDesc().RenderMode != EmitterRenderMode::Beam) continue;
                if (em.GetDesc().Blend != targetBlend) continue;
                const auto& bm = em.GetDesc().Beam;
                int   N   = (std::max)(2, bm.Segments);
                float ei  = em.GetDesc().EmissiveIntensity;
                float col[4] = { bm.Color.x * ei, bm.Color.y * ei, bm.Color.z * ei, bm.Color.w };

                std::vector<Vector3> pts(N + 1);
                for (int i = 0; i <= N; ++i) {
                    float t = static_cast<float>(i) / static_cast<float>(N);
                    pts[i]  = Vector3(
                        bm.Start.x + (bm.End.x - bm.Start.x) * t,
                        bm.Start.y + (bm.End.y - bm.Start.y) * t,
                        bm.Start.z + (bm.End.z - bm.Start.z) * t) + origin;
                    if (i > 0 && i < N) {
                        float phase = elapsed * bm.NoiseSpeed + t * 6.28f;
                        float nx = std::sin(phase * 1.7f + t * 3.0f) * bm.NoiseAmp;
                        float nz = std::cos(phase * 2.3f + t * 4.0f) * bm.NoiseAmp;
                        pts[i].x += nx;
                        pts[i].z += nz;
                    }
                }
                for (int i = 0; i < N; ++i) {
                    if (startOffset + vtxCount + 6 > MAX_BEAM_VERTS) break;
                    Vector3 p0 = pts[i], p1 = pts[i + 1];
                    Vector3 seg = (p1 - p0).GetSafeNormal();
                    Vector3 mid = Vector3((p0.x+p1.x)*0.5f, (p0.y+p1.y)*0.5f, (p0.z+p1.z)*0.5f);
                    Vector3 toCam = (camPos - mid).GetSafeNormal();
                    Vector3 perp  = Vector3::Cross(seg, toCam).GetSafeNormal() * (bm.Width * 0.5f);
                    auto PV = [&](Vector3 p, float u, float v) -> BeamVert {
                        return { {p.x, p.y, p.z}, u, v, {col[0], col[1], col[2], col[3]} };
                    };
                    Vector3 a = p0 - perp, b = p0 + perp;
                    Vector3 c = p1 - perp, d = p1 + perp;
                    float u0 = static_cast<float>(i)   / N;
                    float u1 = static_cast<float>(i+1) / N;
                    dst[startOffset + vtxCount++] = PV(a, u0, 0);
                    dst[startOffset + vtxCount++] = PV(b, u0, 1);
                    dst[startOffset + vtxCount++] = PV(d, u1, 1);
                    dst[startOffset + vtxCount++] = PV(a, u0, 0);
                    dst[startOffset + vtxCount++] = PV(d, u1, 1);
                    dst[startOffset + vtxCount++] = PV(c, u1, 0);
                }
            }
        }
        return vtxCount;
    };

    auto IssueDraw = [&](ID3D12PipelineState* pso, uint32_t startVtx, uint32_t vtxCount) {
        cmd->SetPipelineState(pso);
        cmd->SetGraphicsRootSignature(m_beamRS.Get());
        cmd->SetGraphicsRootConstantBufferView(0, m_passCB[fi]->GetGPUVirtualAddress());
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        D3D12_VERTEX_BUFFER_VIEW vbv = {};
        vbv.BufferLocation = m_beamVB_buf[fi]->GetGPUVirtualAddress() + (UINT64)startVtx * sizeof(BeamVert);
        vbv.SizeInBytes    = sizeof(BeamVert) * vtxCount;
        vbv.StrideInBytes  = sizeof(BeamVert);
        cmd->IASetVertexBuffers(0, 1, &vbv);
        cmd->DrawInstanced(vtxCount, 1, 0, 0);
    };

    uint32_t alphaBeams = FillBeams(BlendMode::AlphaBlend, 0);
    if (alphaBeams > 0) IssueDraw(m_beamPSO.Get(), 0, alphaBeams);

    uint32_t addBeams = FillBeams(BlendMode::Additive, alphaBeams);
    if (addBeams > 0) IssueDraw(m_beamAddPSO.Get(), alphaBeams, addBeams);

    return alphaBeams + addBeams;
}

// ── Ribbon draw ──────────────────────────────────────────────────────────────

void ParticlePass::DrawRibbons(ID3D12GraphicsCommandList* cmd, uint32_t fi,
                                const SceneManager& scene,
                                const Vector3& camPos, uint32_t beamVtxUsed) {
    BeamVert* dst = m_beamMapped[fi];

    auto FillRibbons = [&](BlendMode targetBlend, uint32_t startOffset) -> uint32_t {
        uint32_t vtxCount = 0;
        for (auto& actorPtr : scene.GetActors()) {
            auto* pc = actorPtr->GetComponent<ParticleComponent>();
            if (!pc) continue;
            auto* tc = actorPtr->GetComponent<TransformComponent>();
            Matrix4x4 world = tc ? tc->GetWorldMatrix() : Matrix4x4::Identity;
            for (auto& em : pc->GetEmitters()) {
                if (em.GetDesc().RenderMode != EmitterRenderMode::Ribbon) continue;
                if (em.GetDesc().Blend != targetBlend) continue;
                float emEI = em.GetDesc().EmissiveIntensity;
                bool  local = em.GetDesc().LocalSpace;

                std::vector<const Particle*> sorted;
                for (auto& p : em.GetParticles())
                    if (p.Active) sorted.push_back(&p);
                if (sorted.size() < 2) continue;

                std::sort(sorted.begin(), sorted.end(),
                    [](const Particle* a, const Particle* b) { return a->Age > b->Age; });

                for (size_t i = 0; i + 1 < sorted.size(); ++i) {
                    if (startOffset + vtxCount + 6 > MAX_BEAM_VERTS) break;
                    const Particle* pa = sorted[i];
                    const Particle* pb = sorted[i + 1];
                    Vector3 p0 = local ? XformPoint(world, pa->Position) : pa->Position;
                    Vector3 p1 = local ? XformPoint(world, pb->Position) : pb->Position;
                    Vector3 seg = (p1 - p0).GetSafeNormal();
                    Vector3 mid = Vector3((p0.x+p1.x)*0.5f,(p0.y+p1.y)*0.5f,(p0.z+p1.z)*0.5f);
                    Vector3 toCam = (camPos - mid).GetSafeNormal();
                    float w0 = pa->Size * 0.5f;
                    float w1 = pb->Size * 0.5f;
                    Vector3 perp0 = Vector3::Cross(seg, toCam).GetSafeNormal() * w0;
                    Vector3 perp1 = Vector3::Cross(seg, toCam).GetSafeNormal() * w1;
                    float u0 = static_cast<float>(i)   / sorted.size();
                    float u1 = static_cast<float>(i+1) / sorted.size();
                    float c0[4] = { pa->Color.x * emEI, pa->Color.y * emEI, pa->Color.z * emEI, pa->Color.w };
                    float c1[4] = { pb->Color.x * emEI, pb->Color.y * emEI, pb->Color.z * emEI, pb->Color.w };
                    auto PV = [&](Vector3 p, float u, float v, float* c) -> BeamVert {
                        return { {p.x,p.y,p.z}, u, v, {c[0],c[1],c[2],c[3]} };
                    };
                    Vector3 a = p0 - perp0, b = p0 + perp0;
                    Vector3 c = p1 - perp1, d = p1 + perp1;
                    dst[startOffset + vtxCount++] = PV(a, u0, 0, c0);
                    dst[startOffset + vtxCount++] = PV(b, u0, 1, c0);
                    dst[startOffset + vtxCount++] = PV(d, u1, 1, c1);
                    dst[startOffset + vtxCount++] = PV(a, u0, 0, c0);
                    dst[startOffset + vtxCount++] = PV(d, u1, 1, c1);
                    dst[startOffset + vtxCount++] = PV(c, u1, 0, c1);
                }
            }
        }
        return vtxCount;
    };

    auto IssueDraw = [&](ID3D12PipelineState* pso, uint32_t startVtx, uint32_t vtxCount) {
        cmd->SetPipelineState(pso);
        cmd->SetGraphicsRootSignature(m_beamRS.Get());
        cmd->SetGraphicsRootConstantBufferView(0, m_passCB[fi]->GetGPUVirtualAddress());
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        D3D12_VERTEX_BUFFER_VIEW vbv = {};
        vbv.BufferLocation = m_beamVB_buf[fi]->GetGPUVirtualAddress() + (UINT64)startVtx * sizeof(BeamVert);
        vbv.SizeInBytes    = sizeof(BeamVert) * vtxCount;
        vbv.StrideInBytes  = sizeof(BeamVert);
        cmd->IASetVertexBuffers(0, 1, &vbv);
        cmd->DrawInstanced(vtxCount, 1, 0, 0);
    };

    uint32_t alphaRibbons = FillRibbons(BlendMode::AlphaBlend, beamVtxUsed);
    if (alphaRibbons > 0) IssueDraw(m_beamPSO.Get(), beamVtxUsed, alphaRibbons);

    uint32_t addRibbons = FillRibbons(BlendMode::Additive, beamVtxUsed + alphaRibbons);
    if (addRibbons > 0) IssueDraw(m_beamAddPSO.Get(), beamVtxUsed + alphaRibbons, addRibbons);
}

// ── GPU sprite draw (compute spawn+update → graphics draw) ───────────────────

void ParticlePass::DrawGPUSprites(ID3D12GraphicsCommandList* cmd,
                                   GraphicsDevice& gfx,
                                   const SceneManager& scene,
                                   TextureManager& texMgr,
                                   uint32_t fi, float dt, float /*elapsed*/,
                                   const Matrix4x4& gpuViewProj,
                                   uint32_t gpuVpX, uint32_t gpuVpY, uint32_t gpuVpW, uint32_t gpuVpH,
                                   ID3D12Resource* normalRes, uint32_t normalSRVSlot) {
    auto* dev = gfx.GetDevice();
    // Lazily transition depth + normal to a compute-readable state for GPU collision, then bind them
    // to every update dispatch (the shader only samples when Collision=1). Restored before the draws.
    bool gpuDepthBound = false;
    auto ensureDepthBound = [&]() {
        if (gpuDepthBound) return;
        TransitionResource(cmd, gfx.GetDepthBuffer(),
                           D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        if (normalRes)
            TransitionResource(cmd, normalRes,
                               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        ID3D12DescriptorHeap* heaps[] = { gfx.GetSRVHeap() };
        cmd->SetDescriptorHeaps(1, heaps);
        gpuDepthBound = true;
    };

    // Collect GPU emitters that need work this frame
    struct GPUDrawEntry {
        GPUEmitterState* state;
        uint32_t          maxParticles;
        bool              additive;
        uint32_t          texSlot;
        int               cols, rows, hasTex;
    };
    std::vector<GPUDrawEntry> drawList;

    for (auto& actorPtr : scene.GetActors()) {
        auto* pc = actorPtr->GetComponent<ParticleComponent>();
        if (!pc) continue;
        auto* tc = actorPtr->GetComponent<TransformComponent>();
        Vector3 worldPos = tc ? tc->GetWorldMatrix().GetTranslation() : Vector3{};

        uint32_t emIdx = 0;
        for (auto& em : pc->GetEmitters()) {
            ++emIdx;
            if (em.GetDesc().Simulation != SimMode::GPU) continue;
            if (em.GetDesc().RenderMode != EmitterRenderMode::Sprite) continue;

            uint64_t key = ((uint64_t)(uintptr_t)actorPtr.get() << 8) | (uint64_t)emIdx;
            uint32_t maxP = (uint32_t)em.GetDesc().MaxParticles;
            const auto& desc = em.GetDesc();

            if (m_gpuEmitters.find(key) == m_gpuEmitters.end()) {
                if (!InitGPUEmitter(dev, key, maxP)) continue;
                m_gpuEmitters[key].prevInit = desc.Init;
            }
            {
                GPUEmitterState& existing = m_gpuEmitters[key];
                if (existing.maxParticles != maxP) {
                    float savedAccum = existing.accumTime;
                    // GPU が前フレームのコマンドリストで古いバッファを参照中の可能性があるため
                    // リソース解放前に全フレームの完了を待つ
                    gfx.WaitForGPU();
                    m_gpuEmitters.erase(key);
                    if (!InitGPUEmitter(dev, key, maxP)) continue;
                    m_gpuEmitters[key].accumTime = savedAccum;
                    m_gpuEmitters[key].prevInit  = desc.Init;
                }
            }
            GPUEmitterState& s = m_gpuEmitters[key];

            // ── Stop / Reset detection ───────────────────────────────────────
            // スポーン時パラメータ(size/life/vel/color)が変化したら即 kill-all して
            // 全パーティクルを新パラメータで再スポーンさせる
            bool initParamsChanged = !InitParamsEqual(s.prevInit, desc.Init);
            bool wasReset = (em.GetResetCount() != s.lastResetCount) || initParamsChanged;
            if (wasReset) {
                s.accumTime          = 0.0f;
                s.elapsed            = 0.0f;
                s.lastResetCount     = em.GetResetCount();
                s.lastBurstFireCount = em.GetBurstFireCount();
                s.prevInit           = desc.Init;
                s.burstDone          = false;
            } else if (s.lastBurstFireCount != em.GetBurstFireCount()) {
                s.lastBurstFireCount = em.GetBurstFireCount();
                s.burstDone          = false;
            }

            // ── Build spawn data on CPU ──────────────────────────────────────
            s.elapsed += dt;  // always advance for turbulence noise

            auto rv = [](float a, float b) { return a + (rand() / (float)RAND_MAX) * (b - a); };
            auto fillSpawnEntry = [&](GPUSpawnData& sd, const Vector3& spawnPos) {
                sd.pos[0] = spawnPos.x; sd.pos[1] = spawnPos.y; sd.pos[2] = spawnPos.z;
                float lifeRange = desc.Init.LifeMax - desc.Init.LifeMin;
                sd.lifetime = desc.Init.LifeMin + (lifeRange > 0 ? (rand() / (float)RAND_MAX) * lifeRange : 0);
                sd.vel[0] = rv(desc.Init.VelMin.x, desc.Init.VelMax.x);
                sd.vel[1] = rv(desc.Init.VelMin.y, desc.Init.VelMax.y);
                sd.vel[2] = rv(desc.Init.VelMin.z, desc.Init.VelMax.z);
                sd.sizeBase = rv(desc.Init.SizeMin, desc.Init.SizeMax);
                float ei = desc.EmissiveIntensity;
                sd.colorStart[0] = desc.Init.ColorStart.x * ei; sd.colorStart[1] = desc.Init.ColorStart.y * ei;
                sd.colorStart[2] = desc.Init.ColorStart.z * ei; sd.colorStart[3] = desc.Init.ColorStart.w;
                sd.colorEnd[0]   = desc.Init.ColorEnd.x * ei;   sd.colorEnd[1]   = desc.Init.ColorEnd.y * ei;
                sd.colorEnd[2]   = desc.Init.ColorEnd.z * ei;   sd.colorEnd[3]   = desc.Init.ColorEnd.w;
                sd.rot     = rv(0.0f, 360.0f);
                sd.rotRate = rv(desc.Init.RotRateMin, desc.Init.RotRateMax);
                sd.pad[0]  = sd.pad[1] = 0;
            };

            uint32_t spawnCount = 0;
            if (wasReset) {
                // Kill all live GPU particles by overwriting every slot with lifetime=0
                memset(s.spawnMapped[fi], 0,
                       (size_t)s.maxParticles * sizeof(GPUSpawnData));
                spawnCount = s.maxParticles;
            } else if (em.IsPlaying()) {
                if (desc.Spawn.BurstMode) {
                    // Burst mode: fire BurstCount particles once
                    if (!s.burstDone && desc.Spawn.BurstCount > 0) {
                        int count = (std::min)((uint32_t)desc.Spawn.BurstCount, maxP);
                        for (int i = 0; i < count; ++i) {
                            fillSpawnEntry(s.spawnMapped[fi][spawnCount++],
                                           SampleGPUSpawnPos(desc.Spawn, worldPos));
                        }
                        s.burstDone = true;
                    }
                } else {
                    // Continuous mode: rate-based spawning
                    s.accumTime += dt;
                    float spawnInterval = (desc.Spawn.RatePerSecond > 0.0f)
                                            ? 1.0f / desc.Spawn.RatePerSecond : 1e9f;
                    while (s.accumTime >= spawnInterval && spawnCount < maxP) {
                        s.accumTime -= spawnInterval;
                        fillSpawnEntry(s.spawnMapped[fi][spawnCount++],
                                       SampleGPUSpawnPos(desc.Spawn, worldPos));
                    }
                }
            }
            // Stopped and not reset: spawnCount stays 0, accumTime stays frozen,
            // existing GPU particles age out naturally via the update CS.

            // ── Write compute CB (offset 0 = SpawnParams, offset 256 = UpdateParams) ──
            // SpawnParams
            struct SpawnParamsCB { uint32_t SpawnCount; uint32_t MaxParticles; uint32_t pad[2]; };
            SpawnParamsCB spawnCB = { spawnCount, maxP, {0,0} };
            memcpy(s.computeCBMapped[fi] + 0, &spawnCB, sizeof(spawnCB));

            // UpdateParams — must exactly match cbuffer layout in ParticleGPU_Update.CS.hlsl
            struct UpdateParamsCB {
                float    Gravity[3];        float    DT;              // row 0
                float    Drag;              float    SizeEndMult;     uint32_t MaxParticles;  uint32_t FadeColor;    // row 1
                uint32_t ShrinkSize;        float    Elapsed;         uint32_t Turbulence;    float    TurbStrength; // row 2
                float    TurbFrequency;     uint32_t UseAttractor;    uint32_t UseColorMid;   float    AttractorStrength; // row 3
                float    AttractorPos[3];   float    AttractorRadius; // row 4
                float    ColorMid[4];       // row 5
                float    ViewProj[16];      // rows 6-9
                float    Viewport[4];       // row 10
                float    RTSize[2];         float _cpad[2];           // row 11
                uint32_t Collision;         float Restitution;       float Friction;         float CollPush; // row 12
                uint32_t UseSizeCurve;      float _sccpad[3];         // row 13
                float    SizeCurve[8];      // rows 14-15 (tightly packed, matches float4[2] in HLSL)
                uint32_t UseVortex;         float VortexStrength;    float VortexInward;     float VortexRadius; // row 16
                float    VortexCenter[3];   float _vcpad;            // row 17
                float    VortexAxis[3];     float _vapad;            // row 18
            };
            static_assert(sizeof(UpdateParamsCB) == 304, "UpdateParamsCB layout mismatch");

            UpdateParamsCB upCB = {};
            upCB.Gravity[0]      = desc.Update.Gravity.x;
            upCB.Gravity[1]      = desc.Update.Gravity.y;
            upCB.Gravity[2]      = desc.Update.Gravity.z;
            upCB.DT              = dt;
            upCB.Drag            = desc.Update.Drag;
            upCB.SizeEndMult     = desc.Update.SizeEndMult;
            upCB.MaxParticles    = maxP;
            upCB.FadeColor       = desc.Update.FadeColor    ? 1u : 0u;
            upCB.ShrinkSize      = desc.Update.ShrinkSize   ? 1u : 0u;
            upCB.Elapsed         = s.elapsed;
            upCB.Turbulence      = desc.Update.Turbulence   ? 1u : 0u;
            upCB.TurbStrength    = desc.Update.TurbStrength;
            upCB.TurbFrequency   = desc.Update.TurbFrequency;
            upCB.UseAttractor    = desc.Update.UseAttractor ? 1u : 0u;
            upCB.UseColorMid     = desc.Init.UseColorMid    ? 1u : 0u;
            upCB.AttractorStrength = desc.Update.AttractorStrength;
            upCB.AttractorPos[0] = desc.Update.AttractorPos.x;
            upCB.AttractorPos[1] = desc.Update.AttractorPos.y;
            upCB.AttractorPos[2] = desc.Update.AttractorPos.z;
            upCB.AttractorRadius = desc.Update.AttractorRadius;
            upCB.ColorMid[0]     = desc.Init.ColorMid.x;
            upCB.ColorMid[1]     = desc.Init.ColorMid.y;
            upCB.ColorMid[2]     = desc.Init.ColorMid.z;
            upCB.ColorMid[3]     = desc.Init.ColorMid.w;
            memcpy(upCB.ViewProj, gpuViewProj.v, 64);
            upCB.Viewport[0] = (float)gpuVpX; upCB.Viewport[1] = (float)gpuVpY;
            upCB.Viewport[2] = (float)gpuVpW; upCB.Viewport[3] = (float)gpuVpH;
            upCB.RTSize[0]   = (float)gfx.GetWidth(); upCB.RTSize[1] = (float)gfx.GetHeight();
            upCB.Collision   = desc.Update.Collision ? 1u : 0u;
            upCB.Restitution = desc.Update.Restitution;
            upCB.Friction    = desc.Update.Friction;
            upCB.CollPush    = desc.Update.CollPush;
            upCB.UseSizeCurve = desc.Update.UseSizeCurve ? 1u : 0u;
            for (int k = 0; k < 8; ++k) upCB.SizeCurve[k] = desc.Update.SizeCurve[k];
            upCB.UseVortex      = desc.Update.UseVortex ? 1u : 0u;
            upCB.VortexStrength = desc.Update.VortexStrength;
            upCB.VortexInward   = desc.Update.VortexInward;
            upCB.VortexRadius   = desc.Update.VortexRadius;
            // GPU particles live in world space; offset the vortex center by the emitter world pos
            // so the swirl follows the emitter (matches the CPU path in Emitter::Update).
            upCB.VortexCenter[0] = desc.Update.VortexCenter.x + worldPos.x;
            upCB.VortexCenter[1] = desc.Update.VortexCenter.y + worldPos.y;
            upCB.VortexCenter[2] = desc.Update.VortexCenter.z + worldPos.z;
            upCB.VortexAxis[0]  = desc.Update.VortexAxis.x;
            upCB.VortexAxis[1]  = desc.Update.VortexAxis.y;
            upCB.VortexAxis[2]  = desc.Update.VortexAxis.z;
            memcpy(s.computeCBMapped[fi] + 256, &upCB, sizeof(upCB));

            // ── Transition particle buffer to UAV for compute ────────────────
            if (s.particleState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
                TransitionResource(cmd, s.particleBuf.Get(), s.particleState,
                                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                s.particleState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            }

            D3D12_GPU_VIRTUAL_ADDRESS cbBase = s.computeCB[fi]->GetGPUVirtualAddress();

            // ── CS Spawn ─────────────────────────────────────────────────────
            if (spawnCount > 0) {
                cmd->SetComputeRootSignature(m_gpuSpawnRS.Get());
                cmd->SetPipelineState(m_gpuSpawnPSO.Get());
                cmd->SetComputeRootConstantBufferView(0, cbBase + 0);
                cmd->SetComputeRootUnorderedAccessView(1, s.particleBuf->GetGPUVirtualAddress());
                cmd->SetComputeRootUnorderedAccessView(2, s.writeHead->GetGPUVirtualAddress());
                cmd->SetComputeRootShaderResourceView(3,  s.spawnUpload[fi]->GetGPUVirtualAddress());
                uint32_t groups = (spawnCount + 63) / 64;
                cmd->Dispatch(groups, 1, 1);
                UAVBarrier(cmd, s.particleBuf.Get());
            }

            // ── CS Update ────────────────────────────────────────────────────
            ensureDepthBound();
            cmd->SetComputeRootSignature(m_gpuUpdateRS.Get());
            cmd->SetPipelineState(m_gpuUpdatePSO.Get());
            cmd->SetComputeRootConstantBufferView(0, cbBase + 256);
            cmd->SetComputeRootUnorderedAccessView(1, s.particleBuf->GetGPUVirtualAddress());
            cmd->SetComputeRootDescriptorTable(2, gfx.GetSRVGPUHandle(gfx.GetDepthSRVSlot()));
            cmd->SetComputeRootDescriptorTable(3, gfx.GetSRVGPUHandle(normalRes ? normalSRVSlot : gfx.GetDepthSRVSlot()));
            {
                uint32_t groups = (maxP + 63) / 64;
                cmd->Dispatch(groups, 1, 1);
            }
            UAVBarrier(cmd, s.particleBuf.Get());

            // ── Transition to SRV for VS read ────────────────────────────────
            TransitionResource(cmd, s.particleBuf.Get(),
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            s.particleState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

            bool hasTex = !desc.SpriteTexturePath.empty();
            uint32_t texSlot = hasTex ? texMgr.LoadTexture(desc.SpriteTexturePath) : texMgr.GetFallbackSlot();
            drawList.push_back({ &s, maxP, desc.Blend == BlendMode::Additive,
                                 texSlot, desc.SubUVCols, desc.SubUVRows, hasTex ? 1 : 0 });
        }
    }

    // Restore depth + normal to their pre-pass states (the draws below depth-test/write).
    if (gpuDepthBound) {
        TransitionResource(cmd, gfx.GetDepthBuffer(),
                           D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        if (normalRes)
            TransitionResource(cmd, normalRes,
                               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    if (drawList.empty()) return;

    // ── Draw all GPU emitters ─────────────────────────────────────────────────
    ID3D12DescriptorHeap* heaps[] = { gfx.GetSRVHeap() };
    cmd->SetDescriptorHeaps(1, heaps);
    cmd->SetGraphicsRootSignature(m_gpuDrawRS.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    ID3D12PipelineState* curPSO = nullptr;
    for (auto& entry : drawList) {
        auto* pso = entry.additive ? m_gpuDrawAddPSO.Get() : m_gpuDrawPSO.Get();
        if (pso != curPSO) { cmd->SetPipelineState(pso); curPSO = pso; }
        cmd->SetGraphicsRootConstantBufferView(0, m_passCB[fi]->GetGPUVirtualAddress());
        cmd->SetGraphicsRootShaderResourceView(1, entry.state->particleBuf->GetGPUVirtualAddress());
        int subuv[4] = { entry.cols, entry.rows, entry.hasTex, 0 };
        cmd->SetGraphicsRoot32BitConstants(2, 4, subuv, 0);
        cmd->SetGraphicsRootDescriptorTable(3, gfx.GetSRVGPUHandle(entry.texSlot));
        cmd->DrawInstanced(6, entry.maxParticles, 0, 0);
    }
}

} // namespace Fujin
