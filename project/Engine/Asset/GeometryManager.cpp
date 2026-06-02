#include "GeometryManager.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <functional>
#include <stdexcept>
#include <algorithm>

namespace Fujin {

bool GeometryManager::Initialize(ID3D12Device* device, ID3D12CommandQueue* queue) {
    m_device = device;
    m_queue  = queue;
    return true;
}

void GeometryManager::Shutdown() {
    m_cache.clear();
    m_skeletalCache.clear();
    m_failedPaths.clear();
    m_builtinCube.reset();
}

void GeometryManager::UploadOneShot(std::function<void(ID3D12GraphicsCommandList*)> fn) {
    ComPtr<ID3D12CommandAllocator> alloc;
    m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc));
    ComPtr<ID3D12GraphicsCommandList> cmd;
    m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&cmd));
    fn(cmd.Get());
    cmd->Close();
    ID3D12CommandList* lists[] = { cmd.Get() };
    m_queue->ExecuteCommandLists(1, lists);
    ComPtr<ID3D12Fence> fence;
    m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    HANDLE ev = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    m_queue->Signal(fence.Get(), 1);
    fence->SetEventOnCompletion(1, ev);
    WaitForSingleObject(ev, INFINITE);
    CloseHandle(ev);
}

static ComPtr<ID3D12Resource> CreateDefaultBuffer(ID3D12Device* device, UINT64 size,
                                                   D3D12_RESOURCE_STATES initialState,
                                                   D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE) {
    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width              = size;
    rd.Height             = 1;
    rd.DepthOrArraySize   = 1;
    rd.MipLevels          = 1;
    rd.Format             = DXGI_FORMAT_UNKNOWN;
    rd.SampleDesc.Count   = 1;
    rd.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    rd.Flags              = flags;
    ComPtr<ID3D12Resource> res;
    device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, initialState, nullptr, IID_PPV_ARGS(&res));
    return res;
}

static ComPtr<ID3D12Resource> CreateUploadBuffer(ID3D12Device* device, UINT64 size) {
    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width              = size;
    rd.Height             = 1;
    rd.DepthOrArraySize   = 1;
    rd.MipLevels          = 1;
    rd.Format             = DXGI_FORMAT_UNKNOWN;
    rd.SampleDesc.Count   = 1;
    rd.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> res;
    device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&res));
    return res;
}

void GeometryManager::UploadGeometry(const std::vector<MeshVertex>& verts,
                                      const std::vector<uint32_t>&   indices,
                                      MeshAsset&                     asset) {
    // Local-space bounds for frustum culling (min/max of vertex positions).
    {
        float mn[3] = {  3.4e38f,  3.4e38f,  3.4e38f };
        float mx[3] = { -3.4e38f, -3.4e38f, -3.4e38f };
        for (const auto& v : verts) {
            if (v.px < mn[0]) mn[0] = v.px;  if (v.px > mx[0]) mx[0] = v.px;
            if (v.py < mn[1]) mn[1] = v.py;  if (v.py > mx[1]) mx[1] = v.py;
            if (v.pz < mn[2]) mn[2] = v.pz;  if (v.pz > mx[2]) mx[2] = v.pz;
        }
        if (verts.empty()) { mn[0]=mn[1]=mn[2]=0.f; mx[0]=mx[1]=mx[2]=0.f; }
        for (int i = 0; i < 3; ++i) { asset.BoundsMin[i] = mn[i]; asset.BoundsMax[i] = mx[i]; }
    }

    UINT64 vbSize = static_cast<UINT64>(verts.size())   * sizeof(MeshVertex);
    UINT64 ibSize = static_cast<UINT64>(indices.size()) * sizeof(uint32_t);

    auto vbDefault = CreateDefaultBuffer(m_device, vbSize, D3D12_RESOURCE_STATE_COMMON);
    auto ibDefault = CreateDefaultBuffer(m_device, ibSize, D3D12_RESOURCE_STATE_COMMON);
    auto vbUpload  = CreateUploadBuffer(m_device, vbSize);
    auto ibUpload  = CreateUploadBuffer(m_device, ibSize);

    void* mapped = nullptr;
    D3D12_RANGE readRange = { 0, 0 };
    vbUpload->Map(0, &readRange, &mapped);
    memcpy(mapped, verts.data(), vbSize);
    vbUpload->Unmap(0, nullptr);

    ibUpload->Map(0, &readRange, &mapped);
    memcpy(mapped, indices.data(), ibSize);
    ibUpload->Unmap(0, nullptr);

    ID3D12Resource* vbDef = vbDefault.Get();
    ID3D12Resource* ibDef = ibDefault.Get();
    ID3D12Resource* vbUp  = vbUpload.Get();
    ID3D12Resource* ibUp  = ibUpload.Get();

    UploadOneShot([=](ID3D12GraphicsCommandList* cmd) {
        cmd->CopyBufferRegion(vbDef, 0, vbUp, 0, vbSize);
        cmd->CopyBufferRegion(ibDef, 0, ibUp, 0, ibSize);

        D3D12_RESOURCE_BARRIER barriers[2] = {};
        barriers[0].Type                          = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[0].Transition.pResource          = vbDef;
        barriers[0].Transition.StateBefore        = D3D12_RESOURCE_STATE_COPY_DEST;
        barriers[0].Transition.StateAfter         = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        barriers[0].Transition.Subresource        = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barriers[1].Type                          = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[1].Transition.pResource          = ibDef;
        barriers[1].Transition.StateBefore        = D3D12_RESOURCE_STATE_COPY_DEST;
        barriers[1].Transition.StateAfter         = D3D12_RESOURCE_STATE_INDEX_BUFFER;
        barriers[1].Transition.Subresource        = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(2, barriers);
    });

    asset.VertexBuffer = std::move(vbDefault);
    asset.IndexBuffer  = std::move(ibDefault);
    asset.VBView.BufferLocation = asset.VertexBuffer->GetGPUVirtualAddress();
    asset.VBView.SizeInBytes    = static_cast<UINT>(vbSize);
    asset.VBView.StrideInBytes  = sizeof(MeshVertex);
    asset.IBView.BufferLocation = asset.IndexBuffer->GetGPUVirtualAddress();
    asset.IBView.SizeInBytes    = static_cast<UINT>(ibSize);
    asset.IBView.Format         = DXGI_FORMAT_R32_UINT;
}

std::unique_ptr<MeshAsset> GeometryManager::CreateProceduralCube() {
    // 6 faces × 4 vertices = 24 verts, 6 faces × 2 tris × 3 idx = 36 idx
    // Winding: CW in screen space (DX12 FrontCounterClockwise=FALSE).
    // Derived from DirectXTK GeometricPrimitive convention.
    struct FaceDesc {
        float nx, ny, nz;
        float tx, ty, tz;
        float p[4][3];
    };
    static const FaceDesc faces[6] = {
        // +Z
        { 0, 0, 1,  1, 0, 0,  {{ 0.5f, 0.5f, 0.5f},{ 0.5f,-0.5f, 0.5f},{-0.5f,-0.5f, 0.5f},{-0.5f, 0.5f, 0.5f}} },
        // -Z
        { 0, 0,-1, -1, 0, 0,  {{-0.5f, 0.5f,-0.5f},{-0.5f,-0.5f,-0.5f},{ 0.5f,-0.5f,-0.5f},{ 0.5f, 0.5f,-0.5f}} },
        // +X
        { 1, 0, 0,  0, 0,-1,  {{ 0.5f, 0.5f,-0.5f},{ 0.5f,-0.5f,-0.5f},{ 0.5f,-0.5f, 0.5f},{ 0.5f, 0.5f, 0.5f}} },
        // -X
        {-1, 0, 0,  0, 0, 1,  {{-0.5f, 0.5f, 0.5f},{-0.5f,-0.5f, 0.5f},{-0.5f,-0.5f,-0.5f},{-0.5f, 0.5f,-0.5f}} },
        // +Y
        { 0, 1, 0, -1, 0, 0,  {{-0.5f, 0.5f, 0.5f},{-0.5f, 0.5f,-0.5f},{ 0.5f, 0.5f,-0.5f},{ 0.5f, 0.5f, 0.5f}} },
        // -Y
        { 0,-1, 0,  1, 0, 0,  {{ 0.5f,-0.5f, 0.5f},{ 0.5f,-0.5f,-0.5f},{-0.5f,-0.5f,-0.5f},{-0.5f,-0.5f, 0.5f}} },
    };
    static const float uvs[4][2] = { {1,0},{1,1},{0,1},{0,0} };

    std::vector<MeshVertex> verts;
    std::vector<uint32_t>   indices;
    verts.reserve(24);
    indices.reserve(36);

    for (int f = 0; f < 6; ++f) {
        uint32_t base = static_cast<uint32_t>(verts.size());
        const FaceDesc& fd = faces[f];
        for (int v = 0; v < 4; ++v) {
            MeshVertex vert = {};
            vert.px = fd.p[v][0]; vert.py = fd.p[v][1]; vert.pz = fd.p[v][2];
            vert.nx = fd.nx;      vert.ny = fd.ny;      vert.nz = fd.nz;
            vert.tx = fd.tx;      vert.ty = fd.ty;      vert.tz = fd.tz;
            vert.u  = uvs[v][0];  vert.v  = uvs[v][1];
            verts.push_back(vert);
        }
        indices.push_back(base);   indices.push_back(base+2); indices.push_back(base+1);
        indices.push_back(base);   indices.push_back(base+3); indices.push_back(base+2);
    }

    auto asset = std::make_unique<MeshAsset>();
    UploadGeometry(verts, indices, *asset);

    SubMesh sm;
    sm.IndexCount = 36;
    sm.StartIndex = 0;
    sm.BaseVertex = 0;
    sm.DiffusePath = "";
    asset->SubMeshes.push_back(std::move(sm));

    return asset;
}

const MeshAsset* GeometryManager::EnsureBuiltinCube() {
    if (!m_builtinCube)
        m_builtinCube = CreateProceduralCube();
    return m_builtinCube.get();
}

const MeshAsset* GeometryManager::LoadMesh(const std::string& path) {
    // Empty path or previously failed path → procedural cube fallback
    if (path.empty()) return EnsureBuiltinCube();
    if (m_failedPaths.count(path)) return EnsureBuiltinCube();
    // Already classified as skeletal → don't load as static
    if (m_skeletalPaths.count(path)) return EnsureBuiltinCube();

    auto it = m_cache.find(path);
    if (it != m_cache.end()) return it->second.get();

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path,
        aiProcess_Triangulate       |
        aiProcess_FlipUVs           |
        aiProcess_CalcTangentSpace  |
        aiProcess_GenSmoothNormals  |
        aiProcess_JoinIdenticalVertices |
        aiProcess_OptimizeMeshes);

    if (!scene || !scene->HasMeshes()) {
        m_failedPaths.insert(path);
        return EnsureBuiltinCube();
    }

    // Classify as skeletal if any submesh has bone weights.
    // This covers .gltf/.glb characters as well as any other format with bones.
    for (uint32_t mi = 0; mi < scene->mNumMeshes; ++mi) {
        if (scene->mMeshes[mi]->HasBones()) {
            m_skeletalPaths.insert(path);
            return EnsureBuiltinCube();
        }
    }

    auto asset = std::make_unique<MeshAsset>();
    std::vector<MeshVertex> verts;
    std::vector<uint32_t>   indices;

    for (uint32_t mi = 0; mi < scene->mNumMeshes; ++mi) {
        const aiMesh* mesh = scene->mMeshes[mi];
        uint32_t baseVert  = static_cast<uint32_t>(verts.size());
        uint32_t startIdx  = static_cast<uint32_t>(indices.size());

        for (uint32_t vi = 0; vi < mesh->mNumVertices; ++vi) {
            MeshVertex vert = {};
            vert.px = mesh->mVertices[vi].x;
            vert.py = mesh->mVertices[vi].y;
            vert.pz = mesh->mVertices[vi].z;
            if (mesh->HasNormals()) {
                vert.nx = mesh->mNormals[vi].x;
                vert.ny = mesh->mNormals[vi].y;
                vert.nz = mesh->mNormals[vi].z;
            }
            if (mesh->HasTangentsAndBitangents()) {
                vert.tx = mesh->mTangents[vi].x;
                vert.ty = mesh->mTangents[vi].y;
                vert.tz = mesh->mTangents[vi].z;
            }
            if (mesh->HasTextureCoords(0)) {
                vert.u = mesh->mTextureCoords[0][vi].x;
                vert.v = mesh->mTextureCoords[0][vi].y;
            }
            verts.push_back(vert);
        }

        for (uint32_t fi = 0; fi < mesh->mNumFaces; ++fi) {
            const aiFace& face = mesh->mFaces[fi];
            for (uint32_t ii = 0; ii < face.mNumIndices; ++ii)
                indices.push_back(face.mIndices[ii]);
        }

        SubMesh sm;
        sm.IndexCount  = static_cast<uint32_t>(indices.size()) - startIdx;
        sm.StartIndex  = startIdx;
        sm.BaseVertex  = static_cast<int32_t>(baseVert);

        uint32_t matIdx = mesh->mMaterialIndex;
        if (matIdx < scene->mNumMaterials) {
            const aiMaterial* mat = scene->mMaterials[matIdx];
            auto extractTexPath = [&](aiTextureType type) -> std::string {
                aiString tp;
                if (mat->GetTexture(type, 0, &tp) != AI_SUCCESS) return {};
                std::string full = tp.C_Str();
                size_t slash = full.find_last_of("/\\");
                std::string fname = (slash != std::string::npos) ? full.substr(slash + 1) : full;
                return "Resource/Textures/" + fname;
            };
            sm.DiffusePath = extractTexPath(aiTextureType_DIFFUSE);
            sm.NormalPath  = extractTexPath(aiTextureType_NORMALS);
            if (sm.NormalPath.empty())
                sm.NormalPath = extractTexPath(aiTextureType_HEIGHT);
            sm.OrmPath     = extractTexPath(aiTextureType_METALNESS);
            aiColor3D color(1.0f, 1.0f, 1.0f);
            mat->Get(AI_MATKEY_COLOR_DIFFUSE, color);
            sm.BaseColor[0] = color.r;
            sm.BaseColor[1] = color.g;
            sm.BaseColor[2] = color.b;
        }
        asset->SubMeshes.push_back(std::move(sm));
    }

    if (verts.empty() || indices.empty()) {
        m_failedPaths.insert(path);
        return EnsureBuiltinCube();
    }

    UploadGeometry(verts, indices, *asset);

    const MeshAsset* ptr = asset.get();
    m_cache[path] = std::move(asset);
    return ptr;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static Matrix4x4 ConvertAiMatrix(const aiMatrix4x4& m) {
    Matrix4x4 r;
    r.m[0][0] = m.a1; r.m[0][1] = m.a2; r.m[0][2] = m.a3; r.m[0][3] = m.a4;
    r.m[1][0] = m.b1; r.m[1][1] = m.b2; r.m[1][2] = m.b3; r.m[1][3] = m.b4;
    r.m[2][0] = m.c1; r.m[2][1] = m.c2; r.m[2][2] = m.c3; r.m[2][3] = m.c4;
    r.m[3][0] = m.d1; r.m[3][1] = m.d2; r.m[3][2] = m.d3; r.m[3][3] = m.d4;
    return r;
}

// Recursively add every aiNode to the skeleton (DFS, pre-order).
static void CollectJoints(const aiNode* node, int32_t parentIdx, Skeleton& skel) {
    uint32_t idx = static_cast<uint32_t>(skel.Joints.size());
    Joint j;
    j.Name           = node->mName.C_Str();
    j.ParentIndex    = parentIdx;
    j.BindPoseLocal  = ConvertAiMatrix(node->mTransformation);
    skel.JointMap[j.Name] = idx;
    skel.Joints.push_back(j);
    for (uint32_t i = 0; i < node->mNumChildren; ++i)
        CollectJoints(node->mChildren[i], static_cast<int32_t>(idx), skel);
}

// ---------------------------------------------------------------------------
// UploadSkeletalGeometry
// ---------------------------------------------------------------------------

void GeometryManager::UploadSkeletalGeometry(const std::vector<SkinnedMeshVertex>& verts,
                                              const std::vector<uint32_t>&          indices,
                                              SkeletalMeshAsset&                    asset) {
    UINT64 vbSize = static_cast<UINT64>(verts.size())   * sizeof(SkinnedMeshVertex);
    UINT64 ibSize = static_cast<UINT64>(indices.size()) * sizeof(uint32_t);

    auto vbDefault = CreateDefaultBuffer(m_device, vbSize, D3D12_RESOURCE_STATE_COMMON);
    auto ibDefault = CreateDefaultBuffer(m_device, ibSize, D3D12_RESOURCE_STATE_COMMON);
    auto vbUpload  = CreateUploadBuffer(m_device, vbSize);
    auto ibUpload  = CreateUploadBuffer(m_device, ibSize);

    void* mapped = nullptr;
    D3D12_RANGE rng = { 0, 0 };
    vbUpload->Map(0, &rng, &mapped); memcpy(mapped, verts.data(), vbSize);   vbUpload->Unmap(0, nullptr);
    ibUpload->Map(0, &rng, &mapped); memcpy(mapped, indices.data(), ibSize); ibUpload->Unmap(0, nullptr);

    ID3D12Resource* vbDef = vbDefault.Get(); ID3D12Resource* ibDef = ibDefault.Get();
    ID3D12Resource* vbUp  = vbUpload.Get();  ID3D12Resource* ibUp  = ibUpload.Get();
    UploadOneShot([=](ID3D12GraphicsCommandList* cmd) {
        cmd->CopyBufferRegion(vbDef, 0, vbUp, 0, vbSize);
        cmd->CopyBufferRegion(ibDef, 0, ibUp, 0, ibSize);
        D3D12_RESOURCE_BARRIER barriers[2] = {};
        barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[0].Transition.pResource = vbDef;
        barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barriers[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barriers[1] = barriers[0]; barriers[1].Transition.pResource = ibDef;
        barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_INDEX_BUFFER;
        cmd->ResourceBarrier(2, barriers);
    });

    asset.VertexBuffer = std::move(vbDefault);
    asset.IndexBuffer  = std::move(ibDefault);
    asset.VBView.BufferLocation = asset.VertexBuffer->GetGPUVirtualAddress();
    asset.VBView.SizeInBytes    = static_cast<UINT>(vbSize);
    asset.VBView.StrideInBytes  = sizeof(SkinnedMeshVertex);
    asset.IBView.BufferLocation = asset.IndexBuffer->GetGPUVirtualAddress();
    asset.IBView.SizeInBytes    = static_cast<UINT>(ibSize);
    asset.IBView.Format         = DXGI_FORMAT_R32_UINT;
}

// ---------------------------------------------------------------------------
// LoadSkeletalMesh  (.gltf / .glb)
// ---------------------------------------------------------------------------

const SkeletalMeshAsset* GeometryManager::LoadSkeletalMesh(const std::string& path) {
    if (path.empty() || m_failedPaths.count(path)) return nullptr;

    auto it = m_skeletalCache.find(path);
    if (it != m_skeletalCache.end()) return it->second.get();

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path,
        aiProcess_Triangulate       |
        aiProcess_FlipUVs           |
        aiProcess_CalcTangentSpace  |
        aiProcess_LimitBoneWeights  |
        aiProcess_GenSmoothNormals);

    if (!scene || !scene->HasMeshes()) {
        m_failedPaths.insert(path);
        return nullptr;
    }

    auto asset = std::make_unique<SkeletalMeshAsset>();

    // ---- Build skeleton from full node hierarchy ----
    CollectJoints(scene->mRootNode, -1, asset->Skel);

    // Pre-compute InverseBindPose for every joint from its global bind-pose transform.
    // aiBone::mOffsetMatrix (more authoritative) will overwrite these values later.
    {
        uint32_t n = static_cast<uint32_t>(asset->Skel.Joints.size());
        std::vector<Matrix4x4> globalBP(n);
        for (uint32_t ji = 0; ji < n; ++ji) {
            int32_t p = asset->Skel.Joints[ji].ParentIndex;
            globalBP[ji] = (p < 0) ? asset->Skel.Joints[ji].BindPoseLocal
                                    : globalBP[p] * asset->Skel.Joints[ji].BindPoseLocal;
            asset->Skel.Joints[ji].InverseBindPose = globalBP[ji].GetInverse();
        }
    }

    // Build mesh-index → owning node's joint-index map for rigid-attachment fallback.
    // Meshes without bone weights will be rigidly attached to their scene node.
    std::unordered_map<uint32_t, uint32_t> meshToJoint;
    {
        std::function<void(const aiNode*, uint32_t)> build = [&](const aiNode* node, uint32_t parentJoint) {
            std::string name = node->mName.C_Str();
            auto it = asset->Skel.JointMap.find(name);
            uint32_t joint = (it != asset->Skel.JointMap.end()) ? it->second : parentJoint;
            for (uint32_t k = 0; k < node->mNumMeshes; ++k)
                meshToJoint[node->mMeshes[k]] = joint;
            for (uint32_t k = 0; k < node->mNumChildren; ++k)
                build(node->mChildren[k], joint);
        };
        build(scene->mRootNode, 0);
    }

    // ---- Extract vertices, indices, bone weights ----
    std::vector<SkinnedMeshVertex> verts;
    std::vector<uint32_t>          indices;
    // Per-vertex bone influences (accumulated over all submeshes).
    // Indexed by global vertex index after all submeshes are merged.
    struct Influence { uint32_t idx[4] = {}; float wgt[4] = {}; int cnt = 0; };
    std::vector<Influence> influences;

    for (uint32_t mi = 0; mi < scene->mNumMeshes; ++mi) {
        const aiMesh* mesh = scene->mMeshes[mi];
        uint32_t baseVert = static_cast<uint32_t>(verts.size());
        uint32_t startIdx = static_cast<uint32_t>(indices.size());

        // Default joint for vertices with no bone weights = the node that owns this mesh.
        uint32_t defaultJoint = meshToJoint.count(mi) ? meshToJoint.at(mi) : 0u;

        // Vertices (position / normal / tangent / uv — bone data filled below)
        verts.resize(verts.size() + mesh->mNumVertices);
        influences.resize(influences.size() + mesh->mNumVertices);
        for (uint32_t vi = 0; vi < mesh->mNumVertices; ++vi) {
            SkinnedMeshVertex& sv = verts[baseVert + vi];
            sv = {};
            sv.px = mesh->mVertices[vi].x;
            sv.py = mesh->mVertices[vi].y;
            sv.pz = mesh->mVertices[vi].z;
            if (mesh->HasNormals()) {
                sv.nx = mesh->mNormals[vi].x;
                sv.ny = mesh->mNormals[vi].y;
                sv.nz = mesh->mNormals[vi].z;
            }
            if (mesh->HasTangentsAndBitangents()) {
                sv.tx = mesh->mTangents[vi].x;
                sv.ty = mesh->mTangents[vi].y;
                sv.tz = mesh->mTangents[vi].z;
            }
            if (mesh->HasTextureCoords(0)) {
                sv.u = mesh->mTextureCoords[0][vi].x;
                sv.v = mesh->mTextureCoords[0][vi].y;
            }
            // Default: rigidly attached to owning node joint (animates with that bone)
            sv.boneIdx[0] = defaultJoint; sv.boneWgt[0] = 1.0f;
        }

        // Indices
        for (uint32_t fi = 0; fi < mesh->mNumFaces; ++fi) {
            const aiFace& face = mesh->mFaces[fi];
            for (uint32_t ii = 0; ii < face.mNumIndices; ++ii)
                indices.push_back(face.mIndices[ii]);
        }

        // Bone weights → accumulate into influences and set inverseBindPose on joints
        for (uint32_t bi = 0; bi < mesh->mNumBones; ++bi) {
            const aiBone* bone = mesh->mBones[bi];
            std::string boneName = bone->mName.C_Str();

            // Map bone name to joint index; create if missing (rare edge case)
            auto jit = asset->Skel.JointMap.find(boneName);
            if (jit == asset->Skel.JointMap.end()) continue;
            uint32_t jointIdx = jit->second;

            // Set inverse bind-pose from this bone's offset matrix
            asset->Skel.Joints[jointIdx].InverseBindPose = ConvertAiMatrix(bone->mOffsetMatrix);

            for (uint32_t wi = 0; wi < bone->mNumWeights; ++wi) {
                uint32_t vi  = baseVert + bone->mWeights[wi].mVertexId;
                float    wgt = bone->mWeights[wi].mWeight;
                Influence& inf = influences[vi];
                if (inf.cnt < 4) {
                    inf.idx[inf.cnt] = jointIdx;
                    inf.wgt[inf.cnt] = wgt;
                    ++inf.cnt;
                }
            }
        }

        // SubMesh descriptor
        SubMesh sm;
        sm.IndexCount = static_cast<uint32_t>(indices.size()) - startIdx;
        sm.StartIndex = startIdx;
        sm.BaseVertex = static_cast<int32_t>(baseVert);
        uint32_t matIdx = mesh->mMaterialIndex;
        if (matIdx < scene->mNumMaterials) {
            const aiMaterial* mat = scene->mMaterials[matIdx];
            auto extractTexPath = [&](aiTextureType type) -> std::string {
                aiString tp;
                if (mat->GetTexture(type, 0, &tp) != AI_SUCCESS) return {};
                std::string full = tp.C_Str();
                size_t slash = full.find_last_of("/\\");
                std::string fname = (slash != std::string::npos) ? full.substr(slash + 1) : full;
                return "Resource/Textures/" + fname;
            };
            sm.DiffusePath = extractTexPath(aiTextureType_DIFFUSE);
            sm.NormalPath  = extractTexPath(aiTextureType_NORMALS);
            if (sm.NormalPath.empty())
                sm.NormalPath = extractTexPath(aiTextureType_HEIGHT);
            sm.OrmPath     = extractTexPath(aiTextureType_METALNESS);
            aiColor3D color(1.0f, 1.0f, 1.0f);
            mat->Get(AI_MATKEY_COLOR_DIFFUSE, color);
            sm.BaseColor[0] = color.r;
            sm.BaseColor[1] = color.g;
            sm.BaseColor[2] = color.b;
        }
        asset->SubMeshes.push_back(std::move(sm));
    }

    // Write accumulated influences back to vertex buffer
    for (uint32_t vi = 0; vi < static_cast<uint32_t>(verts.size()); ++vi) {
        const Influence& inf = influences[vi];
        if (inf.cnt == 0) continue;
        float wsum = 0.0f;
        for (int k = 0; k < inf.cnt; ++k) wsum += inf.wgt[k];
        float inv = (wsum > 1e-5f) ? 1.0f / wsum : 0.0f;
        verts[vi].boneIdx[0] = verts[vi].boneIdx[1] = verts[vi].boneIdx[2] = verts[vi].boneIdx[3] = 0;
        verts[vi].boneWgt[0] = verts[vi].boneWgt[1] = verts[vi].boneWgt[2] = verts[vi].boneWgt[3] = 0.0f;
        for (int k = 0; k < inf.cnt; ++k) {
            verts[vi].boneIdx[k] = inf.idx[k];
            verts[vi].boneWgt[k] = inf.wgt[k] * inv;
        }
    }

    // ---- Extract animation clips ----
    for (uint32_t ai = 0; ai < scene->mNumAnimations; ++ai) {
        const aiAnimation* anim = scene->mAnimations[ai];
        float tps = (anim->mTicksPerSecond > 0.0) ? (float)anim->mTicksPerSecond : 1000.0f;
        AnimationClip clip;
        clip.Name             = anim->mName.C_Str();
        clip.DurationSeconds  = (float)(anim->mDuration / tps);

        for (uint32_t ci = 0; ci < anim->mNumChannels; ++ci) {
            const aiNodeAnim* ch = anim->mChannels[ci];
            NodeAnim na;
            na.JointName = ch->mNodeName.C_Str();

            for (uint32_t k = 0; k < ch->mNumPositionKeys; ++k) {
                auto& key = ch->mPositionKeys[k];
                na.PositionKeys.push_back({ (float)(key.mTime / tps),
                    Vector3(key.mValue.x, key.mValue.y, key.mValue.z) });
            }
            for (uint32_t k = 0; k < ch->mNumScalingKeys; ++k) {
                auto& key = ch->mScalingKeys[k];
                na.ScaleKeys.push_back({ (float)(key.mTime / tps),
                    Vector3(key.mValue.x, key.mValue.y, key.mValue.z) });
            }
            for (uint32_t k = 0; k < ch->mNumRotationKeys; ++k) {
                auto& key = ch->mRotationKeys[k];
                na.RotationKeys.push_back({ (float)(key.mTime / tps),
                    Quaternion(key.mValue.x, key.mValue.y, key.mValue.z, key.mValue.w) });
            }
            clip.Channels.push_back(std::move(na));
        }
        asset->Clips.push_back(std::move(clip));
    }

    if (verts.empty() || indices.empty()) {
        m_failedPaths.insert(path);
        return nullptr;
    }

    UploadSkeletalGeometry(verts, indices, *asset);

    const SkeletalMeshAsset* ptr = asset.get();
    m_skeletalCache[path] = std::move(asset);
    return ptr;
}

bool GeometryManager::IsSkeletal(const std::string& path) {
    if (path.empty()) return false;
    if (m_skeletalPaths.count(path)) return true;
    if (m_cache.count(path) || m_failedPaths.count(path)) return false;
    // Not yet classified — trigger LoadMesh which populates m_skeletalPaths if bones found.
    LoadMesh(path);
    return m_skeletalPaths.count(path) > 0;
}

} // namespace Fujin
