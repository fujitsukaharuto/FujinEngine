#include "LightingPass.h"
#include "Engine/Core/SceneManager.h"
#include "Engine/Core/Actor.h"
#include "Engine/Core/LightComponent.h"
#include "Engine/Core/TransformComponent.h"
#include "Engine/Graphics/DxcHelper.h"
#include <stdexcept>

namespace Fujin {

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
        if (!CreateConstantBuffers(gfx.GetDevice())) return false;
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
    D3D12_ROOT_PARAMETER params[12] = {};
    params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_PIXEL;
    params[1].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].Descriptor.ShaderRegister = 1;
    params[1].ShaderVisibility          = D3D12_SHADER_VISIBILITY_PIXEL;
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
    rsDesc.NumParameters     = 12;
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

bool LightingPass::CreateConstantBuffers(ID3D12Device* device) {
    for (uint32_t i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i) {
        m_cameraCB[i] = CreateUploadCB(device, CAMERA_CB_SIZE, &m_cameraMapped[i]);
        m_lightsCB[i] = CreateUploadCB(device, LIGHTS_CB_SIZE, &m_lightsMapped[i]);
        m_shadowCB[i] = CreateUploadCB(device, SHADOW_CB_SIZE, &m_shadowMapped[i]);
        if (!m_cameraCB[i] || !m_lightsCB[i] || !m_shadowCB[i]) return false;
    }
    return true;
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
                            uint32_t scissorW, uint32_t scissorH) {
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

    // Lights CB
    struct GpuLight { float pos[3]; float type; float dir[3]; float range;
                      float color[3]; float intensity; float spotAngle; float pad[3]; };
    uint32_t lightCount = 0;
    GpuLight gpuLights[MAX_LIGHTS] = {};

    for (auto& actorPtr : scene.GetActors()) {
        if (lightCount >= MAX_LIGHTS) break;
        Actor* actor = actorPtr.get();
        auto* lc = actor->GetComponent<LightComponent>();
        if (!lc) continue;
        auto* tc = actor->GetComponent<TransformComponent>();
        GpuLight& gl = gpuLights[lightCount];

        if (tc) {
            gl.pos[0] = tc->Position.x; gl.pos[1] = tc->Position.y; gl.pos[2] = tc->Position.z;
            Matrix4x4 rotMat = tc->Rotation.ToMatrix();
            gl.dir[0] = rotMat.m[2][0]; gl.dir[1] = rotMat.m[2][1]; gl.dir[2] = rotMat.m[2][2];
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
    memcpy(m_lightsMapped[fi] + 16, gpuLights,   sizeof(GpuLight) * lightCount);

    // Shadow CB: [LightViewProj[4](256B)][CascadeSplits(16B)]
    for (uint32_t c = 0; c < ShadowPass::CASCADE_COUNT; ++c)
        memcpy(m_shadowMapped[fi] + c * 64, shadowData.LightViewProj[c].v, 64);
    memcpy(m_shadowMapped[fi] + 256, shadowData.CascadeSplits, 16);

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
    cmd->SetGraphicsRootConstantBufferView(1, m_lightsCB[fi]->GetGPUVirtualAddress());
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

    cmd->DrawInstanced(3, 1, 0, 0);
}

void LightingPass::Shutdown() {
    for (uint32_t i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i) {
        if (m_cameraMapped[i]) { m_cameraCB[i]->Unmap(0, nullptr); m_cameraMapped[i] = nullptr; }
        if (m_lightsMapped[i]) { m_lightsCB[i]->Unmap(0, nullptr); m_lightsMapped[i] = nullptr; }
        if (m_shadowMapped[i]) { m_shadowCB[i]->Unmap(0, nullptr); m_shadowMapped[i] = nullptr; }
        m_cameraCB[i].Reset();
        m_lightsCB[i].Reset();
        m_shadowCB[i].Reset();
    }
    m_pso.Reset();
    m_rootSignature.Reset();
}

} // namespace Fujin
