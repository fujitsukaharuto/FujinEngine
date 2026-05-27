#pragma once
#include "Engine/Graphics/GraphicsDevice.h"
#include "Engine/Math/Math.h"
#include "Engine/Renderer/Effect/ParticleTypes.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>
#include <unordered_map>

namespace Fujin {

class SceneManager;

class ParticlePass {
public:
    bool Initialize(GraphicsDevice& gfx);
    void Execute(ID3D12GraphicsCommandList* cmd,
                 GraphicsDevice& gfx,
                 const SceneManager& scene,
                 const Matrix4x4& viewProj,
                 const Matrix4x4& view,
                 uint32_t vpX, uint32_t vpY, uint32_t vpW, uint32_t vpH,
                 uint32_t frameIndex,
                 float    elapsed,
                 float    dt);
    void Shutdown();

private:
    static constexpr uint32_t MAX_SPRITES   = 8192;
    static constexpr uint32_t MAX_BEAM_VERTS = 4096;
    static constexpr uint32_t PASS_CB_SIZE  = 256;

    // Sprite instance data (matches HLSL input)
    struct InstanceVert {
        float pos[3];
        float size;
        float rot;
        float pad[3];
        float color[4];
    }; // 48 bytes

    // Beam/Ribbon vertex
    struct BeamVert {
        float pos[3];
        float u, v;
        float color[4];
    }; // 36 bytes

    struct PassCB {
        float viewProj[16];
        float camRight[3]; float _p0;
        float camUp[3];    float _p1;
    }; // 96 bytes → padded to 256

    // Sprite pipeline
    ComPtr<ID3D12RootSignature> m_spriteRS;
    ComPtr<ID3D12PipelineState> m_spritePSO;
    ComPtr<ID3D12PipelineState> m_spriteAddPSO;   // additive blend

    // Beam/Ribbon pipeline
    ComPtr<ID3D12RootSignature> m_beamRS;
    ComPtr<ID3D12PipelineState> m_beamPSO;
    ComPtr<ID3D12PipelineState> m_beamAddPSO;     // additive blend

    // Geometry
    ComPtr<ID3D12Resource>  m_quadVB;
    ComPtr<ID3D12Resource>  m_quadIB;
    ComPtr<ID3D12Resource>  m_instanceVB[NUM_FRAMES_IN_FLIGHT];
    InstanceVert*           m_instanceMapped[NUM_FRAMES_IN_FLIGHT] = {};
    ComPtr<ID3D12Resource>  m_beamVB_buf[NUM_FRAMES_IN_FLIGHT];
    BeamVert*               m_beamMapped[NUM_FRAMES_IN_FLIGHT]  = {};

    // Constant buffer
    ComPtr<ID3D12Resource>  m_passCB[NUM_FRAMES_IN_FLIGHT];
    uint8_t*                m_passMapped[NUM_FRAMES_IN_FLIGHT] = {};

    bool CreateSpritePipeline(GraphicsDevice& gfx);
    bool CreateBeamPipeline(GraphicsDevice& gfx);
    bool CreateBuffers(GraphicsDevice& gfx);
    bool CreateGPUComputePipelines(GraphicsDevice& gfx);
    bool CreateGPUDrawPipeline(GraphicsDevice& gfx);

    void DrawSprites(ID3D12GraphicsCommandList* cmd, uint32_t frameIdx,
                     const SceneManager& scene);
    uint32_t DrawBeams(ID3D12GraphicsCommandList* cmd, uint32_t frameIdx,
                       const SceneManager& scene,
                       const Vector3& camPos, float elapsed);
    void DrawRibbons(ID3D12GraphicsCommandList* cmd, uint32_t frameIdx,
                     const SceneManager& scene,
                     const Vector3& camPos, uint32_t beamVtxUsed);
    void DrawGPUSprites(ID3D12GraphicsCommandList* cmd, GraphicsDevice& gfx,
                        const SceneManager& scene, uint32_t frameIdx, float dt, float elapsed);

    // GPU particle pipeline
    ComPtr<ID3D12RootSignature> m_gpuSpawnRS;
    ComPtr<ID3D12PipelineState> m_gpuSpawnPSO;
    ComPtr<ID3D12RootSignature> m_gpuUpdateRS;
    ComPtr<ID3D12PipelineState> m_gpuUpdatePSO;
    ComPtr<ID3D12RootSignature> m_gpuDrawRS;
    ComPtr<ID3D12PipelineState> m_gpuDrawPSO;
    ComPtr<ID3D12PipelineState> m_gpuDrawAddPSO;  // additive blend

    struct GPUEmitterState {
        ComPtr<ID3D12Resource> particleBuf;                         // DEFAULT UAV+SRV
        ComPtr<ID3D12Resource> writeHead;                           // DEFAULT UAV
        ComPtr<ID3D12Resource> computeCB[NUM_FRAMES_IN_FLIGHT];    // UPLOAD 512B
        ComPtr<ID3D12Resource> spawnUpload[NUM_FRAMES_IN_FLIGHT];  // UPLOAD N*80B
        uint8_t* computeCBMapped[NUM_FRAMES_IN_FLIGHT] = {};
        GPUSpawnData* spawnMapped[NUM_FRAMES_IN_FLIGHT] = {};
        uint32_t maxParticles    = 0;
        float    accumTime       = 0.0f;
        float    elapsed         = 0.0f;
        uint32_t lastResetCount  = 0;
        InitModule prevInit      = {};
        D3D12_RESOURCE_STATES particleState = D3D12_RESOURCE_STATE_COMMON;
    };
    std::unordered_map<uint64_t, GPUEmitterState> m_gpuEmitters;

    bool InitGPUEmitter(ID3D12Device* dev, uint64_t key, uint32_t maxParticles);
};

} // namespace Fujin
