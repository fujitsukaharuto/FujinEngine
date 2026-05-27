#include "ParticlePass.h"
#include "Engine/Core/SceneManager.h"
#include "Engine/Core/Actor.h"
#include "Engine/Core/TransformComponent.h"
#include "Engine/Core/ParticleComponent.h"
#include "Engine/Graphics/DxcHelper.h"
#include <cmath>
#include <algorithm>
#include <vector>

namespace Fujin {

static constexpr float PI = 3.14159265f;

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
    D3D12_ROOT_PARAMETER param = {};
    param.ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    param.Descriptor.ShaderRegister = 0;
    param.ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters  = 1;
    desc.pParameters    = &param;
    desc.Flags          = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

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

    // Update CS root signature: [0]=CBV b0, [1]=UAV u0 (particles)
    {
        D3D12_ROOT_PARAMETER params[2] = {};
        params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[0].Descriptor.ShaderRegister = 0;
        params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        params[1].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[1].Descriptor.ShaderRegister = 0;
        params[1].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        D3D12_ROOT_SIGNATURE_DESC desc = {};
        desc.NumParameters = 2;
        desc.pParameters   = params;
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

    // [0]=CBV b0 (VS), [1]=SRV t0 (VS) — no input assembler
    D3D12_ROOT_PARAMETER params[2] = {};
    params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;
    params[1].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[1].Descriptor.ShaderRegister = 0;
    params[1].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters = 2;
    rsDesc.pParameters   = params;
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
        if (!CreateUploadBuffer(dev, 512, s.computeCB[i], &p)) return false;
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
    if (!CreateBuffers(gfx))              return false;
    if (!CreateGPUComputePipelines(gfx))  return false;
    if (!CreateGPUDrawPipeline(gfx))      return false;
    return true;
}

void ParticlePass::Shutdown() {
    m_spritePSO.Reset(); m_spriteAddPSO.Reset(); m_spriteRS.Reset();
    m_beamPSO.Reset();   m_beamAddPSO.Reset();   m_beamRS.Reset();
    m_gpuSpawnPSO.Reset();  m_gpuSpawnRS.Reset();
    m_gpuUpdatePSO.Reset(); m_gpuUpdateRS.Reset();
    m_gpuDrawPSO.Reset();   m_gpuDrawAddPSO.Reset(); m_gpuDrawRS.Reset();
    m_quadVB.Reset();    m_quadIB.Reset();
    for (uint32_t i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i) {
        m_instanceVB[i].Reset();
        m_beamVB_buf[i].Reset();
        m_passCB[i].Reset();
    }
    m_gpuEmitters.clear();
}

// ── Execute ──────────────────────────────────────────────────────────────────

void ParticlePass::Execute(ID3D12GraphicsCommandList* cmd,
                            GraphicsDevice& gfx,
                            const SceneManager& scene,
                            const Matrix4x4& viewProj,
                            const Matrix4x4& view,
                            uint32_t vpX, uint32_t vpY,
                            uint32_t vpW, uint32_t vpH,
                            uint32_t frameIndex,
                            float elapsed,
                            float dt)
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
    DrawGPUSprites(cmd, gfx, scene, frameIndex, dt, elapsed);

    // CPU particles
    DrawSprites(cmd, frameIndex, scene);
    uint32_t beamVtxUsed = DrawBeams(cmd, frameIndex, scene, camPos, elapsed);
    DrawRibbons(cmd, frameIndex, scene, camPos, beamVtxUsed);
}

// ── Sprite draw ──────────────────────────────────────────────────────────────

void ParticlePass::DrawSprites(ID3D12GraphicsCommandList* cmd, uint32_t fi,
                                const SceneManager& scene) {
    uint32_t alphaCount = 0;
    uint32_t addCount   = 0;
    InstanceVert* dst = m_instanceMapped[fi];

    auto FillSprites = [&](BlendMode targetBlend, uint32_t& count, uint32_t offset) {
        for (auto& actorPtr : scene.GetActors()) {
            auto* pc = actorPtr->GetComponent<ParticleComponent>();
            if (!pc) continue;
            for (auto& em : pc->GetEmitters()) {
                if (em.GetDesc().RenderMode != EmitterRenderMode::Sprite) continue;
                if (em.GetDesc().Blend != targetBlend) continue;
                float ei = em.GetDesc().EmissiveIntensity;
                for (auto& p : em.GetParticles()) {
                    if (!p.Active || (offset + count) >= MAX_SPRITES) continue;
                    auto& iv    = dst[offset + count++];
                    iv.pos[0]   = p.Position.x;
                    iv.pos[1]   = p.Position.y;
                    iv.pos[2]   = p.Position.z;
                    iv.size     = p.Size;
                    iv.rot      = p.Rotation * 3.14159f / 180.0f;
                    iv.pad[0] = iv.pad[1] = iv.pad[2] = 0.0f;
                    iv.color[0] = p.Color.x * ei;
                    iv.color[1] = p.Color.y * ei;
                    iv.color[2] = p.Color.z * ei;
                    iv.color[3] = p.Color.w;
                }
            }
        }
    };

    FillSprites(BlendMode::AlphaBlend, alphaCount, 0);
    FillSprites(BlendMode::Additive,   addCount,   alphaCount);

    if (alphaCount == 0 && addCount == 0) return;

    D3D12_VERTEX_BUFFER_VIEW vbvs[2] = {};
    vbvs[0].BufferLocation = m_quadVB->GetGPUVirtualAddress();
    vbvs[0].SizeInBytes    = sizeof(float) * 4 * 4;
    vbvs[0].StrideInBytes  = sizeof(float) * 4;
    vbvs[1].BufferLocation = m_instanceVB[fi]->GetGPUVirtualAddress();
    vbvs[1].SizeInBytes    = sizeof(InstanceVert) * (alphaCount + addCount);
    vbvs[1].StrideInBytes  = sizeof(InstanceVert);

    D3D12_INDEX_BUFFER_VIEW ibv = {};
    ibv.BufferLocation = m_quadIB->GetGPUVirtualAddress();
    ibv.SizeInBytes    = sizeof(uint16_t) * 6;
    ibv.Format         = DXGI_FORMAT_R16_UINT;

    cmd->SetGraphicsRootSignature(m_spriteRS.Get());
    cmd->SetGraphicsRootConstantBufferView(0, m_passCB[fi]->GetGPUVirtualAddress());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->IASetVertexBuffers(0, 2, vbvs);
    cmd->IASetIndexBuffer(&ibv);

    if (alphaCount > 0) {
        cmd->SetPipelineState(m_spritePSO.Get());
        cmd->DrawIndexedInstanced(6, alphaCount, 0, 0, 0);
    }
    if (addCount > 0) {
        cmd->SetPipelineState(m_spriteAddPSO.Get());
        cmd->DrawIndexedInstanced(6, addCount, 0, 0, alphaCount);
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
            for (auto& em : pc->GetEmitters()) {
                if (em.GetDesc().RenderMode != EmitterRenderMode::Ribbon) continue;
                if (em.GetDesc().Blend != targetBlend) continue;
                float emEI = em.GetDesc().EmissiveIntensity;

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
                    Vector3 p0 = pa->Position, p1 = pb->Position;
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
                                   uint32_t fi, float dt, float /*elapsed*/) {
    auto* dev = gfx.GetDevice();

    // Collect GPU emitters that need work this frame
    struct GPUDrawEntry {
        GPUEmitterState* state;
        uint32_t          maxParticles;
        bool              additive;
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
                s.accumTime      = 0.0f;
                s.elapsed        = 0.0f;
                s.lastResetCount = em.GetResetCount();
                s.prevInit       = desc.Init;
            }

            // ── Build spawn data on CPU ──────────────────────────────────────
            s.elapsed += dt;  // always advance for turbulence noise

            uint32_t spawnCount = 0;
            if (wasReset) {
                // Kill all live GPU particles by overwriting every slot with lifetime=0
                memset(s.spawnMapped[fi], 0,
                       (size_t)s.maxParticles * sizeof(GPUSpawnData));
                spawnCount = s.maxParticles;
            } else if (em.IsPlaying()) {
                s.accumTime += dt;
                float spawnInterval = (desc.Spawn.RatePerSecond > 0.0f)
                                        ? 1.0f / desc.Spawn.RatePerSecond : 1e9f;
                auto rv = [](float a, float b) { return a + (rand() / (float)RAND_MAX) * (b - a); };
                while (s.accumTime >= spawnInterval && spawnCount < maxP) {
                    s.accumTime -= spawnInterval;
                    GPUSpawnData& sd = s.spawnMapped[fi][spawnCount++];
                    Vector3 spawnPos = SampleGPUSpawnPos(desc.Spawn, worldPos);
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
            };
            static_assert(sizeof(UpdateParamsCB) == 96, "UpdateParamsCB layout mismatch");

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
            cmd->SetComputeRootSignature(m_gpuUpdateRS.Get());
            cmd->SetPipelineState(m_gpuUpdatePSO.Get());
            cmd->SetComputeRootConstantBufferView(0, cbBase + 256);
            cmd->SetComputeRootUnorderedAccessView(1, s.particleBuf->GetGPUVirtualAddress());
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

            drawList.push_back({ &s, maxP, desc.Blend == BlendMode::Additive });
        }
    }

    if (drawList.empty()) return;

    // ── Draw all GPU emitters ─────────────────────────────────────────────────
    cmd->SetGraphicsRootSignature(m_gpuDrawRS.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    ID3D12PipelineState* curPSO = nullptr;
    for (auto& entry : drawList) {
        auto* pso = entry.additive ? m_gpuDrawAddPSO.Get() : m_gpuDrawPSO.Get();
        if (pso != curPSO) { cmd->SetPipelineState(pso); curPSO = pso; }
        cmd->SetGraphicsRootConstantBufferView(0, m_passCB[fi]->GetGPUVirtualAddress());
        cmd->SetGraphicsRootShaderResourceView(1, entry.state->particleBuf->GetGPUVirtualAddress());
        cmd->DrawInstanced(6, entry.maxParticles, 0, 0);
    }
}

} // namespace Fujin
