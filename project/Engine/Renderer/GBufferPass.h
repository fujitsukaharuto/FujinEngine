#pragma once
#include "GBuffer.h"
#include "Engine/Graphics/GraphicsDevice.h"
#include "Engine/Asset/GeometryManager.h"
#include "Engine/Asset/TextureManager.h"
#include "Engine/Animation/AnimationTypes.h"
#include "Engine/Math/Math.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>
#include <unordered_set>
#include <unordered_map>

namespace Fujin {

class SceneManager;
class MaterialManager;

class GBufferPass {
public:
    Vector3 CameraPos    = Vector3(0.0f, 3.0f, -5.0f);
    Vector3 CameraTarget = Vector3(0.0f, 0.0f,  5.0f);

    bool Initialize(GraphicsDevice& gfx, const GBuffer& gbuffer);
    void Execute(ID3D12GraphicsCommandList* cmd,
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
                 const std::unordered_set<uint64_t>* visibleActors = nullptr);
    void Shutdown();

private:
    static constexpr uint32_t MAX_OBJECTS         = 256;
    static constexpr uint32_t CB_SLOT_SIZE        = 256;
    static constexpr uint32_t MAX_SKINNED_ACTORS  = 256;  // one transform+palette slot per skinned actor
    static constexpr uint32_t BONE_PALETTE_STRIDE = MAX_BONES * 64; // 8192 bytes

    // --- Static mesh ---
    ComPtr<ID3D12RootSignature> m_rootSignature;
    // PSO variants [doubleSided][alphaClip]: index = (doubleSided<<1)|alphaClip
    ComPtr<ID3D12PipelineState> m_psoVariants[4];
    ComPtr<ID3D12Resource>      m_cbuffer[NUM_FRAMES_IN_FLIGHT];
    uint8_t*                    m_cbMapped[NUM_FRAMES_IN_FLIGHT] = {};

    // --- Skinned mesh ---
    ComPtr<ID3D12RootSignature> m_skinnedRootSig;
    ComPtr<ID3D12PipelineState> m_skinnedPso;
    ComPtr<ID3D12Resource>      m_skinnedCB[NUM_FRAMES_IN_FLIGHT];
    uint8_t*                    m_skinnedCBMapped[NUM_FRAMES_IN_FLIGHT] = {};
    ComPtr<ID3D12Resource>      m_bonePaletteCB[NUM_FRAMES_IN_FLIGHT];
    uint8_t*                    m_bonePaletteMapped[NUM_FRAMES_IN_FLIGHT] = {};
    ComPtr<ID3D12Resource>      m_prevBonePaletteCB[NUM_FRAMES_IN_FLIGHT];     // last frame's palette (motion vectors)
    uint8_t*                    m_prevBonePaletteMapped[NUM_FRAMES_IN_FLIGHT] = {};

    bool CreateRootSignature(ID3D12Device* device);
    bool CreatePipelineState(ID3D12Device* device, const GBuffer& gbuffer);
    bool CreatePSOVariant(ID3D12Device* device, const GBuffer& gbuffer,
                          D3D12_CULL_MODE cull, bool alphaClip,
                          ComPtr<ID3D12PipelineState>& outPso);
    bool CreateConstantBuffers(ID3D12Device* device);
    bool CreateSkinnedRootSignature(ID3D12Device* device);
    bool CreateSkinnedPipelineState(ID3D12Device* device, const GBuffer& gbuffer);
    bool CreateSkinnedBuffers(ID3D12Device* device);

    // Per-actor world matrix from the previous frame (for motion vectors).
    std::unordered_map<uint64_t, Matrix4x4> m_prevWorld;
};

} // namespace Fujin
