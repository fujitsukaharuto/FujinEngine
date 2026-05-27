#pragma once
#include "Engine/Graphics/GraphicsDevice.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <functional>
#include <cstdint>

namespace Fujin {

using Microsoft::WRL::ComPtr;

class TextureManager {
public:
    bool Initialize(GraphicsDevice& gfx);
    void Shutdown();

    uint32_t LoadTexture(const std::string& path);
    uint32_t GetFallbackSlot()   const { return m_fallbackSlot; }
    uint32_t GetFlatNormalSlot() const { return m_flatNormalSlot; }

private:
    GraphicsDevice*     m_gfx    = nullptr;
    ID3D12Device*       m_device = nullptr;
    ID3D12CommandQueue* m_queue  = nullptr;
    uint32_t            m_fallbackSlot   = 0;
    uint32_t            m_flatNormalSlot = 0;

    std::unordered_map<std::string, uint32_t>     m_cache;
    std::vector<ComPtr<ID3D12Resource>>           m_textures;

    void UploadOneShot(std::function<void(ID3D12GraphicsCommandList*)> fn);
    uint32_t UploadTexture(const void* pixels, uint32_t width, uint32_t height,
                           DXGI_FORMAT format, uint32_t srcRowPitch);
};

} // namespace Fujin
