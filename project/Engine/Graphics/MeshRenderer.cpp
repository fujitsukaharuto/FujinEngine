#include "MeshRenderer.h"
#include "Engine/Core/SceneManager.h"
#include "Engine/Core/Actor.h"
#include "Engine/Core/TransformComponent.h"
#include "Engine/Core/MeshComponent.h"
#include "Engine/Math/Math.h"
#include <d3dcompiler.h>
#include <cstring>
#include <stdexcept>

#pragma comment(lib, "d3dcompiler.lib")

namespace Fujin {

// ---- Cube geometry ----
struct Vertex { float px, py, pz, nx, ny, nz; };

static const Vertex s_cubeVerts[24] = {
    // Front  (z = +0.5)
    { -0.5f, -0.5f,  0.5f,  0,  0,  1 }, {  0.5f, -0.5f,  0.5f,  0,  0,  1 },
    {  0.5f,  0.5f,  0.5f,  0,  0,  1 }, { -0.5f,  0.5f,  0.5f,  0,  0,  1 },
    // Back   (z = -0.5)
    {  0.5f, -0.5f, -0.5f,  0,  0, -1 }, { -0.5f, -0.5f, -0.5f,  0,  0, -1 },
    { -0.5f,  0.5f, -0.5f,  0,  0, -1 }, {  0.5f,  0.5f, -0.5f,  0,  0, -1 },
    // Top    (y = +0.5)
    { -0.5f,  0.5f,  0.5f,  0,  1,  0 }, {  0.5f,  0.5f,  0.5f,  0,  1,  0 },
    {  0.5f,  0.5f, -0.5f,  0,  1,  0 }, { -0.5f,  0.5f, -0.5f,  0,  1,  0 },
    // Bottom (y = -0.5)
    { -0.5f, -0.5f, -0.5f,  0, -1,  0 }, {  0.5f, -0.5f, -0.5f,  0, -1,  0 },
    {  0.5f, -0.5f,  0.5f,  0, -1,  0 }, { -0.5f, -0.5f,  0.5f,  0, -1,  0 },
    // Right  (x = +0.5)
    {  0.5f, -0.5f,  0.5f,  1,  0,  0 }, {  0.5f, -0.5f, -0.5f,  1,  0,  0 },
    {  0.5f,  0.5f, -0.5f,  1,  0,  0 }, {  0.5f,  0.5f,  0.5f,  1,  0,  0 },
    // Left   (x = -0.5)
    { -0.5f, -0.5f, -0.5f, -1,  0,  0 }, { -0.5f, -0.5f,  0.5f, -1,  0,  0 },
    { -0.5f,  0.5f,  0.5f, -1,  0,  0 }, { -0.5f,  0.5f, -0.5f, -1,  0,  0 },
};

static const uint16_t s_cubeIndices[36] = {
     0,  1,  2,   0,  2,  3,
     4,  5,  6,   4,  6,  7,
     8,  9, 10,   8, 10, 11,
    12, 13, 14,  12, 14, 15,
    16, 17, 18,  16, 18, 19,
    20, 21, 22,  20, 22, 23,
};

// ---- HLSL shaders ----
static const char* s_vsCode = R"(
cbuffer PerObject : register(b0) {
    row_major float4x4 WorldViewProj;
    row_major float4x4 World;
};
struct VSIn  { float3 Pos : POSITION; float3 Normal : NORMAL; };
struct VSOut { float4 Sv : SV_POSITION; float3 Normal : NORMAL; };
VSOut main(VSIn v) {
    VSOut o;
    o.Sv     = mul(WorldViewProj, float4(v.Pos, 1.0));
    o.Normal = normalize(mul((float3x3)World, v.Normal));
    return o;
}
)";

static const char* s_psCode = R"(
float4 main(float4 sv : SV_POSITION, float3 normal : NORMAL) : SV_TARGET {
    float3 L = normalize(float3(1.0, 2.0, -1.0));
    float  d = saturate(dot(normalize(normal), L));
    float3 c = float3(0.55, 0.65, 0.9) * (0.2 + 0.8 * d);
    return float4(c, 1.0);
}
)";

// ---- Helpers ----
static ComPtr<ID3DBlob> CompileShader(const char* src, const char* target) {
#ifdef _DEBUGMODE
    UINT flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    UINT flags = 0;
#endif
    ComPtr<ID3DBlob> blob, err;
    HRESULT hr = D3DCompile(src, strlen(src), nullptr, nullptr, nullptr,
                             "main", target, flags, 0, &blob, &err);
    if (FAILED(hr)) {
        if (err) OutputDebugStringA(static_cast<const char*>(err->GetBufferPointer()));
        throw std::runtime_error("shader compile failed");
    }
    return blob;
}

static ComPtr<ID3D12Resource> CreateUploadBuffer(ID3D12Device* device, UINT64 size) {
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
    device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&res));
    return res;
}

// ---- MeshRenderer ----
bool MeshRenderer::Initialize(ID3D12Device* device, DXGI_FORMAT rtvFormat) {
    try {
        if (!CreateCubeGeometry(device))     return false;
        if (!CreatePipeline(device, rtvFormat)) return false;
        if (!CreateConstantBuffers(device))  return false;
    } catch (...) {
        return false;
    }
    return true;
}

bool MeshRenderer::CreateCubeGeometry(ID3D12Device* device) {
    const UINT64 vbSize = sizeof(s_cubeVerts);
    const UINT64 ibSize = sizeof(s_cubeIndices);

    m_vertexBuffer = CreateUploadBuffer(device, vbSize);
    m_indexBuffer  = CreateUploadBuffer(device, ibSize);

    void* mapped = nullptr;
    D3D12_RANGE range = { 0, 0 };

    m_vertexBuffer->Map(0, &range, &mapped);
    memcpy(mapped, s_cubeVerts, vbSize);
    m_vertexBuffer->Unmap(0, nullptr);

    m_indexBuffer->Map(0, &range, &mapped);
    memcpy(mapped, s_cubeIndices, ibSize);
    m_indexBuffer->Unmap(0, nullptr);

    m_vbView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
    m_vbView.SizeInBytes    = static_cast<UINT>(vbSize);
    m_vbView.StrideInBytes  = sizeof(Vertex);

    m_ibView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
    m_ibView.SizeInBytes    = static_cast<UINT>(ibSize);
    m_ibView.Format         = DXGI_FORMAT_R16_UINT;
    return true;
}

bool MeshRenderer::CreatePipeline(ID3D12Device* device, DXGI_FORMAT rtvFormat) {
    // Root signature: one root CBV (b0)
    D3D12_ROOT_PARAMETER rootParam = {};
    rootParam.ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParam.Descriptor.ShaderRegister = 0;
    rootParam.Descriptor.RegisterSpace  = 0;
    rootParam.ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters = 1;
    rsDesc.pParameters   = &rootParam;
    rsDesc.Flags         = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> rsBlob, rsErr;
    D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr);
    device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(),
                                IID_PPV_ARGS(&m_rootSignature));

    auto vs = CompileShader(s_vsCode, "vs_5_0");
    auto ps = CompileShader(s_psCode, "ps_5_0");

    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_RASTERIZER_DESC rastDesc = {};
    rastDesc.FillMode        = D3D12_FILL_MODE_SOLID;
    rastDesc.CullMode        = D3D12_CULL_MODE_BACK;
    rastDesc.FrontCounterClockwise = FALSE;
    rastDesc.DepthClipEnable = TRUE;

    D3D12_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable    = TRUE;
    dsDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    dsDesc.DepthFunc      = D3D12_COMPARISON_FUNC_LESS;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature        = m_rootSignature.Get();
    psoDesc.VS                    = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS                    = { ps->GetBufferPointer(), ps->GetBufferSize() };
    psoDesc.InputLayout           = { inputLayout, 2 };
    psoDesc.RasterizerState       = rastDesc;
    psoDesc.BlendState            = blendDesc;
    psoDesc.DepthStencilState     = dsDesc;
    psoDesc.DSVFormat             = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleMask            = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets      = 1;
    psoDesc.RTVFormats[0]         = rtvFormat;
    psoDesc.SampleDesc            = { 1, 0 };

    device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pso));
    return true;
}

bool MeshRenderer::CreateConstantBuffers(ID3D12Device* device) {
    const UINT64 totalSize = static_cast<UINT64>(CB_SLOT_SIZE) * MAX_OBJECTS;
    D3D12_RANGE range = { 0, 0 };
    for (uint32_t i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i) {
        m_cbuffer[i] = CreateUploadBuffer(device, totalSize);
        m_cbuffer[i]->Map(0, &range, reinterpret_cast<void**>(&m_cbMapped[i]));
    }
    return true;
}

void MeshRenderer::Render(ID3D12GraphicsCommandList* cmdList,
                           const SceneManager& scene,
                           uint32_t width, uint32_t height,
                           uint32_t frameIndex) {
    // Camera matrices
    float aspect = (height > 0) ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;
    Matrix4x4 view = Matrix4x4::LookAt(CameraPos, CameraTarget, Vector3(0, 1, 0));
    Matrix4x4 proj = Matrix4x4::Perspective(Math::ToRadians(60.0f), aspect, 0.1f, 1000.0f);
    Matrix4x4 viewProj = proj * view;

    // Setup pipeline
    D3D12_VIEWPORT vp     = { 0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1 };
    D3D12_RECT     scissor = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };

    cmdList->SetPipelineState(m_pso.Get());
    cmdList->SetGraphicsRootSignature(m_rootSignature.Get());
    cmdList->RSSetViewports(1, &vp);
    cmdList->RSSetScissorRects(1, &scissor);
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->IASetVertexBuffers(0, 1, &m_vbView);
    cmdList->IASetIndexBuffer(&m_ibView);

    uint8_t* cbBase = m_cbMapped[frameIndex % NUM_FRAMES_IN_FLIGHT];
    uint32_t slotIndex = 0;

    for (auto& actorPtr : scene.GetActors()) {
        Actor* actor = actorPtr.get();
        if (!actor->HasComponent<MeshComponent>()) continue;
        if (slotIndex >= MAX_OBJECTS) break;

        auto* t = actor->GetComponent<TransformComponent>();
        Matrix4x4 world = t ? t->GetWorldMatrix() : Matrix4x4::Identity;
        Matrix4x4 wvp   = viewProj * world;

        // Write CB slot: [WorldViewProj(64B)][World(64B)] + pad to 256B
        uint8_t* slot = cbBase + static_cast<size_t>(slotIndex) * CB_SLOT_SIZE;
        memcpy(slot,      wvp.v,   sizeof(float) * 16);
        memcpy(slot + 64, world.v, sizeof(float) * 16);

        D3D12_GPU_VIRTUAL_ADDRESS cbAddr =
            m_cbuffer[frameIndex % NUM_FRAMES_IN_FLIGHT]->GetGPUVirtualAddress()
            + static_cast<UINT64>(slotIndex) * CB_SLOT_SIZE;

        cmdList->SetGraphicsRootConstantBufferView(0, cbAddr);
        cmdList->DrawIndexedInstanced(36, 1, 0, 0, 0);

        ++slotIndex;
    }
}

void MeshRenderer::Shutdown() {
    for (uint32_t i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i) {
        if (m_cbMapped[i]) {
            m_cbuffer[i]->Unmap(0, nullptr);
            m_cbMapped[i] = nullptr;
        }
        m_cbuffer[i].Reset();
    }
    m_pso.Reset();
    m_rootSignature.Reset();
    m_vertexBuffer.Reset();
    m_indexBuffer.Reset();
}

} // namespace Fujin
