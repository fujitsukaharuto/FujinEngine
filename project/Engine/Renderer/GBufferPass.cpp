#include "GBufferPass.h"
#include "Material/MaterialManager.h"
#include "Engine/Core/SceneManager.h"
#include "Engine/Core/Actor.h"
#include "Engine/Core/TransformComponent.h"
#include "Engine/Core/MeshComponent.h"
#include "Engine/Core/AnimationComponent.h"
#include "Engine/Asset/SkeletalMeshAsset.h"
#include "Engine/Math/Math.h"
#include "Engine/Graphics/DxcHelper.h"
#include <stdexcept>
#include <cctype>
#include <string>
#include <unordered_set>

namespace Fujin {



bool GBufferPass::Initialize(GraphicsDevice& gfx, const GBuffer& gbuffer) {
    try {
        if (!CreateRootSignature(gfx.GetDevice()))              return false;
        if (!CreatePipelineState(gfx.GetDevice(), gbuffer))     return false;
        if (!CreateConstantBuffers(gfx.GetDevice()))            return false;
        if (!CreateSkinnedRootSignature(gfx.GetDevice()))       return false;
        if (!CreateSkinnedPipelineState(gfx.GetDevice(), gbuffer)) return false;
        if (!CreateSkinnedBuffers(gfx.GetDevice()))             return false;
    } catch (...) {
        return false;
    }
    return true;
}

bool GBufferPass::CreateRootSignature(ID3D12Device* device) {
    D3D12_ROOT_PARAMETER params[4] = {};

    // b0: per-object CB (VS+PS)
    params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].Descriptor.RegisterSpace  = 0;
    params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;

    // t0=albedo, t1=normal, t2=ORM — each as a separate 1-SRV descriptor table
    D3D12_DESCRIPTOR_RANGE ranges[3] = {};
    for (int i = 0; i < 3; ++i) {
        ranges[i].RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[i].NumDescriptors                    = 1;
        ranges[i].BaseShaderRegister                = static_cast<UINT>(i);
        ranges[i].RegisterSpace                     = 0;
        ranges[i].OffsetInDescriptorsFromTableStart = 0;
        params[i + 1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[i + 1].DescriptorTable.NumDescriptorRanges = 1;
        params[i + 1].DescriptorTable.pDescriptorRanges   = &ranges[i];
        params[i + 1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
    }

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.MaxLOD           = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister   = 0;
    sampler.RegisterSpace    = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters     = 4;
    rsDesc.pParameters       = params;
    rsDesc.NumStaticSamplers = 1;
    rsDesc.pStaticSamplers   = &sampler;
    rsDesc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> rsBlob, rsErr;
    D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr);
    return SUCCEEDED(device->CreateRootSignature(0, rsBlob->GetBufferPointer(),
                                                  rsBlob->GetBufferSize(),
                                                  IID_PPV_ARGS(&m_rootSignature)));
}

bool GBufferPass::CreatePSOVariant(ID3D12Device* device, [[maybe_unused]] const GBuffer& gbuffer,
                                    D3D12_CULL_MODE cull, bool alphaClip,
                                    ComPtr<ID3D12PipelineState>& outPso) {
    auto vs = LoadOrCompileShader(L"Resource/Shaders/GBufferPass.VS.hlsl", L"vs_6_0");
    auto ps = alphaClip
        ? LoadOrCompileShader(L"Resource/Shaders/GBufferPass.AlphaClip.PS.hlsl", L"ps_6_0")
        : LoadOrCompileShader(L"Resource/Shaders/GBufferPass.PS.hlsl", L"ps_6_0");

    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_RASTERIZER_DESC rastDesc = {};
    rastDesc.FillMode        = D3D12_FILL_MODE_SOLID;
    rastDesc.CullMode        = cull;
    rastDesc.DepthClipEnable = TRUE;

    D3D12_BLEND_DESC blendDesc = {};
    for (uint32_t i = 0; i < GBuffer::RT_COUNT; ++i)
        blendDesc.RenderTarget[i].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable    = TRUE;
    dsDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    dsDesc.DepthFunc      = D3D12_COMPARISON_FUNC_LESS;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature        = m_rootSignature.Get();
    psoDesc.VS                    = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS                    = { ps->GetBufferPointer(), ps->GetBufferSize() };
    psoDesc.InputLayout           = { inputLayout, 4 };
    psoDesc.RasterizerState       = rastDesc;
    psoDesc.BlendState            = blendDesc;
    psoDesc.DepthStencilState     = dsDesc;
    psoDesc.DSVFormat             = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleMask            = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets      = GBuffer::RT_COUNT;
    for (uint32_t i = 0; i < GBuffer::RT_COUNT; ++i)
        psoDesc.RTVFormats[i]     = GBuffer::RT_FORMATS[i];
    psoDesc.SampleDesc            = { 1, 0 };

    return SUCCEEDED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&outPso)));
}

bool GBufferPass::CreatePipelineState(ID3D12Device* device, [[maybe_unused]] const GBuffer& gbuffer) {
    // 4 variants: index = (doubleSided<<1) | alphaClip
    if (!CreatePSOVariant(device, gbuffer, D3D12_CULL_MODE_BACK, false, m_psoVariants[0])) return false;
    if (!CreatePSOVariant(device, gbuffer, D3D12_CULL_MODE_BACK, true,  m_psoVariants[1])) return false;
    if (!CreatePSOVariant(device, gbuffer, D3D12_CULL_MODE_NONE, false, m_psoVariants[2])) return false;
    if (!CreatePSOVariant(device, gbuffer, D3D12_CULL_MODE_NONE, true,  m_psoVariants[3])) return false;
    return true;
}

bool GBufferPass::CreateConstantBuffers(ID3D12Device* device) {
    const UINT64 totalSize = static_cast<UINT64>(CB_SLOT_SIZE) * MAX_OBJECTS;
    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width              = totalSize;
    rd.Height             = 1;
    rd.DepthOrArraySize   = 1;
    rd.MipLevels          = 1;
    rd.Format             = DXGI_FORMAT_UNKNOWN;
    rd.SampleDesc.Count   = 1;
    rd.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_RANGE range = { 0, 0 };
    for (uint32_t i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i) {
        if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_cbuffer[i]))))
            return false;
        m_cbuffer[i]->Map(0, &range, reinterpret_cast<void**>(&m_cbMapped[i]));
    }
    return true;
}

void GBufferPass::Execute(ID3D12GraphicsCommandList* cmd,
                           GraphicsDevice& gfx,
                           const GBuffer& gbuffer,
                           const SceneManager& scene,
                           uint32_t vpX, uint32_t vpY, uint32_t vpW, uint32_t vpH,
                           uint32_t frameIndex,
                           GeometryManager& geoMgr,
                           TextureManager& texMgr,
                           MaterialManager& matMgr,
                           const Matrix4x4& viewProj,
                           const Matrix4x4& prevViewProj,
                           const std::unordered_set<uint64_t>* visibleActors) {

    // Bind GBuffer RTVs + main depth
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[GBuffer::RT_COUNT];
    for (uint32_t i = 0; i < GBuffer::RT_COUNT; ++i)
        rtvHandles[i] = gbuffer.GetRTV(i);
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = gfx.GetCurrentDSV();
    cmd->OMSetRenderTargets(GBuffer::RT_COUNT, rtvHandles, FALSE, &dsv);

    D3D12_VIEWPORT vp = { static_cast<float>(vpX), static_cast<float>(vpY),
                          static_cast<float>(vpW), static_cast<float>(vpH), 0.0f, 1.0f };
    D3D12_RECT scissor = { static_cast<LONG>(vpX), static_cast<LONG>(vpY),
                           static_cast<LONG>(vpX + vpW), static_cast<LONG>(vpY + vpH) };
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &scissor);
    cmd->SetGraphicsRootSignature(m_rootSignature.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    uint32_t fi      = frameIndex % NUM_FRAMES_IN_FLIGHT;
    uint8_t* cbBase  = m_cbMapped[fi];
    uint32_t slotIdx = 0;

    ID3D12PipelineState* curStaticPso = nullptr;

    for (auto& actorPtr : scene.GetActors()) {
        Actor* actor = actorPtr.get();
        if (!actor->HasComponent<MeshComponent>()) continue;

        // Frustum culling (static opaque meshes only; skinned/translucent handled elsewhere).
        if (visibleActors && !visibleActors->count(actor->GetId())) continue;

        auto* meshComp = actor->GetComponent<MeshComponent>();
        if (geoMgr.IsSkeletal(meshComp->MeshPath)) continue;  // handled by skinned pass

        if (meshComp->Blend == MeshBlendMode::Translucent) continue;  // handled by TranslucencyPass

        const MeshAsset* mesh = geoMgr.LoadMesh(meshComp->MeshPath);
        if (!mesh || mesh->SubMeshes.empty()) continue;

        // Switch PSO variant based on per-actor flags.
        int psoIdx = (meshComp->DoubleSided ? 2 : 0) | (meshComp->Blend == MeshBlendMode::AlphaClip ? 1 : 0);
        ID3D12PipelineState* pso = m_psoVariants[psoIdx].Get();
        if (pso != curStaticPso) {
            cmd->SetPipelineState(pso);
            curStaticPso = pso;
        }

        auto* t = actor->GetComponent<TransformComponent>();
        Matrix4x4 world = t ? t->GetWorldMatrix() : Matrix4x4::Identity;
        Matrix4x4 wvp   = viewProj * world;

        // Motion vectors: previous-frame world-view-proj (last frame's world for this actor).
        uint64_t  actorId = actor->GetId();
        auto      pwIt    = m_prevWorld.find(actorId);
        Matrix4x4 prevWvp = prevViewProj * (pwIt != m_prevWorld.end() ? pwIt->second : world);
        m_prevWorld[actorId] = world;

        cmd->IASetVertexBuffers(0, 1, &mesh->VBView);
        cmd->IASetIndexBuffer(&mesh->IBView);

        // Resolve material for this actor (cached after first load).
        const Material* mat = meshComp->MaterialPath.empty()
                              ? nullptr
                              : matMgr.LoadOrCreate(meshComp->MaterialPath);

        const CBLayout& layout = matMgr.GetLayout();

        for (const SubMesh& sm : mesh->SubMeshes) {
            if (slotIdx >= MAX_OBJECTS) break;
            uint8_t* slot = cbBase + static_cast<size_t>(slotIdx) * CB_SLOT_SIZE;
            memcpy(slot,       wvp.v,     64);   // WorldViewProj      @0
            memcpy(slot + 64,  world.v,   64);   // World             @64
            memcpy(slot + 128, prevWvp.v, 64);   // PrevWorldViewProj @128

            // Write material params from reflected layout (now @192), or fall back to SubMesh defaults.
            if (mat && mat->ParamData.size() >= layout.MaterialSize && layout.MaterialSize > 0) {
                memcpy(slot + layout.MaterialOffset, mat->ParamData.data(), layout.MaterialSize);
            } else {
                // Fallback: use SubMesh base color with neutral PBR values.
                float fallback[8] = {
                    sm.BaseColor[0], sm.BaseColor[1], sm.BaseColor[2],
                    0.0f, 0.5f, 1.0f, 0.0f, 0.0f
                };
                memcpy(slot + layout.MaterialOffset, fallback, sizeof(fallback));
            }

            D3D12_GPU_VIRTUAL_ADDRESS cbAddr =
                m_cbuffer[fi]->GetGPUVirtualAddress() + static_cast<UINT64>(slotIdx) * CB_SLOT_SIZE;
            cmd->SetGraphicsRootConstantBufferView(0, cbAddr);

            const std::string& albedoPath = (mat && !mat->AlbedoTexturePath.empty()) ? mat->AlbedoTexturePath : sm.DiffusePath;
            const std::string& normalPath = (mat && !mat->NormalTexturePath.empty()) ? mat->NormalTexturePath : sm.NormalPath;
            const std::string& ormPath    = (mat && !mat->OrmTexturePath.empty())    ? mat->OrmTexturePath    : sm.OrmPath;

            uint32_t albedoSlot = texMgr.LoadTexture(albedoPath);
            uint32_t normalSlot = normalPath.empty() ? texMgr.GetFlatNormalSlot() : texMgr.LoadTexture(normalPath);
            uint32_t ormSlot    = ormPath.empty()    ? texMgr.GetFallbackSlot()   : texMgr.LoadTexture(ormPath);

            cmd->SetGraphicsRootDescriptorTable(1, gfx.GetSRVGPUHandle(albedoSlot));
            cmd->SetGraphicsRootDescriptorTable(2, gfx.GetSRVGPUHandle(normalSlot));
            cmd->SetGraphicsRootDescriptorTable(3, gfx.GetSRVGPUHandle(ormSlot));
            cmd->DrawIndexedInstanced(sm.IndexCount, 1, sm.StartIndex, sm.BaseVertex, 0);
            ++slotIdx;
        }
    }

    // ---- Skinned mesh pass (.gltf / .glb) ----
    cmd->SetPipelineState(m_skinnedPso.Get());
    cmd->SetGraphicsRootSignature(m_skinnedRootSig.Get());

    // Reuse viewport/scissor from above (already set).

    static const Matrix4x4 s_identityPalette[MAX_BONES] = {};  // default-constructed = Identity

    uint8_t* sCBBase      = m_skinnedCBMapped[fi];
    uint32_t actorPalSlot = 0;  // one slot per skinned actor (transforms + palette)

    for (auto& actorPtr : scene.GetActors()) {
        Actor* actor = actorPtr.get();
        auto*  mc    = actor->GetComponent<MeshComponent>();
        if (!mc || mc->MeshPath.empty()) continue;

        if (!geoMgr.IsSkeletal(mc->MeshPath)) continue;
        if (actorPalSlot >= MAX_SKINNED_ACTORS) break;

        const SkeletalMeshAsset* mesh = geoMgr.LoadSkeletalMesh(mc->MeshPath);
        if (!mesh || mesh->SubMeshes.empty()) { ++actorPalSlot; continue; }

        auto* t = actor->GetComponent<TransformComponent>();
        Matrix4x4 world = t ? t->GetWorldMatrix() : Matrix4x4::Identity;
        Matrix4x4 wvp   = viewProj * world;

        // Motion vectors: previous-frame world-view-proj (last frame's world for this actor).
        uint64_t  actorId = actor->GetId();
        auto      pwIt    = m_prevWorld.find(actorId);
        Matrix4x4 prevWvp = prevViewProj * (pwIt != m_prevWorld.end() ? pwIt->second : world);
        m_prevWorld[actorId] = world;

        // Per-actor transform CB: wvp + world + prevWvp, bound once for all submeshes
        uint8_t* transformSlot = sCBBase + static_cast<size_t>(actorPalSlot) * CB_SLOT_SIZE;
        memcpy(transformSlot,       wvp.v,     64);
        memcpy(transformSlot + 64,  world.v,   64);
        memcpy(transformSlot + 128, prevWvp.v, 64);
        D3D12_GPU_VIRTUAL_ADDRESS actorCBAddr =
            m_skinnedCB[fi]->GetGPUVirtualAddress() + static_cast<UINT64>(actorPalSlot) * CB_SLOT_SIZE;

        // Bone palette CBs — current and previous frame, written once per actor, shared across submeshes.
        auto*            animComp = actor->GetComponent<AnimationComponent>();
        const Matrix4x4* palette  = (animComp && animComp->PaletteReady)
                                        ? animComp->BonePalette.data()
                                        : s_identityPalette;
        const Matrix4x4* prevPalette = (animComp && animComp->PaletteReady)
                                        ? animComp->PrevBonePalette.data()
                                        : s_identityPalette;
        uint8_t* palPtr = m_bonePaletteMapped[fi] + static_cast<size_t>(actorPalSlot) * BONE_PALETTE_STRIDE;
        memcpy(palPtr, palette, BONE_PALETTE_STRIDE);
        D3D12_GPU_VIRTUAL_ADDRESS palAddr =
            m_bonePaletteCB[fi]->GetGPUVirtualAddress() + static_cast<UINT64>(actorPalSlot) * BONE_PALETTE_STRIDE;

        uint8_t* prevPalPtr = m_prevBonePaletteMapped[fi] + static_cast<size_t>(actorPalSlot) * BONE_PALETTE_STRIDE;
        memcpy(prevPalPtr, prevPalette, BONE_PALETTE_STRIDE);
        D3D12_GPU_VIRTUAL_ADDRESS prevPalAddr =
            m_prevBonePaletteCB[fi]->GetGPUVirtualAddress() + static_cast<UINT64>(actorPalSlot) * BONE_PALETTE_STRIDE;

        cmd->IASetVertexBuffers(0, 1, &mesh->VBView);
        cmd->IASetIndexBuffer(&mesh->IBView);
        cmd->SetGraphicsRootConstantBufferView(0, actorCBAddr);
        cmd->SetGraphicsRootConstantBufferView(1, palAddr);
        cmd->SetGraphicsRootConstantBufferView(6, prevPalAddr);

        const Material* mat = mc->MaterialPath.empty()
                              ? nullptr
                              : matMgr.LoadOrCreate(mc->MaterialPath);

        const CBLayout& layout = matMgr.GetLayout();
        // Skinned pass uses root constants (b2) which match the same material param layout.
        static float s_fallbackMat[8] = {};

        for (const SubMesh& sm : mesh->SubMeshes) {
            if (mat && mat->ParamData.size() >= layout.MaterialSize && layout.MaterialSize > 0) {
                // ParamData and root-constant slots share the same 32-byte material layout.
                cmd->SetGraphicsRoot32BitConstants(2, layout.MaterialSize / 4,
                                                   mat->ParamData.data(), 0);
            } else {
                s_fallbackMat[0] = sm.BaseColor[0];
                s_fallbackMat[1] = sm.BaseColor[1];
                s_fallbackMat[2] = sm.BaseColor[2];
                s_fallbackMat[3] = 0.0f; // Metallic
                s_fallbackMat[4] = 0.5f; // Roughness
                s_fallbackMat[5] = 1.0f; // AO
                cmd->SetGraphicsRoot32BitConstants(2, 8, s_fallbackMat, 0);
            }

            const std::string& albedoPath = (mat && !mat->AlbedoTexturePath.empty()) ? mat->AlbedoTexturePath : sm.DiffusePath;
            const std::string& normalPath = (mat && !mat->NormalTexturePath.empty()) ? mat->NormalTexturePath : sm.NormalPath;
            const std::string& ormPath    = (mat && !mat->OrmTexturePath.empty())    ? mat->OrmTexturePath    : sm.OrmPath;

            uint32_t albedoSlot = texMgr.LoadTexture(albedoPath);
            uint32_t normalSlot = normalPath.empty() ? texMgr.GetFlatNormalSlot() : texMgr.LoadTexture(normalPath);
            uint32_t ormSlot    = ormPath.empty()    ? texMgr.GetFallbackSlot()   : texMgr.LoadTexture(ormPath);

            cmd->SetGraphicsRootDescriptorTable(3, gfx.GetSRVGPUHandle(albedoSlot));
            cmd->SetGraphicsRootDescriptorTable(4, gfx.GetSRVGPUHandle(normalSlot));
            cmd->SetGraphicsRootDescriptorTable(5, gfx.GetSRVGPUHandle(ormSlot));
            cmd->DrawIndexedInstanced(sm.IndexCount, 1, sm.StartIndex, sm.BaseVertex, 0);
        }
        ++actorPalSlot;
    }

    // Evict motion-vector history for actors that no longer exist. Actor ids are never reused
    // (SceneManager::m_nextId only increments), so without this the map grows by one entry for
    // every actor ever destroyed over a long edit/play session.
    {
        std::unordered_set<uint64_t> live;
        live.reserve(scene.GetActors().size());
        for (auto& ap : scene.GetActors()) live.insert(ap->GetId());
        for (auto it = m_prevWorld.begin(); it != m_prevWorld.end(); )
            if (live.count(it->first)) ++it; else it = m_prevWorld.erase(it);
    }
}

void GBufferPass::Shutdown() {
    for (uint32_t i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i) {
        if (m_cbMapped[i])           { m_cbuffer[i]->Unmap(0, nullptr);       m_cbMapped[i] = nullptr; }
        if (m_skinnedCBMapped[i])    { m_skinnedCB[i]->Unmap(0, nullptr);     m_skinnedCBMapped[i] = nullptr; }
        if (m_bonePaletteMapped[i])  { m_bonePaletteCB[i]->Unmap(0, nullptr); m_bonePaletteMapped[i] = nullptr; }
        if (m_prevBonePaletteMapped[i]) { m_prevBonePaletteCB[i]->Unmap(0, nullptr); m_prevBonePaletteMapped[i] = nullptr; }
        m_cbuffer[i].Reset();
        m_skinnedCB[i].Reset();
        m_bonePaletteCB[i].Reset();
        m_prevBonePaletteCB[i].Reset();
    }
    for (auto& pso : m_psoVariants) pso.Reset();
    m_rootSignature.Reset();
    m_skinnedPso.Reset();
    m_skinnedRootSig.Reset();
}

// ---------------------------------------------------------------------------
// Skinned root signature: b0 (per-obj), b1 (bone palette), t0 (albedo)
// ---------------------------------------------------------------------------

bool GBufferPass::CreateSkinnedRootSignature(ID3D12Device* device) {
    D3D12_ROOT_PARAMETER params[7] = {};

    // b0: per-actor transforms (wvp + world + prevWvp) — VS only
    params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;

    // b1: bone palette — VS only
    params[1].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].Descriptor.ShaderRegister = 1;
    params[1].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;

    // b3: previous-frame bone palette (skeletal motion vectors) — VS only
    params[6].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[6].Descriptor.ShaderRegister = 3;
    params[6].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;

    // b2: per-submesh material (root constants, 8 dwords) — PS only
    params[2].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[2].Constants.ShaderRegister = 2;
    params[2].Constants.RegisterSpace  = 0;
    params[2].Constants.Num32BitValues = 8;
    params[2].ShaderVisibility         = D3D12_SHADER_VISIBILITY_PIXEL;

    // t0=albedo, t1=normal, t2=ORM — each as a separate 1-SRV descriptor table
    D3D12_DESCRIPTOR_RANGE ranges[3] = {};
    for (int i = 0; i < 3; ++i) {
        ranges[i].RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[i].NumDescriptors                    = 1;
        ranges[i].BaseShaderRegister                = static_cast<UINT>(i);
        ranges[i].RegisterSpace                     = 0;
        ranges[i].OffsetInDescriptorsFromTableStart = 0;
        params[i + 3].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[i + 3].DescriptorTable.NumDescriptorRanges = 1;
        params[i + 3].DescriptorTable.pDescriptorRanges   = &ranges[i];
        params[i + 3].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
    }

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter         = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.MaxLOD         = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters     = 7;
    rsDesc.pParameters       = params;
    rsDesc.NumStaticSamplers = 1;
    rsDesc.pStaticSamplers   = &sampler;
    rsDesc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> rsBlob, rsErr;
    D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr);
    return SUCCEEDED(device->CreateRootSignature(0, rsBlob->GetBufferPointer(),
                                                  rsBlob->GetBufferSize(),
                                                  IID_PPV_ARGS(&m_skinnedRootSig)));
}

bool GBufferPass::CreateSkinnedPipelineState(ID3D12Device* device, [[maybe_unused]] const GBuffer& gbuffer) {
    auto vs = LoadOrCompileShader(L"Resource/Shaders/GBufferPassSkinned.VS.hlsl", L"vs_6_0");
    auto ps = LoadOrCompileShader(L"Resource/Shaders/GBufferPassSkinned.PS.hlsl", L"ps_6_0");

    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT",      0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT,       0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_UINT,  0, 44, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 60, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_RASTERIZER_DESC rastDesc = {};
    rastDesc.FillMode        = D3D12_FILL_MODE_SOLID;
    rastDesc.CullMode        = D3D12_CULL_MODE_BACK;
    rastDesc.DepthClipEnable = TRUE;

    D3D12_BLEND_DESC blendDesc = {};
    for (uint32_t i = 0; i < GBuffer::RT_COUNT; ++i)
        blendDesc.RenderTarget[i].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable    = TRUE;
    dsDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    dsDesc.DepthFunc      = D3D12_COMPARISON_FUNC_LESS;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature        = m_skinnedRootSig.Get();
    psoDesc.VS                    = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS                    = { ps->GetBufferPointer(), ps->GetBufferSize() };
    psoDesc.InputLayout           = { inputLayout, 6 };
    psoDesc.RasterizerState       = rastDesc;
    psoDesc.BlendState            = blendDesc;
    psoDesc.DepthStencilState     = dsDesc;
    psoDesc.DSVFormat             = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleMask            = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets      = GBuffer::RT_COUNT;
    for (uint32_t i = 0; i < GBuffer::RT_COUNT; ++i)
        psoDesc.RTVFormats[i]     = GBuffer::RT_FORMATS[i];
    psoDesc.SampleDesc            = { 1, 0 };

    return SUCCEEDED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_skinnedPso)));
}

bool GBufferPass::CreateSkinnedBuffers(ID3D12Device* device) {
    // Per-actor transform CB: one 256-byte slot (wvp+world) per actor
    {
        const UINT64 totalSize = static_cast<UINT64>(CB_SLOT_SIZE) * MAX_SKINNED_ACTORS;
        D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC rd = {};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width = totalSize;
        rd.Height = rd.DepthOrArraySize = rd.MipLevels = 1;
        rd.Format = DXGI_FORMAT_UNKNOWN; rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_RANGE range = { 0, 0 };
        for (uint32_t i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i) {
            if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_skinnedCB[i]))))
                return false;
            m_skinnedCB[i]->Map(0, &range, reinterpret_cast<void**>(&m_skinnedCBMapped[i]));
        }
    }
    // Bone palette CB (current + previous frame, for skeletal motion vectors)
    {
        const UINT64 totalSize = static_cast<UINT64>(BONE_PALETTE_STRIDE) * MAX_SKINNED_ACTORS;
        D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC rd = {};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width = totalSize;
        rd.Height = rd.DepthOrArraySize = rd.MipLevels = 1;
        rd.Format = DXGI_FORMAT_UNKNOWN; rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_RANGE range = { 0, 0 };
        for (uint32_t i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i) {
            if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_bonePaletteCB[i]))))
                return false;
            m_bonePaletteCB[i]->Map(0, &range, reinterpret_cast<void**>(&m_bonePaletteMapped[i]));
            if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_prevBonePaletteCB[i]))))
                return false;
            m_prevBonePaletteCB[i]->Map(0, &range, reinterpret_cast<void**>(&m_prevBonePaletteMapped[i]));
        }
    }
    return true;
}

} // namespace Fujin
