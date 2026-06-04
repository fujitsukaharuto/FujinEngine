#pragma once
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstdint>
#include "GpuProfiler.h"

namespace Fujin {

using Microsoft::WRL::ComPtr;

constexpr uint32_t NUM_BACK_BUFFERS     = 2;
constexpr uint32_t NUM_FRAMES_IN_FLIGHT = 2;

struct FrameContext {
    ComPtr<ID3D12CommandAllocator> CommandAllocator;
    UINT64 FenceValue = 0;
};

class GraphicsDevice {
public:
    bool Initialize(HWND hwnd, uint32_t width, uint32_t height);
    void Shutdown();

    void BeginFrame();
    void EndFrame();
    void WaitForGPU();
    void Resize(uint32_t width, uint32_t height);

    ID3D12Device*               GetDevice()            const { return m_device.Get(); }
    ID3D12GraphicsCommandList*  GetCommandList()       const { return m_commandList.Get(); }
    ID3D12CommandQueue*         GetCommandQueue()      const { return m_commandQueue.Get(); }
    ID3D12DescriptorHeap*       GetSRVHeap()           const { return m_srvHeap.Get(); }
    D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentRTV()        const { return m_rtvHandles[m_backBufferIndex]; }
    D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentDSV()        const { return m_dsvHandle; }
    uint32_t                    GetCurrentFrameIndex() const { return m_frameContextIndex; }
    uint32_t                    GetWidth()             const { return m_width; }
    uint32_t                    GetHeight()            const { return m_height; }
    ID3D12Resource*             GetDepthBuffer()       const { return m_depthBuffer.Get(); }
    uint32_t                    GetDepthSRVSlot()      const { return m_depthSRVSlot; }
    uint32_t                    GetSRVDescriptorSize() const { return m_srvDescriptorSize; }

    uint32_t                    AllocateSRVSlot();
    D3D12_CPU_DESCRIPTOR_HANDLE GetSRVCPUHandle(uint32_t slot) const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetSRVGPUHandle(uint32_t slot) const;

    GpuProfiler&                GetProfiler()          { return m_profiler; }

    // VSync toggle: when off, present with no sync interval (tearing allowed if the
    // swap chain supports it) so the frame rate is uncapped for measurement.
    void                        SetVSync(bool v)       { m_vsync = v; }
    bool                        GetVSync()       const { return m_vsync; }

private:
    void CreateDevice();
    void CreateCommandQueue();
    void CreateSwapChain(HWND hwnd, uint32_t width, uint32_t height);
    void CreateRTVHeap();
    void CreateRenderTargets();
    void ReleaseRenderTargets();
    void CreateDepthBuffer();
    void ReleaseDepthBuffer();
    void CreateFrameContexts();
    void CreateCommandList();
    void CreateSRVHeap();
    void CreateFence();

    ComPtr<ID3D12Device>              m_device;
    ComPtr<ID3D12CommandQueue>        m_commandQueue;
    ComPtr<IDXGISwapChain3>           m_swapChain;
    ComPtr<ID3D12DescriptorHeap>      m_rtvHeap;
    ComPtr<ID3D12Resource>            m_backBuffers[NUM_BACK_BUFFERS];
    D3D12_CPU_DESCRIPTOR_HANDLE       m_rtvHandles[NUM_BACK_BUFFERS] = {};
    FrameContext                      m_frameContexts[NUM_FRAMES_IN_FLIGHT];
    ComPtr<ID3D12GraphicsCommandList> m_commandList;
    ComPtr<ID3D12DescriptorHeap>      m_srvHeap;

    ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
    ComPtr<ID3D12Resource>       m_depthBuffer;
    D3D12_CPU_DESCRIPTOR_HANDLE  m_dsvHandle       = {};
    uint32_t                     m_depthSRVSlot    = 0;

    uint32_t                     m_srvDescriptorSize = 0;
    uint32_t                     m_srvAllocCount     = 1; // slot 0 reserved for ImGui

    ComPtr<ID3D12Fence> m_fence;
    HANDLE              m_fenceEvent        = nullptr;
    UINT64              m_fenceLastSignaled = 0;

    uint32_t m_frameContextIndex = NUM_FRAMES_IN_FLIGHT - 1;
    uint32_t m_backBufferIndex   = 0;
    uint32_t m_rtvDescriptorSize = 0;

    HWND     m_hwnd   = nullptr;
    uint32_t m_width  = 0;
    uint32_t m_height = 0;

    GpuProfiler m_profiler;
    bool        m_vsync            = true;  // present sync interval 1 vs 0
    bool        m_tearingSupported = false; // DXGI allow-tearing capability
};

} // namespace Fujin
