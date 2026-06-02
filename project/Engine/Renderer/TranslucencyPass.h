#pragma once
#include "ShadowPass.h"
#include "Engine/Graphics/GraphicsDevice.h"
#include "Engine/Asset/GeometryManager.h"
#include "Engine/Asset/TextureManager.h"
#include "Engine/Renderer/Material/MaterialManager.h"
#include "Engine/Math/Math.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>

namespace Fujin {

using Microsoft::WRL::ComPtr;

class SceneManager;

class TranslucencyPass {
public:
    bool Initialize(GraphicsDevice& gfx);
    void Shutdown();

    // Call after LightingPass and before ParticlePass.
    // Renders MeshBlendMode::Translucent meshes forward into the HDR RT, sorted back-to-front.
    // Depth buffer must be in DEPTH_WRITE state (depth test on, depth write off in PSO).
    void Execute(ID3D12GraphicsCommandList* cmd,
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
                 uint32_t vpX, uint32_t vpY, uint32_t vpW, uint32_t vpH);

private:
    static constexpr uint32_t MAX_OBJECTS    = 128;
    static constexpr uint32_t CB_SLOT_SIZE   = 256;
    static constexpr uint32_t CAMERA_CB_SIZE = 256;
    static constexpr uint32_t LIGHTS_CB_SIZE = 1280;
    static constexpr uint32_t SHADOW_CB_SIZE = 512;
    static constexpr uint32_t MAX_LIGHTS     = 16;

    // PSO[0] = CullBack, PSO[1] = CullNone (DoubleSided)
    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_pso[2];

    ComPtr<ID3D12Resource> m_objCB[NUM_FRAMES_IN_FLIGHT];
    uint8_t*               m_objMapped[NUM_FRAMES_IN_FLIGHT]    = {};

    ComPtr<ID3D12Resource> m_cameraCB[NUM_FRAMES_IN_FLIGHT];
    uint8_t*               m_cameraMapped[NUM_FRAMES_IN_FLIGHT] = {};

    ComPtr<ID3D12Resource> m_lightsCB[NUM_FRAMES_IN_FLIGHT];
    uint8_t*               m_lightsMapped[NUM_FRAMES_IN_FLIGHT] = {};

    ComPtr<ID3D12Resource> m_shadowCB[NUM_FRAMES_IN_FLIGHT];
    uint8_t*               m_shadowMapped[NUM_FRAMES_IN_FLIGHT] = {};

    bool CreateRootSignature(ID3D12Device* device);
    bool CreatePipelineStates(ID3D12Device* device);
    bool CreateConstantBuffers(ID3D12Device* device);
};

} // namespace Fujin
