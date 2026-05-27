#pragma once
#include "GBuffer.h"
#include "ShadowPass.h"
#include "Engine/Graphics/GraphicsDevice.h"
#include "Engine/Math/Math.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>

namespace Fujin {

class SceneManager;

class LightingPass {
public:
    bool Initialize(GraphicsDevice& gfx);
    void Execute(ID3D12GraphicsCommandList* cmd,
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
                 uint32_t ssaoSRVSlot = 0,
                 float    ssaoStrength = 1.0f,
                 uint32_t scissorX = 0, uint32_t scissorY = 0,
                 uint32_t scissorW = 0, uint32_t scissorH = 0);
    void Shutdown();

private:
    static constexpr uint32_t MAX_LIGHTS     = 16;
    static constexpr uint32_t CAMERA_CB_SIZE = 256;
    static constexpr uint32_t LIGHTS_CB_SIZE = 1280;
    static constexpr uint32_t SHADOW_CB_SIZE = 512; // 4*64B LightViewProj + 16B splits

    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_pso;
    ComPtr<ID3D12Resource>      m_cameraCB[NUM_FRAMES_IN_FLIGHT];
    ComPtr<ID3D12Resource>      m_lightsCB[NUM_FRAMES_IN_FLIGHT];
    ComPtr<ID3D12Resource>      m_shadowCB[NUM_FRAMES_IN_FLIGHT];
    uint8_t*                    m_cameraMapped[NUM_FRAMES_IN_FLIGHT] = {};
    uint8_t*                    m_lightsMapped[NUM_FRAMES_IN_FLIGHT] = {};
    uint8_t*                    m_shadowMapped[NUM_FRAMES_IN_FLIGHT] = {};

    bool CreateRootSignature(ID3D12Device* device);
    bool CreatePipelineState(ID3D12Device* device);
    bool CreateConstantBuffers(ID3D12Device* device);
};

} // namespace Fujin
