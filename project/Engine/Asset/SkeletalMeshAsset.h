#pragma once
#include "MeshAsset.h"
#include "Engine/Animation/AnimationTypes.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <vector>

namespace Fujin {

using Microsoft::WRL::ComPtr;

// Vertex layout for GPU-skinned meshes (76 bytes).
//   Offset  0: Position  (float3)
//   Offset 12: Normal    (float3)
//   Offset 24: Tangent   (float3)
//   Offset 36: UV        (float2)
//   Offset 44: BoneIdx   (uint32 × 4)
//   Offset 60: BoneWgt   (float  × 4)
struct SkinnedMeshVertex {
    float    px, py, pz;
    float    nx, ny, nz;
    float    tx, ty, tz;
    float    u,  v;
    uint32_t boneIdx[4];
    float    boneWgt[4];
};

// GPU resources + skeleton + animation clips.
struct SkeletalMeshAsset {
    ComPtr<ID3D12Resource>    VertexBuffer;
    ComPtr<ID3D12Resource>    IndexBuffer;
    D3D12_VERTEX_BUFFER_VIEW  VBView = {};
    D3D12_INDEX_BUFFER_VIEW   IBView = {};
    std::vector<SubMesh>      SubMeshes;

    Skeleton                  Skel;
    std::vector<AnimationClip> Clips;
};

} // namespace Fujin
