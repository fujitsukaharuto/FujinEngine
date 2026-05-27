#include "TextureManager.h"
#include <DirectXTex.h>
#include <functional>
#include <cstdint>
#include <stdexcept>

namespace Fujin {

bool TextureManager::Initialize(GraphicsDevice& gfx) {
    m_gfx    = &gfx;
    m_device = gfx.GetDevice();
    m_queue  = gfx.GetCommandQueue();

    // Create 2×2 white fallback texture
    uint32_t pixels[4] = { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF };
    m_fallbackSlot = UploadTexture(pixels, 2, 2, DXGI_FORMAT_R8G8B8A8_UNORM, 2 * sizeof(uint32_t));

    // Create 2×2 flat normal map (R=128,G=128,B=255 → tangent-space (0,0,1))
    uint32_t flatNorm[4] = { 0xFFFF8080, 0xFFFF8080, 0xFFFF8080, 0xFFFF8080 };
    m_flatNormalSlot = UploadTexture(flatNorm, 2, 2, DXGI_FORMAT_R8G8B8A8_UNORM, 2 * sizeof(uint32_t));

    return m_fallbackSlot != UINT32_MAX;
}

void TextureManager::Shutdown() {
    m_textures.clear();
    m_cache.clear();
}

void TextureManager::UploadOneShot(std::function<void(ID3D12GraphicsCommandList*)> fn) {
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

uint32_t TextureManager::UploadTexture(const void* pixels, uint32_t width, uint32_t height,
                                        DXGI_FORMAT format, uint32_t srcRowPitch) {
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width            = width;
    texDesc.Height           = height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels        = 1;
    texDesc.Format           = format;
    texDesc.SampleDesc.Count = 1;
    texDesc.Flags            = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    ComPtr<ID3D12Resource> texture;
    if (FAILED(m_device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE,
            &texDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture))))
        return UINT32_MAX;

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
    UINT   numRows;
    UINT64 rowSizeInBytes, uploadSize;
    m_device->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, &numRows, &rowSizeInBytes, &uploadSize);

    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC uploadDesc = {};
    uploadDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadDesc.Width            = uploadSize;
    uploadDesc.Height           = 1;
    uploadDesc.DepthOrArraySize = 1;
    uploadDesc.MipLevels        = 1;
    uploadDesc.Format           = DXGI_FORMAT_UNKNOWN;
    uploadDesc.SampleDesc.Count = 1;
    uploadDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> uploadBuffer;
    m_device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuffer));

    UINT8* pDst = nullptr;
    D3D12_RANGE readRange = { 0, 0 };
    uploadBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pDst));
    pDst += footprint.Offset;
    const UINT8* pSrc = static_cast<const UINT8*>(pixels);
    for (UINT row = 0; row < numRows; ++row)
        memcpy(pDst + static_cast<size_t>(row) * footprint.Footprint.RowPitch,
               pSrc + static_cast<size_t>(row) * srcRowPitch,
               rowSizeInBytes);
    uploadBuffer->Unmap(0, nullptr);

    ID3D12Resource* texRaw    = texture.Get();
    ID3D12Resource* uploadRaw = uploadBuffer.Get();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = footprint;

    UploadOneShot([=](ID3D12GraphicsCommandList* cmd) {
        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource        = texRaw;
        dst.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.pResource        = uploadRaw;
        src.Type             = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint  = fp;
        cmd->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type                          = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource          = texRaw;
        barrier.Transition.StateBefore        = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter         = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource        = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1, &barrier);
    });

    uint32_t slot = m_gfx->AllocateSRVSlot();
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                    = format;
    srvDesc.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels       = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;
    m_device->CreateShaderResourceView(texture.Get(), &srvDesc, m_gfx->GetSRVCPUHandle(slot));

    m_textures.push_back(std::move(texture));
    return slot;
}

uint32_t TextureManager::LoadTexture(const std::string& path) {
    auto it = m_cache.find(path);
    if (it != m_cache.end()) return it->second;

    // Convert path to wide string for DirectXTex
    std::wstring wpath(path.begin(), path.end());

    DirectX::ScratchImage image;
    HRESULT hr;

    // Try WIC first (PNG, JPG, BMP, TGA, etc.)
    hr = DirectX::LoadFromWICFile(wpath.c_str(), DirectX::WIC_FLAGS_NONE, nullptr, image);
    if (FAILED(hr)) {
        // Try DDS
        hr = DirectX::LoadFromDDSFile(wpath.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image);
    }
    if (FAILED(hr)) {
        m_cache[path] = m_fallbackSlot;
        return m_fallbackSlot;
    }

    const DirectX::Image* img = image.GetImages();
    uint32_t slot = UploadTexture(img->pixels,
        static_cast<uint32_t>(img->width), static_cast<uint32_t>(img->height),
        img->format, static_cast<uint32_t>(img->rowPitch));

    if (slot == UINT32_MAX) slot = m_fallbackSlot;
    m_cache[path] = slot;
    return slot;
}

} // namespace Fujin
