#include "GBuffer.h"

namespace Fujin {

bool GBuffer::Initialize(GraphicsDevice& gfx, uint32_t width, uint32_t height) {
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NumDescriptors = RT_COUNT;
    rtvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(gfx.GetDevice()->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap))))
        return false;

    UINT rtvSize = gfx.GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (uint32_t i = 0; i < RT_COUNT; ++i) {
        m_rtvHandles[i] = handle;
        handle.ptr += rtvSize;
    }

    if (!m_srvSlotsAllocated) {
        for (uint32_t i = 0; i < RT_COUNT; ++i)
            m_srvSlots[i] = gfx.AllocateSRVSlot();
        m_srvSlotsAllocated = true;
    }

    CreateResources(gfx, width, height);
    return true;
}

void GBuffer::Shutdown() {
    ReleaseResources();
    m_rtvHeap.Reset();
}

void GBuffer::Resize(GraphicsDevice& gfx, uint32_t width, uint32_t height) {
    ReleaseResources();
    CreateResources(gfx, width, height);
}

void GBuffer::CreateResources(GraphicsDevice& gfx, uint32_t width, uint32_t height) {
    ID3D12Device* device = gfx.GetDevice();

    for (uint32_t i = 0; i < RT_COUNT; ++i) {
        D3D12_CLEAR_VALUE clearVal = {};
        clearVal.Format = RT_FORMATS[i];

        D3D12_HEAP_PROPERTIES hp = {};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC rd = {};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width            = width;
        rd.Height           = height;
        rd.DepthOrArraySize = 1;
        rd.MipLevels        = 1;
        rd.Format           = RT_FORMATS[i];
        rd.SampleDesc.Count = 1;
        rd.Flags            = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearVal, IID_PPV_ARGS(&m_rts[i]));

        device->CreateRenderTargetView(m_rts[i].Get(), nullptr, m_rtvHandles[i]);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                    = RT_FORMATS[i];
        srvDesc.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels       = 1;
        srvDesc.Texture2D.MostDetailedMip = 0;
        device->CreateShaderResourceView(m_rts[i].Get(), &srvDesc, gfx.GetSRVCPUHandle(m_srvSlots[i]));
    }
}

void GBuffer::ReleaseResources() {
    for (auto& rt : m_rts) rt.Reset();
}

void GBuffer::TransitionToWrite(ID3D12GraphicsCommandList* cmd) {
    D3D12_RESOURCE_BARRIER barriers[RT_COUNT] = {};
    for (uint32_t i = 0; i < RT_COUNT; ++i) {
        barriers[i].Type                          = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[i].Transition.pResource          = m_rts[i].Get();
        barriers[i].Transition.StateBefore        = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[i].Transition.StateAfter         = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barriers[i].Transition.Subresource        = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    }
    cmd->ResourceBarrier(RT_COUNT, barriers);
}

void GBuffer::TransitionToRead(ID3D12GraphicsCommandList* cmd) {
    D3D12_RESOURCE_BARRIER barriers[RT_COUNT] = {};
    for (uint32_t i = 0; i < RT_COUNT; ++i) {
        barriers[i].Type                          = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[i].Transition.pResource          = m_rts[i].Get();
        barriers[i].Transition.StateBefore        = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barriers[i].Transition.StateAfter         = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[i].Transition.Subresource        = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    }
    cmd->ResourceBarrier(RT_COUNT, barriers);
}

void GBuffer::Clear(ID3D12GraphicsCommandList* cmd) {
    constexpr float zeros[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    for (uint32_t i = 0; i < RT_COUNT; ++i)
        cmd->ClearRenderTargetView(m_rtvHandles[i], zeros, 0, nullptr);
}

} // namespace Fujin
