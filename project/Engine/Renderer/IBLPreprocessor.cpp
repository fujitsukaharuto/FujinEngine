#include "IBLPreprocessor.h"
#include "Engine/Graphics/DxcHelper.h"
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <vector>
#include <functional>

namespace Fujin {

static constexpr float PROC_SKY_PI = 3.14159265358979f;

// ─── one-shot GPU execution ──────────────────────────────────────────────────

void IBLPreprocessor::ExecOneShot(GraphicsDevice& gfx,
                                   std::function<void(ID3D12GraphicsCommandList*)> fn) {
    ID3D12Device*       device = gfx.GetDevice();
    ID3D12CommandQueue* queue  = gfx.GetCommandQueue();

    ComPtr<ID3D12CommandAllocator> alloc;
    device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc));
    ComPtr<ID3D12GraphicsCommandList> cmd;
    device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(),
                              nullptr, IID_PPV_ARGS(&cmd));

    ID3D12DescriptorHeap* heaps[] = { gfx.GetSRVHeap() };
    cmd->SetDescriptorHeaps(1, heaps);

    fn(cmd.Get());
    cmd->Close();

    ID3D12CommandList* lists[] = { cmd.Get() };
    queue->ExecuteCommandLists(1, lists);

    ComPtr<ID3D12Fence> fence;
    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    HANDLE ev = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    queue->Signal(fence.Get(), 1);
    fence->SetEventOnCompletion(1, ev);
    WaitForSingleObject(ev, INFINITE);
    CloseHandle(ev);
}

// ─── pipeline creation ────────────────────────────────────────────────────────

bool IBLPreprocessor::CreatePipelines(ID3D12Device* device) {
    // Single root signature shared by all three compute passes:
    //   params[0]: Root constants b0 (4 x float32: roughness, mipSize, pad, pad)
    //   params[1]: Descriptor table t0 (source equirect Texture2D SRV)
    //   params[2]: Descriptor table u0 (output UAV)
    //   sampler s0: linear clamp (for env sampling)

    D3D12_ROOT_PARAMETER params[3] = {};

    params[0].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.ShaderRegister = 0;
    params[0].Constants.RegisterSpace  = 0;
    params[0].Constants.Num32BitValues = 4;
    params[0].ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_DESCRIPTOR_RANGE srvRange = {};
    srvRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors                    = 1;
    srvRange.BaseShaderRegister                = 0;
    srvRange.RegisterSpace                     = 0;
    srvRange.OffsetInDescriptorsFromTableStart = 0;
    params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges   = &srvRange;
    params[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_DESCRIPTOR_RANGE uavRange = {};
    uavRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors                    = 1;
    uavRange.BaseShaderRegister                = 0;
    uavRange.RegisterSpace                     = 0;
    uavRange.OffsetInDescriptorsFromTableStart = 0;
    params[2].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].DescriptorTable.NumDescriptorRanges = 1;
    params[2].DescriptorTable.pDescriptorRanges   = &uavRange;
    params[2].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_STATIC_SAMPLER_DESC samp = {};
    samp.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samp.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samp.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samp.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samp.MaxLOD           = D3D12_FLOAT32_MAX;
    samp.ShaderRegister   = 0;
    samp.RegisterSpace    = 0;
    samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters     = 3;
    rsDesc.pParameters       = params;
    rsDesc.NumStaticSamplers = 1;
    rsDesc.pStaticSamplers   = &samp;
    rsDesc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> rsBlob, rsErr;
    D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr);
    if (FAILED(device->CreateRootSignature(0, rsBlob->GetBufferPointer(),
                                            rsBlob->GetBufferSize(),
                                            IID_PPV_ARGS(&m_rs))))
        return false;

    auto makeCSPSO = [&](const wchar_t* file, ComPtr<ID3D12PipelineState>& pso) -> bool {
        auto cs = LoadOrCompileShader(file, L"cs_6_0");
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = m_rs.Get();
        desc.CS             = { cs->GetBufferPointer(), cs->GetBufferSize() };
        return SUCCEEDED(device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pso)));
    };

    if (!makeCSPSO(L"Resource/Shaders/IBL_BRDF.CS.hlsl",      m_brdfPSO)) return false;
    if (!makeCSPSO(L"Resource/Shaders/IBL_Irradiance.CS.hlsl", m_irrPSO))  return false;
    if (!makeCSPSO(L"Resource/Shaders/IBL_Prefilter.CS.hlsl",  m_prefPSO)) return false;
    return true;
}

// ─── GPU texture creation ─────────────────────────────────────────────────────

bool IBLPreprocessor::CreateOutputTextures(GraphicsDevice& gfx) {
    ID3D12Device* device = gfx.GetDevice();

    auto makeUAVTex = [&](UINT64 w, UINT h, UINT16 arraySize, UINT16 mips,
                          DXGI_FORMAT fmt, ComPtr<ID3D12Resource>& res) -> bool {
        D3D12_HEAP_PROPERTIES hp = {};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rd = {};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width            = w;
        rd.Height           = h;
        rd.DepthOrArraySize = arraySize;
        rd.MipLevels        = mips;
        rd.Format           = fmt;
        rd.SampleDesc.Count = 1;
        rd.Flags            = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        return SUCCEEDED(device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&res)));
    };

    // BRDF LUT: 512x512 R16G16F, Texture2D
    if (!makeUAVTex(BRDF_LUT_SIZE, BRDF_LUT_SIZE, 1, 1,
                    DXGI_FORMAT_R16G16_FLOAT, m_brdfTex)) return false;
    m_brdfSRVSlot = gfx.AllocateSRVSlot();
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
        uav.Format             = DXGI_FORMAT_R16G16_FLOAT;
        uav.ViewDimension      = D3D12_UAV_DIMENSION_TEXTURE2D;
        uav.Texture2D.MipSlice = 0;
        device->CreateUnorderedAccessView(m_brdfTex.Get(), nullptr, &uav,
                                          gfx.GetSRVCPUHandle(m_brdfSRVSlot));
    }

    // Irradiance: 32x32x6 RGBA16F
    if (!makeUAVTex(IRRADIANCE_SIZE, IRRADIANCE_SIZE, 6, 1,
                    DXGI_FORMAT_R16G16B16A16_FLOAT, m_irrTex)) return false;
    m_irrSRVSlot = gfx.AllocateSRVSlot();
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
        uav.Format                          = DXGI_FORMAT_R16G16B16A16_FLOAT;
        uav.ViewDimension                   = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
        uav.Texture2DArray.MipSlice         = 0;
        uav.Texture2DArray.FirstArraySlice  = 0;
        uav.Texture2DArray.ArraySize        = 6;
        device->CreateUnorderedAccessView(m_irrTex.Get(), nullptr, &uav,
                                          gfx.GetSRVCPUHandle(m_irrSRVSlot));
    }

    // Prefiltered env: 128x128x6 RGBA16F with PREFILTER_MIP_COUNT mips
    if (!makeUAVTex(PREFILTER_BASE_SIZE, PREFILTER_BASE_SIZE, 6, PREFILTER_MIP_COUNT,
                    DXGI_FORMAT_R16G16B16A16_FLOAT, m_prefTex)) return false;
    for (uint32_t mip = 0; mip < PREFILTER_MIP_COUNT; ++mip) {
        m_prefUAVSlots[mip] = gfx.AllocateSRVSlot();
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
        uav.Format                         = DXGI_FORMAT_R16G16B16A16_FLOAT;
        uav.ViewDimension                  = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
        uav.Texture2DArray.MipSlice        = mip;
        uav.Texture2DArray.FirstArraySlice = 0;
        uav.Texture2DArray.ArraySize       = 6;
        device->CreateUnorderedAccessView(m_prefTex.Get(), nullptr, &uav,
                                          gfx.GetSRVCPUHandle(m_prefUAVSlots[mip]));
    }
    m_prefSRVSlot = m_prefUAVSlots[0]; // will be overwritten with TextureCube SRV
    return true;
}

// ─── procedural sky upload ────────────────────────────────────────────────────

bool IBLPreprocessor::UploadDefaultSky(GraphicsDevice& gfx) {
    ID3D12Device* device = gfx.GetDevice();
    const uint32_t W = 512, H = 256;

    // Generate equirectangular sky in RGBA32F
    std::vector<float> pixels(W * H * 4);
    for (uint32_t y = 0; y < H; ++y) {
        float theta  = (float(y) + 0.5f) / float(H) * PROC_SKY_PI; // 0=top, PI=bottom
        float elev   = 1.0f - theta / (PROC_SKY_PI * 0.5f);         // 1=zenith, -1=nadir
        elev = (std::max)(-1.0f, (std::min)(1.0f, elev));
        for (uint32_t x = 0; x < W; ++x) {
            float r, g, b;
            if (elev >= 0.0f) {
                // sky: lerp horizon (0.60, 0.70, 0.80) → zenith (0.08, 0.18, 0.50)
                r = 0.60f + elev * (0.08f - 0.60f);
                g = 0.70f + elev * (0.18f - 0.70f);
                b = 0.80f + elev * (0.50f - 0.80f);
            } else {
                float t = -elev;
                // ground: lerp horizon (0.30, 0.28, 0.26) → nadir (0.05, 0.04, 0.03)
                r = 0.30f + t * (0.05f - 0.30f);
                g = 0.28f + t * (0.04f - 0.28f);
                b = 0.26f + t * (0.03f - 0.26f);
            }
            uint32_t idx = (y * W + x) * 4;
            pixels[idx + 0] = r;
            pixels[idx + 1] = g;
            pixels[idx + 2] = b;
            pixels[idx + 3] = 1.0f;
        }
    }

    // Create the GPU texture (RGBA32F, single mip, Texture2D)
    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width            = W;
    rd.Height           = H;
    rd.DepthOrArraySize = 1;
    rd.MipLevels        = 1;
    rd.Format           = DXGI_FORMAT_R32G32B32A32_FLOAT;
    rd.SampleDesc.Count = 1;
    if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_skyTex))))
        return false;

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp;
    UINT   numRows;
    UINT64 rowSize, uploadSize;
    device->GetCopyableFootprints(&rd, 0, 1, 0, &fp, &numRows, &rowSize, &uploadSize);

    D3D12_HEAP_PROPERTIES uhp = {};
    uhp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC urd = {};
    urd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    urd.Width            = uploadSize;
    urd.Height           = 1;
    urd.DepthOrArraySize = 1;
    urd.MipLevels        = 1;
    urd.Format           = DXGI_FORMAT_UNKNOWN;
    urd.SampleDesc.Count = 1;
    urd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> uploadBuf;
    device->CreateCommittedResource(&uhp, D3D12_HEAP_FLAG_NONE, &urd,
                                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                    IID_PPV_ARGS(&uploadBuf));

    uint8_t* dst = nullptr;
    D3D12_RANGE readRange = { 0, 0 };
    uploadBuf->Map(0, &readRange, reinterpret_cast<void**>(&dst));
    dst += fp.Offset;
    const uint8_t* src = reinterpret_cast<const uint8_t*>(pixels.data());
    uint32_t srcRowPitch = W * 4 * sizeof(float);
    for (UINT row = 0; row < numRows; ++row)
        memcpy(dst + row * fp.Footprint.RowPitch, src + row * srcRowPitch, rowSize);
    uploadBuf->Unmap(0, nullptr);

    ID3D12Resource* skyRaw    = m_skyTex.Get();
    ID3D12Resource* uploadRaw = uploadBuf.Get();
    auto fpCopy = fp;
    ExecOneShot(gfx, [=](ID3D12GraphicsCommandList* cmd) {
        D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
        dstLoc.pResource        = skyRaw;
        dstLoc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstLoc.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
        srcLoc.pResource       = uploadRaw;
        srcLoc.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        srcLoc.PlacedFootprint = fpCopy;
        cmd->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

        D3D12_RESOURCE_BARRIER b = {};
        b.Type                          = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource          = skyRaw;
        b.Transition.StateBefore        = D3D12_RESOURCE_STATE_COPY_DEST;
        b.Transition.StateAfter         = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                                          D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        b.Transition.Subresource        = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1, &b);
    });

    m_skySRVSlot = gfx.AllocateSRVSlot();
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                    = DXGI_FORMAT_R32G32B32A32_FLOAT;
    srvDesc.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels       = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;
    device->CreateShaderResourceView(m_skyTex.Get(), &srvDesc,
                                     gfx.GetSRVCPUHandle(m_skySRVSlot));
    return true;
}

// ─── compute dispatch ─────────────────────────────────────────────────────────

void IBLPreprocessor::RunCompute(GraphicsDevice& gfx) {
    // Capture slot values for lambda
    uint32_t skySRV    = m_skySRVSlot;
    uint32_t brdfSlot  = m_brdfSRVSlot;
    uint32_t irrSlot   = m_irrSRVSlot;
    uint32_t prefSlots[PREFILTER_MIP_COUNT];
    for (uint32_t i = 0; i < PREFILTER_MIP_COUNT; ++i) prefSlots[i] = m_prefUAVSlots[i];

    ID3D12RootSignature* rs       = m_rs.Get();
    ID3D12PipelineState* brdfPSO  = m_brdfPSO.Get();
    ID3D12PipelineState* irrPSO   = m_irrPSO.Get();
    ID3D12PipelineState* prefPSO  = m_prefPSO.Get();
    ID3D12Resource*      brdfTex  = m_brdfTex.Get();
    ID3D12Resource*      irrTex   = m_irrTex.Get();
    ID3D12Resource*      prefTex  = m_prefTex.Get();

    ExecOneShot(gfx, [&](ID3D12GraphicsCommandList* cmd) {
        cmd->SetComputeRootSignature(rs);

        // ── BRDF LUT ──────────────────────────────────────────────────────────
        cmd->SetPipelineState(brdfPSO);
        float zeros[4] = { 0, 0, 0, 0 };
        cmd->SetComputeRoot32BitConstants(0, 4, zeros, 0);
        cmd->SetComputeRootDescriptorTable(1, gfx.GetSRVGPUHandle(skySRV)); // unused but must be bound
        cmd->SetComputeRootDescriptorTable(2, gfx.GetSRVGPUHandle(brdfSlot));
        cmd->Dispatch(BRDF_LUT_SIZE / 8, BRDF_LUT_SIZE / 8, 1);

        // UAV barrier before next read
        D3D12_RESOURCE_BARRIER uavBar = {};
        uavBar.Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        uavBar.UAV.pResource = brdfTex;
        cmd->ResourceBarrier(1, &uavBar);

        // ── Irradiance ────────────────────────────────────────────────────────
        cmd->SetPipelineState(irrPSO);
        cmd->SetComputeRoot32BitConstants(0, 4, zeros, 0);
        cmd->SetComputeRootDescriptorTable(1, gfx.GetSRVGPUHandle(skySRV));
        cmd->SetComputeRootDescriptorTable(2, gfx.GetSRVGPUHandle(irrSlot));
        uint32_t irrGroups = (IRRADIANCE_SIZE + 7) / 8;
        cmd->Dispatch(irrGroups, irrGroups, 6);

        uavBar.UAV.pResource = irrTex;
        cmd->ResourceBarrier(1, &uavBar);

        // ── Prefiltered env ───────────────────────────────────────────────────
        cmd->SetPipelineState(prefPSO);
        cmd->SetComputeRootDescriptorTable(1, gfx.GetSRVGPUHandle(skySRV));
        for (uint32_t mip = 0; mip < PREFILTER_MIP_COUNT; ++mip) {
            float roughness  = float(mip) / float(PREFILTER_MIP_COUNT - 1);
            float mipSize    = float(PREFILTER_BASE_SIZE >> mip);
            float consts[4]  = { roughness, mipSize, 0, 0 };
            cmd->SetComputeRoot32BitConstants(0, 4, consts, 0);
            cmd->SetComputeRootDescriptorTable(2, gfx.GetSRVGPUHandle(prefSlots[mip]));
            uint32_t groups = (std::max)(1u, (uint32_t(PREFILTER_BASE_SIZE >> mip) + 7) / 8);
            cmd->Dispatch(groups, groups, 6);
            uavBar.UAV.pResource = prefTex;
            cmd->ResourceBarrier(1, &uavBar);
        }

        // ── Transition all outputs to shader-readable ─────────────────────────
        D3D12_RESOURCE_BARRIER barriers[3] = {};
        for (auto& b : barriers) {
            b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            b.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                                       D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        }
        barriers[0].Transition.pResource = brdfTex;
        barriers[1].Transition.pResource = irrTex;
        barriers[2].Transition.pResource = prefTex;
        cmd->ResourceBarrier(3, barriers);
    });

    ID3D12Device* device = gfx.GetDevice();

    // Overwrite BRDF slot with SRV (2D, R16G16F)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
        srv.Format                    = DXGI_FORMAT_R16G16_FLOAT;
        srv.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels       = 1;
        srv.Texture2D.MostDetailedMip = 0;
        device->CreateShaderResourceView(brdfTex, &srv,
                                         gfx.GetSRVCPUHandle(m_brdfSRVSlot));
    }

    // Overwrite irradiance slot with TextureCube SRV
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
        srv.Format                       = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srv.ViewDimension                = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srv.Shader4ComponentMapping      = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.TextureCube.MostDetailedMip  = 0;
        srv.TextureCube.MipLevels        = 1;
        device->CreateShaderResourceView(irrTex, &srv,
                                         gfx.GetSRVCPUHandle(m_irrSRVSlot));
    }

    // Overwrite prefilter slot[0] with TextureCube SRV (all mips)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
        srv.Format                       = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srv.ViewDimension                = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srv.Shader4ComponentMapping      = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.TextureCube.MostDetailedMip  = 0;
        srv.TextureCube.MipLevels        = PREFILTER_MIP_COUNT;
        device->CreateShaderResourceView(prefTex, &srv,
                                         gfx.GetSRVCPUHandle(m_prefSRVSlot));
    }
}

// ─── public interface ─────────────────────────────────────────────────────────

bool IBLPreprocessor::Initialize(GraphicsDevice& gfx) {
    try {
        if (!CreatePipelines(gfx.GetDevice()))  return false;
        if (!CreateOutputTextures(gfx))          return false;
        if (!UploadDefaultSky(gfx))              return false;
        RunCompute(gfx);
        m_ready = true;
    } catch (...) {
        return false;
    }
    return true;
}

void IBLPreprocessor::Shutdown() {
    m_skyTex.Reset();
    m_brdfTex.Reset();
    m_irrTex.Reset();
    m_prefTex.Reset();
    m_brdfPSO.Reset();
    m_irrPSO.Reset();
    m_prefPSO.Reset();
    m_rs.Reset();
    m_ready = false;
}

} // namespace Fujin
