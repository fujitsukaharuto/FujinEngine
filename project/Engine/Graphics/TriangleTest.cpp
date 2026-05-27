#include "TriangleTest.h"
#include <d3dcompiler.h>
#include <stdexcept>

#pragma comment(lib, "d3dcompiler.lib")

namespace Fujin {

static const char* s_vsCode = R"(
float4 main(uint id : SV_VertexID) : SV_POSITION
{
    float2 pos[3] = {
        float2( 0.0f,  0.5f),
        float2( 0.5f, -0.5f),
        float2(-0.5f, -0.5f)
    };
    return float4(pos[id], 0.0f, 1.0f);
}
)";

static const char* s_psCode = R"(
float4 main() : SV_TARGET
{
    return float4(1.0f, 0.5f, 0.0f, 1.0f);
}
)";

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

bool TriangleTest::Initialize(ID3D12Device* device, DXGI_FORMAT rtvFormat) {
    try {
        // Empty root signature
        D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
        rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> rsBlob, rsErr;
        D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr);
        device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(),
                                    IID_PPV_ARGS(&m_rootSignature));

        auto vs = CompileShader(s_vsCode, "vs_5_0");
        auto ps = CompileShader(s_psCode, "ps_5_0");

        D3D12_RASTERIZER_DESC rastDesc = {};
        rastDesc.FillMode        = D3D12_FILL_MODE_SOLID;
        rastDesc.CullMode        = D3D12_CULL_MODE_NONE;
        rastDesc.DepthClipEnable = TRUE;

        D3D12_BLEND_DESC blendDesc = {};
        blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature        = m_rootSignature.Get();
        psoDesc.VS                    = { vs->GetBufferPointer(), vs->GetBufferSize() };
        psoDesc.PS                    = { ps->GetBufferPointer(), ps->GetBufferSize() };
        psoDesc.RasterizerState       = rastDesc;
        psoDesc.BlendState            = blendDesc;
        psoDesc.DepthStencilState     = {};
        psoDesc.SampleMask            = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets      = 1;
        psoDesc.RTVFormats[0]         = rtvFormat;
        psoDesc.SampleDesc            = { 1, 0 };

        device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pso));
    } catch (...) {
        return false;
    }
    return true;
}

void TriangleTest::Render(ID3D12GraphicsCommandList* cmdList, UINT width, UINT height) {
    D3D12_VIEWPORT vp = { 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
    D3D12_RECT     scissor = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };

    cmdList->SetPipelineState(m_pso.Get());
    cmdList->SetGraphicsRootSignature(m_rootSignature.Get());
    cmdList->RSSetViewports(1, &vp);
    cmdList->RSSetScissorRects(1, &scissor);
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->DrawInstanced(3, 1, 0, 0);
}

void TriangleTest::Shutdown() {
    m_pso.Reset();
    m_rootSignature.Reset();
}

} // namespace Fujin
