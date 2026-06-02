#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <vector>
#include <string>

namespace Fujin {

using Microsoft::WRL::ComPtr;

struct MeshVertex {
    float px, py, pz;
    float nx, ny, nz;
    float tx, ty, tz;
    float u, v;
};

struct SubMesh {
    uint32_t    IndexCount    = 0;
    uint32_t    StartIndex    = 0;
    int32_t     BaseVertex    = 0;
    std::string DiffusePath;
    std::string NormalPath;
    std::string OrmPath;
    float       BaseColor[3]  = { 1.0f, 1.0f, 1.0f };
};

struct MeshAsset {
    ComPtr<ID3D12Resource>   VertexBuffer;
    ComPtr<ID3D12Resource>   IndexBuffer;
    D3D12_VERTEX_BUFFER_VIEW VBView = {};
    D3D12_INDEX_BUFFER_VIEW  IBView = {};
    std::vector<SubMesh>     SubMeshes;

    // Local-space bounds (min/max of vertex positions) for frustum culling.
    float BoundsMin[3] = { 0.0f, 0.0f, 0.0f };
    float BoundsMax[3] = { 0.0f, 0.0f, 0.0f };
};

} // namespace Fujin
