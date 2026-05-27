#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>
#include "Engine/Graphics/GraphicsDevice.h"
#include "Engine/Math/Vector.h"

namespace Fujin {

using Microsoft::WRL::ComPtr;

class SceneManager;

class MeshRenderer {
public:
    bool Initialize(ID3D12Device* device, DXGI_FORMAT rtvFormat);
    void Render(ID3D12GraphicsCommandList* cmdList,
                const SceneManager& scene,
                uint32_t width, uint32_t height,
                uint32_t frameIndex);
    void Shutdown();

    // Camera (adjustable at runtime; Phase 3 will replace with a camera component)
    Vector3 CameraPos    = Vector3( 0.0f, 3.0f, -5.0f);
    Vector3 CameraTarget = Vector3( 0.0f, 0.0f,  5.0f);

private:
    static constexpr uint32_t MAX_OBJECTS  = 64;
    static constexpr uint32_t CB_SLOT_SIZE = 256; // per-object CB slot (256-byte aligned)

    bool CreateCubeGeometry(ID3D12Device* device);
    bool CreatePipeline(ID3D12Device* device, DXGI_FORMAT rtvFormat);
    bool CreateConstantBuffers(ID3D12Device* device);

    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_pso;
    ComPtr<ID3D12Resource>      m_vertexBuffer;
    ComPtr<ID3D12Resource>      m_indexBuffer;
    D3D12_VERTEX_BUFFER_VIEW    m_vbView = {};
    D3D12_INDEX_BUFFER_VIEW     m_ibView = {};

    // Double-buffered constant buffer (one per frame in flight)
    ComPtr<ID3D12Resource>      m_cbuffer[NUM_FRAMES_IN_FLIGHT];
    uint8_t*                    m_cbMapped[NUM_FRAMES_IN_FLIGHT] = {};
};

} // namespace Fujin
