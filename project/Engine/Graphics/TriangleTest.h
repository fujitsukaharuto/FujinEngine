#pragma once
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

namespace Fujin {

using Microsoft::WRL::ComPtr;

class TriangleTest {
public:
    bool Initialize(ID3D12Device* device, DXGI_FORMAT rtvFormat);
    void Render(ID3D12GraphicsCommandList* cmdList, UINT width, UINT height);
    void Shutdown();

private:
    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_pso;
};

} // namespace Fujin
