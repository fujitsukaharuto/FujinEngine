#pragma once
#include "Engine/Graphics/GraphicsDevice.h"
#include "Engine/Asset/TextureManager.h"
#include "Engine/Math/Math.h"
#include "Engine/Spatial/Bvh.h"   // Aabb
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>
#include <vector>


namespace Fujin {

class SceneManager;
class GeometryManager;
class MaterialManager;
struct MeshAsset;
struct SkeletalMeshAsset;

// A resolved shadow caster, gathered once per frame by SceneRenderer and shared by every shadow
// pass (cascade/spot/point) for both rendering and the cache dirty-hash. Avoids re-resolving
// components/assets per light per face.
struct ShadowCaster {
    const MeshAsset*         mesh  = nullptr;   // non-skinned
    const SkeletalMeshAsset* smesh = nullptr;   // skinned
    Matrix4x4 world;
    Aabb      box;                              // world AABB (non-skinned only)
    Vector3   center;                           // box center (non-skinned)
    float     radius = 0.0f;                    // bounding radius (non-skinned)
    int       kind   = 0;                       // 0=static, 1=alphaClip, 2=skinned
    bool      doubleSided = false;
    uint32_t  albedoSlot  = 0;                  // alphaClip
    uint32_t  skinSlot    = 0;                  // skinned (palette slot)
    const Matrix4x4* palette = nullptr;         // skinned bone palette (valid for the frame)
    uint64_t  actorId     = 0;
    bool      skeletal    = false;
};

struct ShadowData {
    Matrix4x4 LightViewProj[4];
    float     CascadeSplits[4];
};

// Spot light shadows (Stage A). One perspective depth map per shadow-casting spot, kept in a
// single Texture2DArray atlas. Built on the CPU each frame by LightingPass (it owns light
// gathering); ViewProj[i] corresponds to atlas slice i. Count==0 means no spot shadow work.
struct SpotShadowData {
    static constexpr uint32_t MAX = 4;   // budget = ShadowPass::MAX_SHADOW_SPOTS
    Matrix4x4 ViewProj[MAX];
    bool      NeedsRender[MAX] = {};     // per-slot cache dirty flag (Stage C)
    uint32_t  Count = 0;                 // number of occupied slots
};

// Point light shadows (Stage B). Each shadow-casting point light gets a depth cube (6 faces) in a
// TextureCubeArray. The lighting PS reconstructs the per-face NDC depth from the major axis of the
// light→fragment vector and PCFs the cube with the comparison sampler. Count==0 means no work.
struct PointShadowData {
    static constexpr uint32_t MAX = 4;   // budget = ShadowPass::MAX_SHADOW_POINTS
    Vector3   Pos[MAX];                  // world position (cube center)
    float     Range[MAX];                // = far plane of the face projection
    bool      NeedsRender[MAX] = {};     // per-slot cache dirty flag (Stage C)
    uint32_t  Count = 0;                 // number of occupied slots
};

class ShadowPass {
public:
    static constexpr uint32_t CASCADE_COUNT    = 4;
    static constexpr uint32_t SHADOW_MAP_SIZE  = 2048;
    static constexpr uint32_t MAX_SHADOW_SPOTS = SpotShadowData::MAX;   // spot shadow budget
    static constexpr uint32_t SPOT_MAP_SIZE    = 1024;
    static constexpr uint32_t MAX_SHADOW_POINTS = PointShadowData::MAX;  // point shadow budget
    static constexpr uint32_t POINT_MAP_SIZE    = 512;                   // per cube face

    bool Initialize(GraphicsDevice& gfx);

    // Gather all shadow casters once per frame (honours MeshComponent::CastShadow). Shared by the
    // spot/point passes and the cache dirty-hash. Skinned slots match the bone-palette upload order.
    static void BuildCasters(const SceneManager& scene,
                             GeometryManager& geoMgr,
                             TextureManager& texMgr,
                             MaterialManager& matMgr,
                             std::vector<ShadowCaster>& out);

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

    // --- Spot light shadows (Stage A) ---
    // Build a spot light's perspective view-projection. spotAngleDeg is the full cone aperture.
    static Matrix4x4 ComputeSpotMatrix(const Vector3& pos, const Vector3& dir,
                                       float spotAngleDeg, float range);

    // GPU-only: render the depth of all (frustum-culled) casters into each active spot slice.
    // No barrier insertion — assumes the spot atlas is already in DEPTH_WRITE.
    void ExecuteSpotGPU(ID3D12GraphicsCommandList* cmd,
                        uint32_t frameIndex,
                        const std::vector<ShadowCaster>& casters,
                        GraphicsDevice& gfx,
                        const SpotShadowData& data);

    // --- Point light shadows (Stage B) ---
    // GPU-only: render 6 cube faces of depth per active point light. No barrier insertion.
    void ExecutePointGPU(ID3D12GraphicsCommandList* cmd,
                         uint32_t frameIndex,
                         const std::vector<ShadowCaster>& casters,
                         GraphicsDevice& gfx,
                         const PointShadowData& data);

    void Shutdown();

    uint32_t        GetSRVSlot()    const { return m_srvSlot; }
    ID3D12Resource* GetShadowMap()  const { return m_shadowMap.Get(); }

    uint32_t        GetSpotSRVSlot() const { return m_spotSrvSlot; }
    ID3D12Resource* GetSpotMap()     const { return m_spotMap.Get(); }

    uint32_t        GetPointSRVSlot() const { return m_pointSrvSlot; }
    ID3D12Resource* GetPointMap()     const { return m_pointMap.Get(); }

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

    // --- Spot shadow atlas (Texture2DArray, MAX_SHADOW_SPOTS slices) ---
    ComPtr<ID3D12Resource>       m_spotMap;
    ComPtr<ID3D12DescriptorHeap> m_spotDsvHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE  m_spotDsvHandles[MAX_SHADOW_SPOTS] = {};
    uint32_t                     m_spotSrvSlot = 0;
    // Per-object LightWVP CBs for the spot pass (static+alphaclip share one; skinned its own).
    ComPtr<ID3D12Resource>       m_spotCB[NUM_FRAMES_IN_FLIGHT];          // MAX_SHADOW_SPOTS × MAX_OBJECTS slots
    uint8_t*                     m_spotCBMapped[NUM_FRAMES_IN_FLIGHT] = {};
    ComPtr<ID3D12Resource>       m_spotSkinnedCB[NUM_FRAMES_IN_FLIGHT];   // MAX_SHADOW_SPOTS × MAX_SKINNED_OBJECTS slots
    uint8_t*                     m_spotSkinnedMapped[NUM_FRAMES_IN_FLIGHT] = {};

    // --- Point shadow cube atlas (TextureCubeArray, 6 × MAX_SHADOW_POINTS slices) ---
    static constexpr uint32_t POINT_FACES = 6 * MAX_SHADOW_POINTS;
    ComPtr<ID3D12Resource>       m_pointMap;          // created in PIXEL_SHADER_RESOURCE
    ComPtr<ID3D12DescriptorHeap> m_pointDsvHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE  m_pointDsvHandles[POINT_FACES] = {};
    uint32_t                     m_pointSrvSlot = 0;
    ComPtr<ID3D12Resource>       m_pointCB[NUM_FRAMES_IN_FLIGHT];          // POINT_FACES × MAX_OBJECTS slots
    uint8_t*                     m_pointCBMapped[NUM_FRAMES_IN_FLIGHT] = {};
    ComPtr<ID3D12Resource>       m_pointSkinnedCB[NUM_FRAMES_IN_FLIGHT];   // POINT_FACES × MAX_SKINNED_OBJECTS slots
    uint8_t*                     m_pointSkinnedMapped[NUM_FRAMES_IN_FLIGHT] = {};

    bool CreateRootSignature(ID3D12Device* device);
    bool CreateAlphaClipRootSignature(ID3D12Device* device);
    bool CreatePipelineState(ID3D12Device* device);
    bool CreateConstantBuffers(ID3D12Device* device);
    bool CreateShadowMap(GraphicsDevice& gfx);
    bool CreateSkinnedRootSignature(ID3D12Device* device);
    bool CreateSkinnedPipelineState(ID3D12Device* device);
    bool CreateSkinnedShadowBuffers(ID3D12Device* device);

    bool CreateSpotShadowMap(GraphicsDevice& gfx);
    bool CreateSpotConstantBuffers(ID3D12Device* device);

    bool CreatePointShadowMap(GraphicsDevice& gfx);
    bool CreatePointConstantBuffers(ID3D12Device* device);
};

} // namespace Fujin
