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
                 uint32_t scissorW = 0, uint32_t scissorH = 0,
                 float    nearZ = 0.1f, float farZ = 1000.0f,
                 uint32_t spotShadowSRVSlot = 0,
                 uint32_t pointShadowSRVSlot = 0);

    // CPU: select up to MAX_SHADOW_SPOTS shadow-casting spot lights (nearest to the camera), assign
    // stable slots, and compute the per-slot cache dirty flag (NeedsRender) from a signature of the
    // light transform + in-range casters. `casters` is the shared list from ShadowPass::BuildCasters.
    void PrepareSpotShadows(const SceneManager& scene, const Vector3& cameraPos,
                            const std::vector<ShadowCaster>& casters);
    const SpotShadowData& GetSpotData() const { return m_spotData; }

    // CPU: same for point lights (cube shadows).
    void PreparePointShadows(const SceneManager& scene, const Vector3& cameraPos,
                             const std::vector<ShadowCaster>& casters);
    const PointShadowData& GetPointData() const { return m_pointData; }

    void Shutdown();

private:
    static constexpr uint32_t MAX_LIGHTS     = 1024;   // upper bound for the light StructuredBuffer
    static constexpr uint32_t CAMERA_CB_SIZE = 256;
    static constexpr uint32_t SHADOW_CB_SIZE = 512; // 4*64B LightViewProj + 16B splits
    static constexpr uint32_t SPOT_CB_SIZE   = 512; // MAX*64B SpotVP + 16B SpotCount

    // Clustered lighting: a tile grid (viewport XY) × NDC-z slices. Light culling is done on the
    // CPU each frame into m_clusterSB; the pixel shader looks up its cluster's light list.
    static constexpr uint32_t CLUSTER_X        = 16;
    static constexpr uint32_t CLUSTER_Y        = 9;
    static constexpr uint32_t CLUSTER_Z        = 24;
    static constexpr uint32_t MAX_PER_CLUSTER  = 64;
    static constexpr uint32_t NUM_CLUSTERS     = CLUSTER_X * CLUSTER_Y * CLUSTER_Z;
    static constexpr uint32_t CLUSTER_STRIDE   = 1 + MAX_PER_CLUSTER;            // uints per cluster
    static constexpr uint32_t CLUSTER_ELEMS    = NUM_CLUSTERS * CLUSTER_STRIDE;  // total uints

    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_pso;
    ComPtr<ID3D12Resource>      m_cameraCB[NUM_FRAMES_IN_FLIGHT];
    ComPtr<ID3D12Resource>      m_lightsSB[NUM_FRAMES_IN_FLIGHT];   // StructuredBuffer<LightData>
    ComPtr<ID3D12Resource>      m_clusterSB[NUM_FRAMES_IN_FLIGHT];  // StructuredBuffer<uint> light lists
    ComPtr<ID3D12Resource>      m_shadowCB[NUM_FRAMES_IN_FLIGHT];
    ComPtr<ID3D12Resource>      m_spotShadowCB[NUM_FRAMES_IN_FLIGHT];  // b3: SpotVP[MAX] + count
    uint8_t*                    m_cameraMapped[NUM_FRAMES_IN_FLIGHT] = {};
    uint8_t*                    m_lightsMapped[NUM_FRAMES_IN_FLIGHT] = {};
    uint8_t*                    m_clusterMapped[NUM_FRAMES_IN_FLIGHT] = {};
    uint8_t*                    m_shadowMapped[NUM_FRAMES_IN_FLIGHT] = {};
    uint8_t*                    m_spotShadowMapped[NUM_FRAMES_IN_FLIGHT] = {};
    uint32_t                    m_lightSRVSlot[NUM_FRAMES_IN_FLIGHT] = {};

    // Per-frame shadow selection (built by Prepare*Shadows, consumed by Execute).
    SpotShadowData              m_spotData;
    uint64_t                    m_spotActorId[SpotShadowData::MAX] = {};
    uint64_t                    m_spotSig[SpotShadowData::MAX] = {};      // cache signature per slot
    PointShadowData             m_pointData;
    uint64_t                    m_pointActorId[PointShadowData::MAX] = {};
    uint64_t                    m_pointSig[PointShadowData::MAX] = {};    // cache signature per slot

    bool CreateRootSignature(ID3D12Device* device);
    bool CreatePipelineState(ID3D12Device* device);
    bool CreateConstantBuffers(GraphicsDevice& gfx);

    // CPU light culling: fill m_clusterMapped[fi] from the gathered lights + camera.
    void CullLightsToClusters(uint32_t fi, const Matrix4x4& invViewProj,
                              float nearZ, float farZ,
                              const struct LightCullItem* lights, uint32_t lightCount);
};

} // namespace Fujin
