#pragma once
#include "MeshAsset.h"
#include "SkeletalMeshAsset.h"
#include <d3d12.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <functional>
#include <vector>

namespace Fujin {

class GeometryManager {
public:
    bool Initialize(ID3D12Device* device, ID3D12CommandQueue* queue);
    void Shutdown();

    // Static mesh (any format without skeletal bones).
    const MeshAsset* LoadMesh(const std::string& path);

    // Skeletal mesh (any format that contains bone data).
    const SkeletalMeshAsset* LoadSkeletalMesh(const std::string& path);

    // Returns true if the mesh at path has bones (i.e. should use the skeletal pipeline).
    // Triggers a classification load on first call; subsequent calls are O(1) cache hits.
    bool IsSkeletal(const std::string& path);

private:
    ID3D12Device*       m_device = nullptr;
    ID3D12CommandQueue* m_queue  = nullptr;

    std::unordered_map<std::string, std::unique_ptr<MeshAsset>>         m_cache;
    std::unordered_map<std::string, std::unique_ptr<SkeletalMeshAsset>> m_skeletalCache;
    std::unordered_set<std::string>                                      m_failedPaths;
    std::unordered_set<std::string>                                      m_skeletalPaths;
    std::unique_ptr<MeshAsset>                                           m_builtinCube;

    const MeshAsset* EnsureBuiltinCube();
    std::unique_ptr<MeshAsset> CreateProceduralCube();

    void UploadGeometry(const std::vector<MeshVertex>&        verts,
                        const std::vector<uint32_t>&           indices,
                        MeshAsset&                             outAsset);
    void UploadSkeletalGeometry(const std::vector<SkinnedMeshVertex>& verts,
                                const std::vector<uint32_t>&          indices,
                                SkeletalMeshAsset&                    outAsset);
    void UploadOneShot(std::function<void(ID3D12GraphicsCommandList*)> fn);
};

} // namespace Fujin
