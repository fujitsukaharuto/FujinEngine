#include "GraphicsDevice.h"
#include <cassert>
#include <stdexcept>
#include <cstdio>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

#ifdef _DEBUGMODE
#pragma comment(lib, "dxguid.lib")
#endif

namespace Fujin {

static void ThrowIfFailed(HRESULT hr) {
    if (FAILED(hr)) {
        char buf[96];
        snprintf(buf, sizeof(buf), "[GraphicsDevice] DX12 call FAILED hr=0x%08X\n", (unsigned)hr);
        OutputDebugStringA(buf);
        throw std::runtime_error("DX12 call failed");
    }
}

bool GraphicsDevice::Initialize(HWND hwnd, uint32_t width, uint32_t height) {
    m_hwnd   = hwnd;
    m_width  = width;
    m_height = height;

    try {
        CreateDevice();
        CreateCommandQueue();
        CreateSwapChain(hwnd, width, height);
        CreateRTVHeap();
        CreateRenderTargets();
        CreateFrameContexts();
        CreateCommandList();
        CreateSRVHeap();
        CreateDepthBuffer();
        CreateFence();
        // GPU timestamp profiler (non-critical: on failure it self-disables, no profiling).
        m_profiler.Initialize(m_device.Get(), m_commandQueue.Get(), 64, NUM_FRAMES_IN_FLIGHT);
    } catch (...) {
        return false;
    }
    return true;
}

void GraphicsDevice::Shutdown() {
    WaitForGPU();
    m_profiler.Shutdown();
    ReleaseRenderTargets();
    ReleaseDepthBuffer();
    if (m_fenceEvent) {
        CloseHandle(m_fenceEvent);
        m_fenceEvent = nullptr;
    }
}

void GraphicsDevice::CreateDevice() {
    UINT dxgiFlags = 0;
#ifdef _DEBUGMODE
    {
        ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
            debug->EnableDebugLayer();
            dxgiFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
    }
#endif

    ComPtr<IDXGIFactory6> factory;
    ThrowIfFailed(CreateDXGIFactory2(dxgiFlags, IID_PPV_ARGS(&factory)));

    ComPtr<IDXGIAdapter1> adapter;
    factory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter));

    ThrowIfFailed(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_device)));

#ifdef _DEBUGMODE
    {
        ComPtr<ID3D12InfoQueue> infoQueue;
        if (SUCCEEDED(m_device.As(&infoQueue))) {
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
        }
    }
#endif
}

void GraphicsDevice::CreateCommandQueue() {
    D3D12_COMMAND_QUEUE_DESC desc = {};
    desc.Type  = D3D12_COMMAND_LIST_TYPE_DIRECT;
    desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    ThrowIfFailed(m_device->CreateCommandQueue(&desc, IID_PPV_ARGS(&m_commandQueue)));
}

void GraphicsDevice::CreateSwapChain(HWND hwnd, uint32_t width, uint32_t height) {
    UINT dxgiFlags = 0;
#ifdef _DEBUGMODE
    dxgiFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif
    ComPtr<IDXGIFactory6> factory;
    ThrowIfFailed(CreateDXGIFactory2(dxgiFlags, IID_PPV_ARGS(&factory)));

    // Detect tearing support so VSync-off can present uncapped (windowed) for measurement.
    {
        BOOL allowTearing = FALSE;
        if (SUCCEEDED(factory->CheckFeatureSupport(
                DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing))))
            m_tearingSupported = (allowTearing == TRUE);
    }

    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.BufferCount = NUM_BACK_BUFFERS;
    desc.Width       = width;
    desc.Height      = height;
    desc.Format      = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.SampleDesc  = { 1, 0 };
    desc.Flags       = m_tearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    ComPtr<IDXGISwapChain1> swapChain1;
    ThrowIfFailed(factory->CreateSwapChainForHwnd(
        m_commandQueue.Get(), hwnd, &desc, nullptr, nullptr, &swapChain1
    ));
    ThrowIfFailed(factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER));
    ThrowIfFailed(swapChain1.As(&m_swapChain));

    m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
}

void GraphicsDevice::CreateRTVHeap() {
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    desc.NumDescriptors = NUM_BACK_BUFFERS;
    desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_rtvHeap)));

    m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (uint32_t i = 0; i < NUM_BACK_BUFFERS; ++i) {
        m_rtvHandles[i] = handle;
        handle.ptr += m_rtvDescriptorSize;
    }
}

void GraphicsDevice::CreateRenderTargets() {
    for (uint32_t i = 0; i < NUM_BACK_BUFFERS; ++i) {
        ThrowIfFailed(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_backBuffers[i])));
        m_device->CreateRenderTargetView(m_backBuffers[i].Get(), nullptr, m_rtvHandles[i]);
    }
}

void GraphicsDevice::ReleaseRenderTargets() {
    for (auto& bb : m_backBuffers) bb.Reset();
}

void GraphicsDevice::CreateDepthBuffer() {
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width            = m_width;
    desc.Height           = m_height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels        = 1;
    desc.Format           = DXGI_FORMAT_R32_TYPELESS;
    desc.SampleDesc.Count = 1;
    desc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearVal = {};
    clearVal.Format               = DXGI_FORMAT_D32_FLOAT;
    clearVal.DepthStencil.Depth   = 1.0f;
    clearVal.DepthStencil.Stencil = 0;

    ThrowIfFailed(m_device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearVal,
        IID_PPV_ARGS(&m_depthBuffer)));

    if (!m_dsvHeap) {
        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
        dsvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.NumDescriptors = 1;
        dsvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap)));
        m_dsvHandle = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
    }

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format             = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension      = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Texture2D.MipSlice = 0;
    m_device->CreateDepthStencilView(m_depthBuffer.Get(), &dsvDesc, m_dsvHandle);

    // Depth SRV for sampling in lighting pass (slot allocated once, reused on resize)
    if (m_depthSRVSlot == 0) {
        m_depthSRVSlot = AllocateSRVSlot();
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                    = DXGI_FORMAT_R32_FLOAT;
    srvDesc.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels       = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;
    m_device->CreateShaderResourceView(m_depthBuffer.Get(), &srvDesc, GetSRVCPUHandle(m_depthSRVSlot));
}

void GraphicsDevice::ReleaseDepthBuffer() {
    m_depthBuffer.Reset();
    // m_dsvHeap is kept across resize (just overwrite the view)
}

void GraphicsDevice::CreateFrameContexts() {
    for (auto& fc : m_frameContexts) {
        ThrowIfFailed(m_device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&fc.CommandAllocator)
        ));
    }
}

void GraphicsDevice::CreateCommandList() {
    ThrowIfFailed(m_device->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        m_frameContexts[0].CommandAllocator.Get(),
        nullptr, IID_PPV_ARGS(&m_commandList)
    ));
    ThrowIfFailed(m_commandList->Close());
}

void GraphicsDevice::CreateSRVHeap() {
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = 512;
    desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_srvHeap)));
    m_srvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

uint32_t GraphicsDevice::AllocateSRVSlot() {
    return m_srvAllocCount++;
}

D3D12_CPU_DESCRIPTOR_HANDLE GraphicsDevice::GetSRVCPUHandle(uint32_t slot) const {
    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(slot) * m_srvDescriptorSize;
    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE GraphicsDevice::GetSRVGPUHandle(uint32_t slot) const {
    D3D12_GPU_DESCRIPTOR_HANDLE handle = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<UINT64>(slot) * m_srvDescriptorSize;
    return handle;
}

void GraphicsDevice::CreateFence() {
    ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    assert(m_fenceEvent != nullptr);
}

void GraphicsDevice::BeginFrame() {
    m_frameContextIndex = (m_frameContextIndex + 1) % NUM_FRAMES_IN_FLIGHT;
    FrameContext& fc = m_frameContexts[m_frameContextIndex];

    if (m_fence->GetCompletedValue() < fc.FenceValue) {
        m_fence->SetEventOnCompletion(fc.FenceValue, m_fenceEvent);
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }

    m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
    ThrowIfFailed(fc.CommandAllocator->Reset());
    ThrowIfFailed(m_commandList->Reset(fc.CommandAllocator.Get(), nullptr));

    D3D12_RESOURCE_BARRIER barrier         = {};
    barrier.Type                           = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource           = m_backBuffers[m_backBufferIndex].Get();
    barrier.Transition.StateBefore         = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter          = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource         = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_commandList->ResourceBarrier(1, &barrier);

    // Back buffer is always fully covered by tonemap draw + ImGui dockspace,
    // so clearing it here is redundant and generates warning #820 (DXGI
    // back buffers cannot have an optimized clear value set on creation).
    m_commandList->ClearDepthStencilView(m_dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    m_commandList->OMSetRenderTargets(1, &m_rtvHandles[m_backBufferIndex], FALSE, &m_dsvHandle);

    ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get() };
    m_commandList->SetDescriptorHeaps(1, heaps);

    // The fence wait above guarantees this slot's previous submission finished, so the
    // profiler can read back that frame's timestamps now without an extra GPU stall.
    m_profiler.BeginFrame(m_frameContextIndex);
}

void GraphicsDevice::EndFrame() {
    D3D12_RESOURCE_BARRIER barrier         = {};
    barrier.Type                           = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource           = m_backBuffers[m_backBufferIndex].Get();
    barrier.Transition.StateBefore         = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter          = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.Subresource         = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_commandList->ResourceBarrier(1, &barrier);

    // Resolve this frame's timestamp queries into the readback buffer before closing.
    m_profiler.EndFrame(m_commandList.Get());

    ThrowIfFailed(m_commandList->Close());

    ID3D12CommandList* lists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(1, lists);

    const UINT syncInterval = m_vsync ? 1u : 0u;
    const UINT presentFlags = (!m_vsync && m_tearingSupported) ? DXGI_PRESENT_ALLOW_TEARING : 0u;
    m_swapChain->Present(syncInterval, presentFlags);

    const UINT64 signalValue = ++m_fenceLastSignaled;
    m_commandQueue->Signal(m_fence.Get(), signalValue);
    m_frameContexts[m_frameContextIndex].FenceValue = signalValue;
}

void GraphicsDevice::WaitForGPU() {
    const UINT64 signalValue = ++m_fenceLastSignaled;
    m_commandQueue->Signal(m_fence.Get(), signalValue);
    m_fence->SetEventOnCompletion(signalValue, m_fenceEvent);
    WaitForSingleObject(m_fenceEvent, INFINITE);
}

void GraphicsDevice::Resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0 || (width == m_width && height == m_height)) return;

    WaitForGPU();
    ReleaseRenderTargets();
    ReleaseDepthBuffer();

    m_width  = width;
    m_height = height;
    ThrowIfFailed(m_swapChain->ResizeBuffers(
        NUM_BACK_BUFFERS, width, height, DXGI_FORMAT_R8G8B8A8_UNORM,
        m_tearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0
    ));
    m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
    CreateRenderTargets();
    CreateDepthBuffer();
}

} // namespace Fujin
