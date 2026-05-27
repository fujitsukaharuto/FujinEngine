#pragma once
#include "Engine/Graphics/GraphicsDevice.h"
#include "Engine/Asset/TextureManager.h"
#include "Engine/Math/Math.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>


namespace Fujin {

class SceneManager;
class GeometryManager;
class MaterialManager;

struct ShadowData {
    Matrix4x4 LightViewProj[4];
    float     CascadeSplits[4];
};

class ShadowPass {
public:
    static constexpr uint32_t CASCADE_COUNT   = 4;
    static constexpr uint32_t SHADOW_MAP_SIZE = 2048;

    bool Initialize(GraphicsDevice& gfx);

    // Full execute: computes cascades + GPU draw + internal barrier transitions.
    // Kept for use outside the RenderGraph path.
    void Execute(ID3D12GraphicsCommandList* cmd,
                 GraphicsDevice& gfx,
                 const SceneManager& scene,
                 uint32_t frameIndex,
                 GeometryManager& geoMgr,
                 TextureManager& texMgr,
                 MaterialManager& matMgr,
                 const Matrix4x4& invViewProj,
                 float nearZ, float farZ,
                 const Vector3& lightDir,
                 ShadowData& outData);

    // CPU-only: compute cascade matrices. Call before ExecuteGPU when
    // resource barriers are managed externally (e.g. by RenderGraph).
    void ComputeCascades(const Matrix4x4& invViewProj,
                         float nearZ, float farZ,
                         const Vector3& lightDir,
                         ShadowData& outData);

    // GPU-only: issue shadow draw calls using pre-computed ShadowData.
    // No barrier insertion — assumes shadow map is already in DEPTH_WRITE.
    void ExecuteGPU(ID3D12GraphicsCommandList* cmd,
                    const SceneManager& scene,
                    uint32_t frameIndex,
                    GeometryManager& geoMgr,
                    TextureManager& texMgr,
                    MaterialManager& matMgr,
                    GraphicsDevice& gfx,
                    const ShadowData& data);

    void Shutdown();

    uint32_t        GetSRVSlot()    const { return m_srvSlot; }
    ID3D12Resource* GetShadowMap()  const { return m_shadowMap.Get(); }

private:
    static constexpr uint32_t MAX_OBJECTS          = 256;
    static constexpr uint32_t CB_SLOT_SIZE         = 256;
    static constexpr uint32_t MAX_SKINNED_OBJECTS  = 32;
    static constexpr uint32_t BONE_PALETTE_STRIDE  = 128 * 64; // MAX_BONES * sizeof(Matrix4x4)

    // --- Static shadow ---
    // Root sig variants: m_rootSignature (no tex), m_rootSignatureAlphaClip (with t0)
    ComPtr<ID3D12RootSignature>  m_rootSignature;
    ComPtr<ID3D12RootSignature>  m_rootSignatureAlphaClip;
    // PSO variants: index = (doubleSided<<1)|alphaClip
    // [0]=CullBack/NoAlphaClip  [1]=CullBack/AlphaClip
    // [2]=CullNone/NoAlphaClip  [3]=CullNone/AlphaClip
    ComPtr<ID3D12PipelineState>  m_psoVariants[4];
    ComPtr<ID3D12Resource>       m_cbuffer[NUM_FRAMES_IN_FLIGHT];
    uint8_t*                     m_cbMapped[NUM_FRAMES_IN_FLIGHT] = {};

    // --- Skinned shadow ---
    ComPtr<ID3D12RootSignature>  m_skinnedRootSig;
    ComPtr<ID3D12PipelineState>  m_skinnedPso;
    ComPtr<ID3D12Resource>       m_skinnedCB[NUM_FRAMES_IN_FLIGHT];      // CASCADE_COUNT × MAX_SKINNED_OBJECTS slots
    uint8_t*                     m_skinnedCBMapped[NUM_FRAMES_IN_FLIGHT] = {};
    ComPtr<ID3D12Resource>       m_bonePaletteCB[NUM_FRAMES_IN_FLIGHT];
    uint8_t*                     m_bonePaletteMapped[NUM_FRAMES_IN_FLIGHT] = {};

    ComPtr<ID3D12Resource>       m_shadowMap;
    ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE  m_dsvHandles[CASCADE_COUNT] = {};
    uint32_t                     m_srvSlot  = 0;
    D3D12_RESOURCE_STATES        m_mapState = D3D12_RESOURCE_STATE_DEPTH_WRITE;

    bool CreateRootSignature(ID3D12Device* device);
    bool CreateAlphaClipRootSignature(ID3D12Device* device);
    bool CreatePipelineState(ID3D12Device* device);
    bool CreateConstantBuffers(ID3D12Device* device);
    bool CreateShadowMap(GraphicsDevice& gfx);
    bool CreateSkinnedRootSignature(ID3D12Device* device);
    bool CreateSkinnedPipelineState(ID3D12Device* device);
    bool CreateSkinnedShadowBuffers(ID3D12Device* device);
};

} // namespace Fujin
