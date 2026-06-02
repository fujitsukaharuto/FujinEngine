#include "TranslucencyPass.h"
#include "Engine/Core/SceneManager.h"
#include "Engine/Core/Actor.h"
#include "Engine/Core/MeshComponent.h"
#include "Engine/Core/TransformComponent.h"
#include "Engine/Core/LightComponent.h"
#include "Engine/Core/AnimationComponent.h"
#include "Engine/Asset/MeshAsset.h"
#include "Engine/Graphics/DxcHelper.h"
#include <algorithm>
#include <stdexcept>

namespace Fujin {

static ComPtr<ID3D12Resource> MakeUploadCB(ID3D12Device* dev, UINT64 sz, uint8_t** mapped) {
    D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width = sz;
    rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_UNKNOWN; rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> res;
    D3D12_RANGE range = {};
    dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&res));
    if (res) res->Map(0, &range, reinterpret_cast<void**>(mapped));
    return res;
}

bool TranslucencyPass::Initialize(GraphicsDevice& gfx) {
    try {
        if (!CreateRootSignature(gfx.GetDevice()))   return false;
        if (!CreatePipelineStates(gfx.GetDevice()))  return false;
        if (!CreateConstantBuffers(gfx.GetDevice())) return false;
    } catch (...) {
        return false;
    }
    return true;
}

bool TranslucencyPass::CreateRootSignature(ID3D12Device* device) {
    // [0] CBV b0 (PerObject)  - VS+PS
    // [1] CBV b1 (Camera)     - PS
    // [2] CBV b2 (Lights)     - PS
    // [3] CBV b3 (Shadow)     - PS
    // [4] Table t0 (Albedo)   - PS
    // [5] Table t1 (Normal)   - PS
    // [6] Table t2 (ORM)      - PS
    // [7] Table t3 (ShadowMap)- PS
    // [8] Table t4 (IBL Irr)  - PS
    // [9] Table t5 (IBL Pref) - PS
    //[10] Table t6 (BRDF LUT) - PS
    D3D12_ROOT_PARAMETER params[11] = {};

    params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;

    for (uint32_t b = 1; b <= 3; ++b) {
        params[b].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[b].Descriptor.ShaderRegister = b;
        params[b].ShaderVisibility          = D3D12_SHADER_VISIBILITY_PIXEL;
    }

    D3D12_DESCRIPTOR_RANGE ranges[7] = {};
    for (uint32_t t = 0; t < 7; ++t) {
        ranges[t].RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[t].NumDescriptors                    = 1;
        ranges[t].BaseShaderRegister                = t;
        ranges[t].RegisterSpace                     = 0;
        ranges[t].OffsetInDescriptorsFromTableStart = 0;
        params[4 + t].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[4 + t].DescriptorTable.NumDescriptorRanges = 1;
        params[4 + t].DescriptorTable.pDescriptorRanges   = &ranges[t];
        params[4 + t].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
    }

    D3D12_STATIC_SAMPLER_DESC samplers[4] = {};
    // s0: LinearWrap for albedo/normal/orm
    samplers[0].Filter         = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[0].AddressU = samplers[0].AddressV = samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].MaxLOD         = D3D12_FLOAT32_MAX;
    samplers[0].ShaderRegister = 0;
    samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    // s1: comparison LESS_EQUAL for shadow PCF
    samplers[1].Filter         = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    samplers[1].AddressU = samplers[1].AddressV = samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].MaxLOD         = D3D12_FLOAT32_MAX;
    samplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    samplers[1].ShaderRegister = 1;
    samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    // s2: LinearClamp mip for IBL
    samplers[2].Filter         = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[2].AddressU = samplers[2].AddressV = samplers[2].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[2].MaxLOD         = D3D12_FLOAT32_MAX;
    samplers[2].ShaderRegister = 2;
    samplers[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    // s3: bilinear clamp for BRDF LUT
    samplers[3].Filter         = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    samplers[3].AddressU = samplers[3].AddressV = samplers[3].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[3].MaxLOD         = 0;
    samplers[3].ShaderRegister = 3;
    samplers[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters     = 11;
    rsDesc.pParameters       = params;
    rsDesc.NumStaticSamplers = 4;
    rsDesc.pStaticSamplers   = samplers;
    rsDesc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> blob, err;
    D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
    return SUCCEEDED(device->CreateRootSignature(0, blob->GetBufferPointer(),
                                                  blob->GetBufferSize(),
                                                  IID_PPV_ARGS(&m_rootSignature)));
}

bool TranslucencyPass::CreatePipelineStates(ID3D12Device* device) {
    auto vs = LoadOrCompileShader(L"Resource/Shaders/Translucency.VS.hlsl", L"vs_6_0");
    auto ps = LoadOrCompileShader(L"Resource/Shaders/Translucency.PS.hlsl", L"ps_6_0");

    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    // Alpha blend: SrcAlpha + InvSrcAlpha
    D3D12_BLEND_DESC blendDesc = {};
    auto& rt0                        = blendDesc.RenderTarget[0];
    rt0.BlendEnable                  = TRUE;
    rt0.SrcBlend                     = D3D12_BLEND_SRC_ALPHA;
    rt0.DestBlend                    = D3D12_BLEND_INV_SRC_ALPHA;
    rt0.BlendOp                      = D3D12_BLEND_OP_ADD;
    rt0.SrcBlendAlpha                = D3D12_BLEND_ONE;
    rt0.DestBlendAlpha               = D3D12_BLEND_INV_SRC_ALPHA;
    rt0.BlendOpAlpha                 = D3D12_BLEND_OP_ADD;
    rt0.RenderTargetWriteMask        = D3D12_COLOR_WRITE_ENABLE_ALL;

    // Depth: test on, write off
    D3D12_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable    = TRUE;
    dsDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    dsDesc.DepthFunc      = D3D12_COMPARISON_FUNC_LESS_EQUAL;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature        = m_rootSignature.Get();
    psoDesc.VS                    = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS                    = { ps->GetBufferPointer(), ps->GetBufferSize() };
    psoDesc.InputLayout           = { inputLayout, 4 };
    psoDesc.BlendState            = blendDesc;
    psoDesc.DepthStencilState     = dsDesc;
    psoDesc.DSVFormat             = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleMask            = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets      = 1;
    psoDesc.RTVFormats[0]         = DXGI_FORMAT_R16G16B16A16_FLOAT;
    psoDesc.SampleDesc            = { 1, 0 };

    // PSO[0]: CullBack (default)
    D3D12_RASTERIZER_DESC rastBack = {};
    rastBack.FillMode        = D3D12_FILL_MODE_SOLID;
    rastBack.CullMode        = D3D12_CULL_MODE_BACK;
    rastBack.DepthClipEnable = TRUE;
    psoDesc.RasterizerState  = rastBack;
    if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pso[0]))))
        return false;

    // PSO[1]: CullNone (DoubleSided)
    D3D12_RASTERIZER_DESC rastNone = rastBack;
    rastNone.CullMode       = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState = rastNone;
    return SUCCEEDED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pso[1])));
}

bool TranslucencyPass::CreateConstantBuffers(ID3D12Device* device) {
    for (uint32_t i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i) {
        m_objCB[i]    = MakeUploadCB(device, static_cast<UINT64>(CB_SLOT_SIZE) * MAX_OBJECTS, &m_objMapped[i]);
        m_cameraCB[i] = MakeUploadCB(device, CAMERA_CB_SIZE, &m_cameraMapped[i]);
        m_lightsCB[i] = MakeUploadCB(device, LIGHTS_CB_SIZE, &m_lightsMapped[i]);
        m_shadowCB[i] = MakeUploadCB(device, SHADOW_CB_SIZE, &m_shadowMapped[i]);
        if (!m_objCB[i] || !m_cameraCB[i] || !m_lightsCB[i] || !m_shadowCB[i])
            return false;
    }
    return true;
}

void TranslucencyPass::Execute(ID3D12GraphicsCommandList* cmd,
                                GraphicsDevice& gfx,
                                const SceneManager& scene,
                                uint32_t frameIndex,
                                GeometryManager& geoMgr,
                                TextureManager& texMgr,
                                MaterialManager& matMgr,
                                const Matrix4x4& viewProj,
                                const Vector3& cameraPos,
                                const Vector3& cameraForward,
                                const ShadowData& shadowData,
                                uint32_t shadowSRVSlot,
                                uint32_t iblIrrSlot,
                                uint32_t iblPrefSlot,
                                uint32_t iblBRDFSlot,
                                D3D12_CPU_DESCRIPTOR_HANDLE hdrRTV,
                                uint32_t vpX, uint32_t vpY,
                                uint32_t vpW, uint32_t vpH) {
    // --- Collect translucent actors, sort back-to-front ---
    struct Entry { Actor* actor; float distSq; };
    std::vector<Entry> entries;
    entries.reserve(32);

    for (auto& actorPtr : scene.GetActors()) {
        auto* mc = actorPtr->GetComponent<MeshComponent>();
        if (!mc || mc->Blend != MeshBlendMode::Translucent) continue;
        if (geoMgr.IsSkeletal(mc->MeshPath)) continue;
        auto* tc = actorPtr->GetComponent<TransformComponent>();
        Vector3 p  = tc ? tc->CachedWorld.Position : Vector3(0, 0, 0);   // world centre for back-to-front sort
        float  dx  = p.x - cameraPos.x, dy = p.y - cameraPos.y, dz = p.z - cameraPos.z;
        entries.push_back({ actorPtr.get(), dx*dx + dy*dy + dz*dz });
    }
    if (entries.empty()) return;

    std::sort(entries.begin(), entries.end(),
              [](const Entry& a, const Entry& b) { return a.distSq > b.distSq; });

    uint32_t fi = frameIndex % NUM_FRAMES_IN_FLIGHT;

    // --- Update per-frame CBs ---

    // Camera CB: [0] CameraPos+pad [16] CameraForward+pad
    memcpy(m_cameraMapped[fi],      &cameraPos,     12);
    memcpy(m_cameraMapped[fi] + 16, &cameraForward, 12);

    // Lights CB
    struct GpuLight {
        float pos[3]; float type;
        float dir[3]; float range;
        float color[3]; float intensity;
        float spotAngle; float pad[3];
    };
    uint32_t lightCount = 0;
    GpuLight gpuLights[MAX_LIGHTS] = {};
    for (auto& actorPtr : scene.GetActors()) {
        if (lightCount >= MAX_LIGHTS) break;
        auto* lc = actorPtr->GetComponent<LightComponent>();
        if (!lc) continue;
        auto* tc = actorPtr->GetComponent<TransformComponent>();
        GpuLight& gl = gpuLights[lightCount];
        if (tc) {
            const Transform& w = tc->CachedWorld;   // parent-resolved world transform
            gl.pos[0] = w.Position.x; gl.pos[1] = w.Position.y; gl.pos[2] = w.Position.z;
            Matrix4x4 rot = w.Rotation.ToMatrix();
            gl.dir[0] = rot.m[2][0]; gl.dir[1] = rot.m[2][1]; gl.dir[2] = rot.m[2][2];
        }
        gl.type      = static_cast<float>(lc->Type);
        gl.range     = lc->Range;
        gl.color[0]  = lc->Color.x; gl.color[1] = lc->Color.y; gl.color[2] = lc->Color.z;
        gl.intensity = lc->Intensity;
        gl.spotAngle = lc->SpotAngle;
        ++lightCount;
    }
    memcpy(m_lightsMapped[fi],      &lightCount, 4);
    memset(m_lightsMapped[fi] + 4,  0,           12);
    memcpy(m_lightsMapped[fi] + 16, gpuLights, sizeof(GpuLight) * lightCount);

    // Shadow CB
    for (uint32_t c = 0; c < ShadowPass::CASCADE_COUNT; ++c)
        memcpy(m_shadowMapped[fi] + c * 64, shadowData.LightViewProj[c].v, 64);
    memcpy(m_shadowMapped[fi] + 256, shadowData.CascadeSplits, 16);

    // --- Render state ---
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = gfx.GetCurrentDSV();
    cmd->OMSetRenderTargets(1, &hdrRTV, FALSE, &dsv);

    D3D12_VIEWPORT vp = { static_cast<float>(vpX), static_cast<float>(vpY),
                          static_cast<float>(vpW), static_cast<float>(vpH), 0.0f, 1.0f };
    D3D12_RECT scissor = { static_cast<LONG>(vpX), static_cast<LONG>(vpY),
                           static_cast<LONG>(vpX + vpW), static_cast<LONG>(vpY + vpH) };
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &scissor);
    cmd->SetGraphicsRootSignature(m_rootSignature.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Per-frame bindings
    cmd->SetGraphicsRootConstantBufferView(1, m_cameraCB[fi]->GetGPUVirtualAddress());
    cmd->SetGraphicsRootConstantBufferView(2, m_lightsCB[fi]->GetGPUVirtualAddress());
    cmd->SetGraphicsRootConstantBufferView(3, m_shadowCB[fi]->GetGPUVirtualAddress());
    cmd->SetGraphicsRootDescriptorTable(7,  gfx.GetSRVGPUHandle(shadowSRVSlot));
    cmd->SetGraphicsRootDescriptorTable(8,  gfx.GetSRVGPUHandle(iblIrrSlot));
    cmd->SetGraphicsRootDescriptorTable(9,  gfx.GetSRVGPUHandle(iblPrefSlot));
    cmd->SetGraphicsRootDescriptorTable(10, gfx.GetSRVGPUHandle(iblBRDFSlot));

    const CBLayout& layout = matMgr.GetLayout();
    uint8_t* objBase = m_objMapped[fi];
    uint32_t slotIdx = 0;
    ID3D12PipelineState* curPso = nullptr;

    for (auto& entry : entries) {
        if (slotIdx >= MAX_OBJECTS) break;
        Actor* actor = entry.actor;
        auto*  mc    = actor->GetComponent<MeshComponent>();
        const MeshAsset* mesh = geoMgr.LoadMesh(mc->MeshPath);
        if (!mesh || mesh->SubMeshes.empty()) continue;

        // PSO: 0=CullBack, 1=CullNone
        ID3D12PipelineState* pso = m_pso[mc->DoubleSided ? 1 : 0].Get();
        if (pso != curPso) { cmd->SetPipelineState(pso); curPso = pso; }

        auto* tc       = actor->GetComponent<TransformComponent>();
        Matrix4x4 world = tc ? tc->GetWorldMatrix() : Matrix4x4::Identity;
        Matrix4x4 wvp   = viewProj * world;

        cmd->IASetVertexBuffers(0, 1, &mesh->VBView);
        cmd->IASetIndexBuffer(&mesh->IBView);

        const Material* mat = mc->MaterialPath.empty()
                              ? nullptr
                              : matMgr.LoadOrCreate(mc->MaterialPath);

        for (const SubMesh& sm : mesh->SubMeshes) {
            if (slotIdx >= MAX_OBJECTS) break;
            uint8_t* slot = objBase + static_cast<size_t>(slotIdx) * CB_SLOT_SIZE;

            // PerObject CB layout (offsets match Translucency.VS/PS.hlsl):
            //  [  0] WorldViewProj (64B)
            //  [ 64] World         (64B)
            //  [128] material block: AlbedoColor, Metallic, Roughness, AO, _matpad (32B)
            //  [160] Opacity       ( 4B)
            memcpy(slot,      wvp.v,   64);
            memcpy(slot + 64, world.v, 64);

            if (mat && mat->ParamData.size() >= layout.MaterialSize && layout.MaterialSize > 0) {
                memcpy(slot + 128, mat->ParamData.data(), layout.MaterialSize);
            } else {
                float fallback[8] = {
                    sm.BaseColor[0], sm.BaseColor[1], sm.BaseColor[2],
                    0.0f, 0.5f, 1.0f, 0.0f, 0.0f
                };
                memcpy(slot + 128, fallback, sizeof(fallback));
            }

            float opacityVal = mc->Opacity;
            memcpy(slot + 160, &opacityVal, 4);

            D3D12_GPU_VIRTUAL_ADDRESS cbAddr =
                m_objCB[fi]->GetGPUVirtualAddress() + static_cast<UINT64>(slotIdx) * CB_SLOT_SIZE;
            cmd->SetGraphicsRootConstantBufferView(0, cbAddr);

            const std::string& albedoPath = (mat && !mat->AlbedoTexturePath.empty()) ? mat->AlbedoTexturePath : sm.DiffusePath;
            const std::string& normalPath = (mat && !mat->NormalTexturePath.empty()) ? mat->NormalTexturePath : sm.NormalPath;
            const std::string& ormPath    = (mat && !mat->OrmTexturePath.empty())    ? mat->OrmTexturePath    : sm.OrmPath;

            uint32_t albedoSlot = texMgr.LoadTexture(albedoPath);
            uint32_t normalSlot = normalPath.empty() ? texMgr.GetFlatNormalSlot() : texMgr.LoadTexture(normalPath);
            uint32_t ormSlot    = ormPath.empty()    ? texMgr.GetFallbackSlot()   : texMgr.LoadTexture(ormPath);

            cmd->SetGraphicsRootDescriptorTable(4, gfx.GetSRVGPUHandle(albedoSlot));
            cmd->SetGraphicsRootDescriptorTable(5, gfx.GetSRVGPUHandle(normalSlot));
            cmd->SetGraphicsRootDescriptorTable(6, gfx.GetSRVGPUHandle(ormSlot));

            cmd->DrawIndexedInstanced(sm.IndexCount, 1, sm.StartIndex, sm.BaseVertex, 0);
            ++slotIdx;
        }
    }
}

void TranslucencyPass::Shutdown() {
    for (uint32_t i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i) {
        if (m_objMapped[i])    { m_objCB[i]->Unmap(0, nullptr);    m_objMapped[i] = nullptr; }
        if (m_cameraMapped[i]) { m_cameraCB[i]->Unmap(0, nullptr); m_cameraMapped[i] = nullptr; }
        if (m_lightsMapped[i]) { m_lightsCB[i]->Unmap(0, nullptr); m_lightsMapped[i] = nullptr; }
        if (m_shadowMapped[i]) { m_shadowCB[i]->Unmap(0, nullptr); m_shadowMapped[i] = nullptr; }
        m_objCB[i].Reset(); m_cameraCB[i].Reset();
        m_lightsCB[i].Reset(); m_shadowCB[i].Reset();
    }
    m_pso[0].Reset(); m_pso[1].Reset();
    m_rootSignature.Reset();
}

} // namespace Fujin
