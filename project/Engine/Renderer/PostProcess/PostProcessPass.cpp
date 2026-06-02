#include "PostProcessPass.h"
#include "Engine/Graphics/DxcHelper.h"
#include <stdexcept>
#include <cstring>
#include <algorithm>

namespace Fujin {

// ─── helpers ──────────────────────────────────────────────────────────────────

static ComPtr<ID3D12Resource> CreateUploadCB(ID3D12Device* dev, UINT64 sz, uint8_t** mapped) {
    D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC   rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width = sz;
    rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_UNKNOWN; rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> res;
    D3D12_RANGE range = {};
    dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&res));
    res->Map(0, &range, reinterpret_cast<void**>(mapped));
    return res;
}

static ComPtr<ID3D12Resource> CreateDefaultTex2D(
        ID3D12Device* dev, DXGI_FORMAT fmt,
        uint32_t w, uint32_t h, D3D12_RESOURCE_FLAGS flags,
        D3D12_RESOURCE_STATES initState,
        const D3D12_CLEAR_VALUE* clearVal = nullptr) {
    D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width            = (UINT64)w; rd.Height = h;
    rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.Format           = fmt; rd.SampleDesc.Count = 1;
    rd.Flags            = flags;
    ComPtr<ID3D12Resource> res;
    HRESULT hr = dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, initState, clearVal, IID_PPV_ARGS(&res));
    if (FAILED(hr)) {
        char buf[256];
        snprintf(buf, sizeof(buf), "[PostProcess] CreateDefaultTex2D FAILED hr=0x%08X w=%u h=%u fmt=%u flags=%u state=%u\n",
                 (unsigned)hr, w, h, (unsigned)fmt, (unsigned)flags, (unsigned)initState);
        OutputDebugStringA(buf);
    }
    return res;
}

void PostProcessPass::Barrier(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res,
                               D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    if (!res || before == after) return;
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource   = res;
    b.Transition.StateBefore = before;
    b.Transition.StateAfter  = after;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd->ResourceBarrier(1, &b);
}

void PostProcessPass::UAVBarrier(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res) {
    D3D12_RESOURCE_BARRIER b = {};
    b.Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    b.UAV.pResource = res;
    cmd->ResourceBarrier(1, &b);
}

void PostProcessPass::DispatchCompute(ID3D12GraphicsCommandList* cmd, uint32_t w, uint32_t h) const {
    cmd->Dispatch((w + 7) / 8, (h + 7) / 8, 1);
}

// ─── Initialize / Shutdown / Resize ──────────────────────────────────────────

bool PostProcessPass::Initialize(GraphicsDevice& gfx, uint32_t width, uint32_t height) {
    m_width  = width;
    m_height = height;

    if (!CreateHDRRT(gfx))        return false;
    if (!CreateSSAOResources(gfx)) return false;
    if (!CreateBloomResources(gfx)) return false;
    if (!CreateTonemapResources(gfx)) return false;
    if (!CreateTAAResources(gfx))     return false;
    if (!CreateSSRResources(gfx))     return false;
    if (!CreateFogResources(gfx))     return false;
    return true;
}

void PostProcessPass::ReleaseResolutionResources() {
    m_hdrRT.Reset();
    m_ssaoRT.Reset(); m_ssaoBlurRT.Reset();
    m_bloomA.Reset(); m_bloomB.Reset();
    m_taaTex[0].Reset(); m_taaTex[1].Reset();
    m_taaHistoryValid = false;
    m_ssrTex.Reset();
    m_fogTex.Reset();
}

void PostProcessPass::Resize(GraphicsDevice& gfx, uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) return;
    if (width == m_width && height == m_height && m_hdrRT && m_bloomA && m_bloomB) return;
    m_width  = width;
    m_height = height;
    ReleaseResolutionResources();
    bool hdrOk  = CreateHDRRT(gfx);
    bool ssaoOk = CreateSSAOResources(gfx);
    bool blmOk  = CreateBloomResources(gfx);
    CreateTAAResources(gfx);
    CreateSSRResources(gfx);
    CreateFogResources(gfx);
    if (!hdrOk || !ssaoOk || !blmOk) {
        char buf[128];
        snprintf(buf, sizeof(buf), "[PostProcess] Resize failed: hdr=%d ssao=%d bloom=%d\n",
                 (int)hdrOk, (int)ssaoOk, (int)blmOk);
        OutputDebugStringA(buf);
    }
    // PSOs and CBs do not need recreation on resize
}

void PostProcessPass::Shutdown() {
    for (uint32_t i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i) {
        if (m_ssaoCBMapped[i])    { m_ssaoCB[i]->Unmap(0, nullptr);    m_ssaoCBMapped[i] = nullptr; }
        if (m_bloomCBMapped[i])   { m_bloomCB[i]->Unmap(0, nullptr);   m_bloomCBMapped[i] = nullptr; }
        if (m_tonemapCBMapped[i]) { m_tonemapCB[i]->Unmap(0, nullptr); m_tonemapCBMapped[i] = nullptr; }
        if (m_taaCBMapped[i])     { m_taaCB[i]->Unmap(0, nullptr);     m_taaCBMapped[i] = nullptr; }
        if (m_ssrCBMapped[i])     { m_ssrCB[i]->Unmap(0, nullptr);     m_ssrCBMapped[i] = nullptr; }
        if (m_fogCBMapped[i])     { m_fogCB[i]->Unmap(0, nullptr);     m_fogCBMapped[i] = nullptr; }
        m_ssaoCB[i].Reset();
        m_bloomCB[i].Reset();
        m_tonemapCB[i].Reset();
        m_taaCB[i].Reset();
        m_ssrCB[i].Reset();
        m_fogCB[i].Reset();
    }
    m_ssaoPSO.Reset();  m_ssaoRS.Reset();
    m_ssaoBlurPSO.Reset(); m_ssaoBlurRS.Reset();
    m_bloomDownPSO.Reset(); m_bloomUpPSO.Reset(); m_bloomRS.Reset();
    m_tonemapPSO.Reset(); m_tonemapRS.Reset();
    m_taaPSO.Reset(); m_taaRS.Reset();
    m_ssrPSO.Reset(); m_ssrRS.Reset();
    m_fogPSO.Reset(); m_fogRS.Reset();
    ReleaseResolutionResources();
    m_hdrRTVHeap.Reset();
}

// ─── Resource creation ────────────────────────────────────────────────────────

bool PostProcessPass::CreateHDRRT(GraphicsDevice& gfx) {
    ID3D12Device* dev = gfx.GetDevice();
    static constexpr DXGI_FORMAT HDR_FMT = DXGI_FORMAT_R16G16B16A16_FLOAT;

    D3D12_CLEAR_VALUE hdrClear = {};
    hdrClear.Format   = HDR_FMT;
    hdrClear.Color[0] = 0.08f; hdrClear.Color[1] = 0.08f;
    hdrClear.Color[2] = 0.10f; hdrClear.Color[3] = 1.00f;
    m_hdrRT = CreateDefaultTex2D(dev, HDR_FMT, m_width, m_height,
                                  D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                                  D3D12_RESOURCE_STATE_RENDER_TARGET,
                                  &hdrClear);
    if (!m_hdrRT) return false;
    m_hdrState = D3D12_RESOURCE_STATE_RENDER_TARGET;

    // RTV
    if (!m_hdrRTVHeap) {
        D3D12_DESCRIPTOR_HEAP_DESC hd = {};
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; hd.NumDescriptors = 1;
        if (FAILED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_hdrRTVHeap)))) return false;
        m_hdrRTV = m_hdrRTVHeap->GetCPUDescriptorHandleForHeapStart();
    }
    D3D12_RENDER_TARGET_VIEW_DESC rtvd = {};
    rtvd.Format        = HDR_FMT;
    rtvd.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    dev->CreateRenderTargetView(m_hdrRT.Get(), &rtvd, m_hdrRTV);

    // SRV (allocate once)
    if (m_hdrSRVSlot == 0) m_hdrSRVSlot = gfx.AllocateSRVSlot();
    D3D12_SHADER_RESOURCE_VIEW_DESC srvd = {};
    srvd.Format                  = HDR_FMT;
    srvd.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvd.Texture2D.MipLevels     = 1;
    dev->CreateShaderResourceView(m_hdrRT.Get(), &srvd, gfx.GetSRVCPUHandle(m_hdrSRVSlot));
    return true;
}

bool PostProcessPass::CreateSSAOResources(GraphicsDevice& gfx) {
    ID3D12Device* dev = gfx.GetDevice();

    m_ssaoRT     = CreateDefaultTex2D(dev, DXGI_FORMAT_R8_UNORM, m_width, m_height,
                                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    m_ssaoBlurRT = CreateDefaultTex2D(dev, DXGI_FORMAT_R8_UNORM, m_width, m_height,
                                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (!m_ssaoRT || !m_ssaoBlurRT) return false;
    m_ssaoState     = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    m_ssaoBlurState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    // Allocate SRV/UAV slots (once)
    if (m_ssaoUAVSlot == 0) {
        m_ssaoUAVSlot     = gfx.AllocateSRVSlot();
        m_ssaoSRVSlot     = gfx.AllocateSRVSlot();
        m_ssaoBlurUAVSlot = gfx.AllocateSRVSlot();
        m_ssaoBlurSRVSlot = gfx.AllocateSRVSlot();
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvd = {};
    srvd.Format = DXGI_FORMAT_R8_UNORM; srvd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvd.Texture2D.MipLevels = 1;

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavd = {};
    uavd.Format = DXGI_FORMAT_R8_UNORM; uavd.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

    dev->CreateUnorderedAccessView(m_ssaoRT.Get(),     nullptr, &uavd, gfx.GetSRVCPUHandle(m_ssaoUAVSlot));
    dev->CreateShaderResourceView (m_ssaoRT.Get(),     &srvd,         gfx.GetSRVCPUHandle(m_ssaoSRVSlot));
    dev->CreateUnorderedAccessView(m_ssaoBlurRT.Get(), nullptr, &uavd, gfx.GetSRVCPUHandle(m_ssaoBlurUAVSlot));
    dev->CreateShaderResourceView (m_ssaoBlurRT.Get(), &srvd,         gfx.GetSRVCPUHandle(m_ssaoBlurSRVSlot));

    // SSAO root signature
    if (!m_ssaoRS) {
        D3D12_DESCRIPTOR_RANGE ranges[3] = {};
        ranges[0] = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0 }; // t0 normals
        ranges[1] = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, 0 }; // t1 depth
        ranges[2] = { D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 0 }; // u0 output

        D3D12_ROOT_PARAMETER params[4] = {};
        params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[0].Descriptor.ShaderRegister = 0;
        params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        for (uint32_t i = 0; i < 3; ++i) {
            params[1 + i].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[1 + i].DescriptorTable.NumDescriptorRanges = 1;
            params[1 + i].DescriptorTable.pDescriptorRanges   = &ranges[i];
            params[1 + i].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;
        }

        D3D12_STATIC_SAMPLER_DESC samps[2] = {};
        samps[0].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        samps[0].AddressU = samps[0].AddressV = samps[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samps[0].MaxLOD = D3D12_FLOAT32_MAX; samps[0].ShaderRegister = 0;
        samps[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        samps[1].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samps[1].AddressU = samps[1].AddressV = samps[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samps[1].MaxLOD = D3D12_FLOAT32_MAX; samps[1].ShaderRegister = 1;
        samps[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC rsd = {};
        rsd.NumParameters = 4; rsd.pParameters = params;
        rsd.NumStaticSamplers = 2; rsd.pStaticSamplers = samps;
        ComPtr<ID3DBlob> blob, err;
        D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
        dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&m_ssaoRS));

        // SSAO blur root signature (simpler: t0 SRV + u0 UAV)
        D3D12_DESCRIPTOR_RANGE blurRanges[2] = {};
        blurRanges[0] = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0 };
        blurRanges[1] = { D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 0 };
        D3D12_ROOT_PARAMETER blurParams[2] = {};
        blurParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        blurParams[0].DescriptorTable = { 1, &blurRanges[0] };
        blurParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        blurParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        blurParams[1].DescriptorTable = { 1, &blurRanges[1] };
        blurParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_STATIC_SAMPLER_DESC blurSamp = {};
        blurSamp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        blurSamp.AddressU = blurSamp.AddressV = blurSamp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        blurSamp.MaxLOD = D3D12_FLOAT32_MAX; blurSamp.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC blurRsd = {};
        blurRsd.NumParameters = 2; blurRsd.pParameters = blurParams;
        blurRsd.NumStaticSamplers = 1; blurRsd.pStaticSamplers = &blurSamp;
        ComPtr<ID3DBlob> blurBlob, blurErr;
        D3D12SerializeRootSignature(&blurRsd, D3D_ROOT_SIGNATURE_VERSION_1, &blurBlob, &blurErr);
        dev->CreateRootSignature(0, blurBlob->GetBufferPointer(), blurBlob->GetBufferSize(), IID_PPV_ARGS(&m_ssaoBlurRS));

        // Compile shaders
        auto ssaoCS     = LoadOrCompileShader(L"Resource/Shaders/SSAO.CS.hlsl",     L"cs_6_0");
        auto ssaoBlurCS = LoadOrCompileShader(L"Resource/Shaders/SSAOBlur.CS.hlsl", L"cs_6_0");

        D3D12_COMPUTE_PIPELINE_STATE_DESC cpd = {};
        cpd.pRootSignature = m_ssaoRS.Get();
        cpd.CS = { ssaoCS->GetBufferPointer(), ssaoCS->GetBufferSize() };
        if (FAILED(dev->CreateComputePipelineState(&cpd, IID_PPV_ARGS(&m_ssaoPSO)))) return false;

        cpd.pRootSignature = m_ssaoBlurRS.Get();
        cpd.CS = { ssaoBlurCS->GetBufferPointer(), ssaoBlurCS->GetBufferSize() };
        if (FAILED(dev->CreateComputePipelineState(&cpd, IID_PPV_ARGS(&m_ssaoBlurPSO)))) return false;
    }

    // Constant buffers
    if (!m_ssaoCB[0]) {
        for (uint32_t i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i)
            m_ssaoCB[i] = CreateUploadCB(dev, SSAO_CB_SIZE, &m_ssaoCBMapped[i]);
    }
    return true;
}

bool PostProcessPass::CreateBloomResources(GraphicsDevice& gfx) {
    ID3D12Device* dev = gfx.GetDevice();
    static constexpr DXGI_FORMAT BLOOM_FMT = DXGI_FORMAT_R16G16B16A16_FLOAT;

    uint32_t hw = (std::max)(1u, m_width / 2);
    uint32_t hh = (std::max)(1u, m_height / 2);

    m_bloomA = CreateDefaultTex2D(dev, BLOOM_FMT, hw, hh,
                                   D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    m_bloomB = CreateDefaultTex2D(dev, BLOOM_FMT, hw, hh,
                                   D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (!m_bloomA || !m_bloomB) return false;
    m_bloomAState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    m_bloomBState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    if (m_bloomAUAVSlot == 0) {
        m_bloomAUAVSlot = gfx.AllocateSRVSlot();
        m_bloomASRVSlot = gfx.AllocateSRVSlot();
        m_bloomBUAVSlot = gfx.AllocateSRVSlot();
        m_bloomBSRVSlot = gfx.AllocateSRVSlot();
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavd = {};
    uavd.Format = BLOOM_FMT; uavd.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    D3D12_SHADER_RESOURCE_VIEW_DESC srvd = {};
    srvd.Format = BLOOM_FMT; srvd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvd.Texture2D.MipLevels = 1;

    dev->CreateUnorderedAccessView(m_bloomA.Get(), nullptr, &uavd, gfx.GetSRVCPUHandle(m_bloomAUAVSlot));
    dev->CreateShaderResourceView (m_bloomA.Get(), &srvd,         gfx.GetSRVCPUHandle(m_bloomASRVSlot));
    dev->CreateUnorderedAccessView(m_bloomB.Get(), nullptr, &uavd, gfx.GetSRVCPUHandle(m_bloomBUAVSlot));
    dev->CreateShaderResourceView (m_bloomB.Get(), &srvd,         gfx.GetSRVCPUHandle(m_bloomBSRVSlot));

    if (!m_bloomRS) {
        D3D12_DESCRIPTOR_RANGE ranges[2] = {};
        ranges[0] = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0 }; // t0
        ranges[1] = { D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 0 }; // u0
        D3D12_ROOT_PARAMETER params[3] = {};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[0].Descriptor.ShaderRegister = 0;
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable = { 1, &ranges[0] };
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[2].DescriptorTable = { 1, &ranges[1] };
        params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_STATIC_SAMPLER_DESC samp = {};
        samp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samp.AddressU = samp.AddressV = samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samp.MaxLOD = D3D12_FLOAT32_MAX; samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC rsd = {};
        rsd.NumParameters = 3; rsd.pParameters = params;
        rsd.NumStaticSamplers = 1; rsd.pStaticSamplers = &samp;
        ComPtr<ID3DBlob> blob, err;
        D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
        dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&m_bloomRS));

        auto downCS = LoadOrCompileShader(L"Resource/Shaders/BloomDownsample.CS.hlsl", L"cs_6_0");
        auto upCS   = LoadOrCompileShader(L"Resource/Shaders/BloomUpsample.CS.hlsl",   L"cs_6_0");

        D3D12_COMPUTE_PIPELINE_STATE_DESC cpd = {};
        cpd.pRootSignature = m_bloomRS.Get();
        cpd.CS = { downCS->GetBufferPointer(), downCS->GetBufferSize() };
        if (FAILED(dev->CreateComputePipelineState(&cpd, IID_PPV_ARGS(&m_bloomDownPSO)))) return false;
        cpd.CS = { upCS->GetBufferPointer(), upCS->GetBufferSize() };
        if (FAILED(dev->CreateComputePipelineState(&cpd, IID_PPV_ARGS(&m_bloomUpPSO)))) return false;
    }
    // Bloom CB: 4 sub-allocations at 256B aligned offsets (one per dispatch)
    if (!m_bloomCB[0]) {
        for (uint32_t i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i)
            m_bloomCB[i] = CreateUploadCB(dev, BLOOM_CB_SIZE, &m_bloomCBMapped[i]);
    }
    return true;
}

bool PostProcessPass::CreateTonemapResources(GraphicsDevice& gfx) {
    ID3D12Device* dev = gfx.GetDevice();

    if (!m_tonemapRS) {
        D3D12_DESCRIPTOR_RANGE ranges[2] = {};
        ranges[0] = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0 }; // t0 HDR
        ranges[1] = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, 0 }; // t1 bloom

        D3D12_ROOT_PARAMETER params[3] = {};
        params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[0].Descriptor.ShaderRegister = 0;
        params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_PIXEL;
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable = { 1, &ranges[0] };
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[2].DescriptorTable = { 1, &ranges[1] };
        params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_STATIC_SAMPLER_DESC samps[2] = {};
        samps[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samps[0].AddressU = samps[0].AddressV = samps[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samps[0].MaxLOD = D3D12_FLOAT32_MAX; samps[0].ShaderRegister = 0;
        samps[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        samps[1].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        samps[1].AddressU = samps[1].AddressV = samps[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samps[1].MaxLOD = D3D12_FLOAT32_MAX; samps[1].ShaderRegister = 1;
        samps[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC rsd = {};
        rsd.NumParameters = 3; rsd.pParameters = params;
        rsd.NumStaticSamplers = 2; rsd.pStaticSamplers = samps;
        ComPtr<ID3DBlob> blob, err;
        D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
        dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&m_tonemapRS));

        auto vs = LoadOrCompileShader(L"Resource/Shaders/LightingPass.VS.hlsl", L"vs_6_0");
        auto ps = LoadOrCompileShader(L"Resource/Shaders/TonemapFXAA.PS.hlsl", L"ps_6_0");

        D3D12_BLEND_DESC bd = {}; bd.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        D3D12_RASTERIZER_DESC rd = {}; rd.FillMode = D3D12_FILL_MODE_SOLID; rd.CullMode = D3D12_CULL_MODE_NONE;
        D3D12_DEPTH_STENCIL_DESC dsd = {}; dsd.DepthEnable = FALSE;

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psd = {};
        psd.pRootSignature        = m_tonemapRS.Get();
        psd.VS                    = { vs->GetBufferPointer(), vs->GetBufferSize() };
        psd.PS                    = { ps->GetBufferPointer(), ps->GetBufferSize() };
        psd.InputLayout           = { nullptr, 0 };
        psd.RasterizerState       = rd;
        psd.BlendState            = bd;
        psd.DepthStencilState     = dsd;
        psd.SampleMask            = UINT_MAX;
        psd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psd.NumRenderTargets      = 1;
        psd.RTVFormats[0]         = DXGI_FORMAT_R8G8B8A8_UNORM;
        psd.SampleDesc            = { 1, 0 };
        if (FAILED(dev->CreateGraphicsPipelineState(&psd, IID_PPV_ARGS(&m_tonemapPSO)))) return false;
    }

    for (uint32_t i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i) {
        if (!m_tonemapCB[i])
            m_tonemapCB[i] = CreateUploadCB(dev, TONEMAP_CB_SIZE, &m_tonemapCBMapped[i]);
    }
    return true;
}

// ─── ExecuteSSAO ─────────────────────────────────────────────────────────────

void PostProcessPass::ExecuteSSAO(ID3D12GraphicsCommandList* cmd,
                                    GraphicsDevice& gfx,
                                    const GBuffer& gbuffer,
                                    const Matrix4x4& viewProj,
                                    const Matrix4x4& invViewProj,
                                    uint32_t frameIndex,
                                    uint32_t vpX, uint32_t vpY, uint32_t vpW, uint32_t vpH) {
    if (!m_ssaoRT || !m_ssaoBlurRT) {
        OutputDebugStringA("[PostProcess] ExecuteSSAO skipped: SSAO resources null\n");
        return;
    }

    if (vpW == 0 || vpH == 0) { vpX = 0; vpY = 0; vpW = m_width; vpH = m_height; }

    uint32_t fi = frameIndex % NUM_FRAMES_IN_FLIGHT;

    // Upload SSAO CB (InvViewProj, ViewProj, radius, bias, viewport)
    struct SSAOCB {
        float invVP[16];
        float vp[16];
        float radius, bias, _p0, _p1;
        float viewport[4];
        float rtSize[2];
        float _p2[2];
    };
    SSAOCB cb = {};
    memcpy(cb.invVP, invViewProj.v, 64);
    memcpy(cb.vp,    viewProj.v,    64);
    cb.radius = 0.5f; cb.bias = 0.025f;
    cb.viewport[0] = (float)vpX; cb.viewport[1] = (float)vpY;
    cb.viewport[2] = (float)vpW; cb.viewport[3] = (float)vpH;
    cb.rtSize[0]   = (float)m_width; cb.rtSize[1] = (float)m_height;
    memcpy(m_ssaoCBMapped[fi], &cb, sizeof(cb));

    // Transition SSAO RT to UAV (was NON_PIXEL after last frame's blur read)
    Barrier(cmd, m_ssaoRT.Get(), m_ssaoState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    m_ssaoState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    // SSAO blur RT starts as UAV (or was transitioned back at end of last frame's ExecuteFinal)
    Barrier(cmd, m_ssaoBlurRT.Get(), m_ssaoBlurState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    m_ssaoBlurState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    ID3D12DescriptorHeap* heaps[] = { gfx.GetSRVHeap() };
    cmd->SetDescriptorHeaps(1, heaps);

    // ── SSAO compute ──
    cmd->SetComputeRootSignature(m_ssaoRS.Get());
    cmd->SetPipelineState(m_ssaoPSO.Get());
    cmd->SetComputeRootConstantBufferView(0, m_ssaoCB[fi]->GetGPUVirtualAddress());
    cmd->SetComputeRootDescriptorTable(1, gfx.GetSRVGPUHandle(gbuffer.GetSRVSlot(1))); // normals
    cmd->SetComputeRootDescriptorTable(2, gfx.GetSRVGPUHandle(gfx.GetDepthSRVSlot())); // depth
    cmd->SetComputeRootDescriptorTable(3, gfx.GetSRVGPUHandle(m_ssaoUAVSlot));
    DispatchCompute(cmd, m_width, m_height);

    UAVBarrier(cmd, m_ssaoRT.Get());

    // Transition SSAO raw → SRV for blur read
    Barrier(cmd, m_ssaoRT.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    m_ssaoState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

    // ── SSAO blur compute ──
    cmd->SetComputeRootSignature(m_ssaoBlurRS.Get());
    cmd->SetPipelineState(m_ssaoBlurPSO.Get());
    cmd->SetComputeRootDescriptorTable(0, gfx.GetSRVGPUHandle(m_ssaoSRVSlot));
    cmd->SetComputeRootDescriptorTable(1, gfx.GetSRVGPUHandle(m_ssaoBlurUAVSlot));
    DispatchCompute(cmd, m_width, m_height);

    UAVBarrier(cmd, m_ssaoBlurRT.Get());

    // Transition blur result to PSR for LightingPass PS
    Barrier(cmd, m_ssaoBlurRT.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    m_ssaoBlurState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
}

// ─── TAA ──────────────────────────────────────────────────────────────────────

bool PostProcessPass::CreateTAAResources(GraphicsDevice& gfx) {
    ID3D12Device* dev = gfx.GetDevice();
    static constexpr DXGI_FORMAT TAA_FMT = DXGI_FORMAT_R16G16B16A16_FLOAT;

    for (int i = 0; i < 2; ++i) {
        m_taaTex[i] = CreateDefaultTex2D(dev, TAA_FMT, m_width, m_height,
                                         D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        if (!m_taaTex[i]) return false;
        m_taaState[i] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    m_taaHistoryValid = false;
    m_taaWriteIndex   = 0;

    if (m_taaSRVSlot[0] == 0) {
        for (int i = 0; i < 2; ++i) {
            m_taaUAVSlot[i] = gfx.AllocateSRVSlot();
            m_taaSRVSlot[i] = gfx.AllocateSRVSlot();
        }
    }
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavd = {};
    uavd.Format = TAA_FMT; uavd.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    D3D12_SHADER_RESOURCE_VIEW_DESC srvd = {};
    srvd.Format = TAA_FMT; srvd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvd.Texture2D.MipLevels = 1;
    for (int i = 0; i < 2; ++i) {
        dev->CreateUnorderedAccessView(m_taaTex[i].Get(), nullptr, &uavd, gfx.GetSRVCPUHandle(m_taaUAVSlot[i]));
        dev->CreateShaderResourceView (m_taaTex[i].Get(), &srvd,         gfx.GetSRVCPUHandle(m_taaSRVSlot[i]));
    }

    if (!m_taaRS) {
        D3D12_DESCRIPTOR_RANGE ranges[5] = {};
        ranges[0] = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0 }; // t0 current
        ranges[1] = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, 0 }; // t1 history
        ranges[2] = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0, 0 }; // t2 depth
        ranges[3] = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3, 0, 0 }; // t3 velocity
        ranges[4] = { D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 0 }; // u0 output
        D3D12_ROOT_PARAMETER params[6] = {};
        params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[0].Descriptor.ShaderRegister = 0;
        params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        for (uint32_t i = 0; i < 5; ++i) {
            params[1 + i].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[1 + i].DescriptorTable.NumDescriptorRanges = 1;
            params[1 + i].DescriptorTable.pDescriptorRanges   = &ranges[i];
            params[1 + i].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;
        }
        D3D12_STATIC_SAMPLER_DESC samps[2] = {};
        samps[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samps[0].AddressU = samps[0].AddressV = samps[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samps[0].MaxLOD = D3D12_FLOAT32_MAX; samps[0].ShaderRegister = 0;
        samps[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        samps[1].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        samps[1].AddressU = samps[1].AddressV = samps[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samps[1].MaxLOD = D3D12_FLOAT32_MAX; samps[1].ShaderRegister = 1;
        samps[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC rsd = {};
        rsd.NumParameters = 6; rsd.pParameters = params;
        rsd.NumStaticSamplers = 2; rsd.pStaticSamplers = samps;
        ComPtr<ID3DBlob> blob, err;
        if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err))) return false;
        if (FAILED(dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&m_taaRS)))) return false;

        auto taaCS = LoadOrCompileShader(L"Resource/Shaders/TAA.CS.hlsl", L"cs_6_0");
        if (!taaCS) return false;
        D3D12_COMPUTE_PIPELINE_STATE_DESC cpd = {};
        cpd.pRootSignature = m_taaRS.Get();
        cpd.CS = { taaCS->GetBufferPointer(), taaCS->GetBufferSize() };
        if (FAILED(dev->CreateComputePipelineState(&cpd, IID_PPV_ARGS(&m_taaPSO)))) return false;
    }
    if (!m_taaCB[0]) {
        for (uint32_t i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i)
            m_taaCB[i] = CreateUploadCB(dev, TONEMAP_CB_SIZE, &m_taaCBMapped[i]);
    }
    return true;
}

void PostProcessPass::ExecuteTAA(ID3D12GraphicsCommandList* cmd,
                                  GraphicsDevice& gfx,
                                  uint32_t frameIndex,
                                  ID3D12Resource* depthResource,
                                  uint32_t depthSRVSlot,
                                  ID3D12Resource* velocityResource,
                                  uint32_t velocitySRVSlot,
                                  const Matrix4x4& invViewProjCur,
                                  const Matrix4x4& viewProjPrev,
                                  float jitterDeltaU, float jitterDeltaV,
                                  uint32_t vpX, uint32_t vpY, uint32_t vpW, uint32_t vpH) {
    if (!TaaEnabled || !m_taaTex[0] || !m_taaTex[1] || !m_taaPSO || !depthResource || !velocityResource) return;

    uint32_t fi   = frameIndex % NUM_FRAMES_IN_FLIGHT;
    uint32_t dst  = m_taaWriteIndex;     // this frame's output
    uint32_t hist = 1u - dst;            // last frame's output = history

    // Full-RT fallback if no sub-viewport was supplied.
    if (vpW == 0 || vpH == 0) { vpX = 0; vpY = 0; vpW = m_width; vpH = m_height; }

    struct TAACB {
        float invVPcur[16];
        float vpPrev[16];
        float viewport[4];   // x, y, w, h
        float rtW, rtH, blend, valid;
        float jitterDU, jitterDV, pad0, pad1;
    };
    TAACB cb = {};
    memcpy(cb.invVPcur, invViewProjCur.v, 64);
    memcpy(cb.vpPrev,   viewProjPrev.v,   64);
    cb.viewport[0] = (float)vpX; cb.viewport[1] = (float)vpY;
    cb.viewport[2] = (float)vpW; cb.viewport[3] = (float)vpH;
    cb.rtW = (float)m_width; cb.rtH = (float)m_height;
    cb.blend  = TaaHistoryBlend;
    cb.valid  = m_taaHistoryValid ? 1.0f : 0.0f;
    cb.jitterDU = jitterDeltaU; cb.jitterDV = jitterDeltaV;
    memcpy(m_taaCBMapped[fi], &cb, sizeof(cb));

    ID3D12DescriptorHeap* heaps[] = { gfx.GetSRVHeap() };
    cmd->SetDescriptorHeaps(1, heaps);

    // States: HDR → SRV read, depth → SRV read, history → SRV, output → UAV.
    Barrier(cmd, m_hdrRT.Get(), m_hdrState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    m_hdrState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    Barrier(cmd, depthResource, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Barrier(cmd, velocityResource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Barrier(cmd, m_taaTex[hist].Get(), m_taaState[hist], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    m_taaState[hist] = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    Barrier(cmd, m_taaTex[dst].Get(), m_taaState[dst], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    m_taaState[dst] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    cmd->SetComputeRootSignature(m_taaRS.Get());
    cmd->SetPipelineState(m_taaPSO.Get());
    cmd->SetComputeRootConstantBufferView(0, m_taaCB[fi]->GetGPUVirtualAddress());
    cmd->SetComputeRootDescriptorTable(1, gfx.GetSRVGPUHandle(m_hdrSRVSlot));
    cmd->SetComputeRootDescriptorTable(2, gfx.GetSRVGPUHandle(m_taaSRVSlot[hist]));
    cmd->SetComputeRootDescriptorTable(3, gfx.GetSRVGPUHandle(depthSRVSlot));
    cmd->SetComputeRootDescriptorTable(4, gfx.GetSRVGPUHandle(velocitySRVSlot));
    cmd->SetComputeRootDescriptorTable(5, gfx.GetSRVGPUHandle(m_taaUAVSlot[dst]));
    DispatchCompute(cmd, m_width, m_height);
    UAVBarrier(cmd, m_taaTex[dst].Get());

    // Copy the resolved result back into the HDR RT so bloom/tonemap run unchanged.
    Barrier(cmd, m_taaTex[dst].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    m_taaState[dst] = D3D12_RESOURCE_STATE_COPY_SOURCE;
    Barrier(cmd, m_hdrRT.Get(), m_hdrState, D3D12_RESOURCE_STATE_COPY_DEST);
    m_hdrState = D3D12_RESOURCE_STATE_COPY_DEST;
    cmd->CopyResource(m_hdrRT.Get(), m_taaTex[dst].Get());
    Barrier(cmd, m_hdrRT.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);
    m_hdrState = D3D12_RESOURCE_STATE_RENDER_TARGET;

    // Restore depth + velocity for the rest of the frame / next frame's RenderGraph tracker.
    Barrier(cmd, depthResource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    Barrier(cmd, velocityResource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    m_taaWriteIndex   = hist;     // next frame writes to the texture we just used as history
    m_taaHistoryValid = true;
}

// ─── SSR ────────────────────────────────────────────────────────────────────────

bool PostProcessPass::CreateSSRResources(GraphicsDevice& gfx) {
    ID3D12Device* dev = gfx.GetDevice();
    static constexpr DXGI_FORMAT FMT = DXGI_FORMAT_R16G16B16A16_FLOAT;

    m_ssrTex = CreateDefaultTex2D(dev, FMT, m_width, m_height,
                                  D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (!m_ssrTex) return false;
    m_ssrState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    if (m_ssrSRVSlot == 0) { m_ssrUAVSlot = gfx.AllocateSRVSlot(); m_ssrSRVSlot = gfx.AllocateSRVSlot(); }
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavd = {}; uavd.Format = FMT; uavd.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    D3D12_SHADER_RESOURCE_VIEW_DESC srvd = {}; srvd.Format = FMT; srvd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; srvd.Texture2D.MipLevels = 1;
    dev->CreateUnorderedAccessView(m_ssrTex.Get(), nullptr, &uavd, gfx.GetSRVCPUHandle(m_ssrUAVSlot));
    dev->CreateShaderResourceView (m_ssrTex.Get(), &srvd,         gfx.GetSRVCPUHandle(m_ssrSRVSlot));

    if (!m_ssrRS) {
        D3D12_DESCRIPTOR_RANGE ranges[4] = {};
        ranges[0] = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0 }; // t0 scene HDR
        ranges[1] = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, 0 }; // t1 depth
        ranges[2] = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0, 0 }; // t2 normal
        ranges[3] = { D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 0 }; // u0 output
        D3D12_ROOT_PARAMETER params[5] = {};
        params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[0].Descriptor.ShaderRegister = 0;
        params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        for (uint32_t i = 0; i < 4; ++i) {
            params[1 + i].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[1 + i].DescriptorTable.NumDescriptorRanges = 1;
            params[1 + i].DescriptorTable.pDescriptorRanges   = &ranges[i];
            params[1 + i].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;
        }
        D3D12_STATIC_SAMPLER_DESC samps[2] = {};
        samps[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samps[0].AddressU = samps[0].AddressV = samps[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samps[0].MaxLOD = D3D12_FLOAT32_MAX; samps[0].ShaderRegister = 0;
        samps[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        samps[1].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        samps[1].AddressU = samps[1].AddressV = samps[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samps[1].MaxLOD = D3D12_FLOAT32_MAX; samps[1].ShaderRegister = 1;
        samps[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC rsd = {};
        rsd.NumParameters = 5; rsd.pParameters = params;
        rsd.NumStaticSamplers = 2; rsd.pStaticSamplers = samps;
        ComPtr<ID3DBlob> blob, err;
        if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err))) return false;
        if (FAILED(dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&m_ssrRS)))) return false;

        auto ssrCS = LoadOrCompileShader(L"Resource/Shaders/SSR.CS.hlsl", L"cs_6_0");
        if (!ssrCS) return false;
        D3D12_COMPUTE_PIPELINE_STATE_DESC cpd = {};
        cpd.pRootSignature = m_ssrRS.Get();
        cpd.CS = { ssrCS->GetBufferPointer(), ssrCS->GetBufferSize() };
        if (FAILED(dev->CreateComputePipelineState(&cpd, IID_PPV_ARGS(&m_ssrPSO)))) return false;
    }
    if (!m_ssrCB[0]) {
        for (uint32_t i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i)
            m_ssrCB[i] = CreateUploadCB(dev, TONEMAP_CB_SIZE, &m_ssrCBMapped[i]);
    }
    return true;
}

void PostProcessPass::ExecuteSSR(ID3D12GraphicsCommandList* cmd,
                                  GraphicsDevice& gfx,
                                  const GBuffer& gbuffer,
                                  ID3D12Resource* depthResource,
                                  uint32_t depthSRVSlot,
                                  const Matrix4x4& viewProj,
                                  const Matrix4x4& invViewProj,
                                  const Vector3& cameraPos,
                                  uint32_t frameIndex,
                                  uint32_t vpX, uint32_t vpY, uint32_t vpW, uint32_t vpH) {
    if (!SsrEnabled || !m_ssrTex || !m_ssrPSO || !depthResource) return;
    if (vpW == 0 || vpH == 0) { vpX = 0; vpY = 0; vpW = m_width; vpH = m_height; }

    uint32_t fi = frameIndex % NUM_FRAMES_IN_FLIGHT;

    struct SSRCB {
        float invVP[16]; float vp[16];
        float camPos[3]; float _p0;
        float viewport[4];
        float rtW, rtH, maxDist, thickness;
        int   maxSteps; float stride, roughCut, intensity;
    };
    const float stride   = 0.25f;
    const int   maxSteps = 48;
    SSRCB cb = {};
    memcpy(cb.invVP, invViewProj.v, 64);
    memcpy(cb.vp,    viewProj.v,    64);
    cb.camPos[0] = cameraPos.x; cb.camPos[1] = cameraPos.y; cb.camPos[2] = cameraPos.z;
    cb.viewport[0] = (float)vpX; cb.viewport[1] = (float)vpY;
    cb.viewport[2] = (float)vpW; cb.viewport[3] = (float)vpH;
    cb.rtW = (float)m_width; cb.rtH = (float)m_height;
    cb.maxDist = stride * maxSteps; cb.thickness = SsrThickness;
    cb.maxSteps = maxSteps; cb.stride = stride;
    cb.roughCut = SsrRoughnessCutoff; cb.intensity = SsrIntensity;
    memcpy(m_ssrCBMapped[fi], &cb, sizeof(cb));

    ID3D12DescriptorHeap* heaps[] = { gfx.GetSRVHeap() };
    cmd->SetDescriptorHeaps(1, heaps);

    ID3D12Resource* normalRes = gbuffer.GetResource(1);
    // Read states: HDR → SRV, depth → SRV, normal → SRV, output → UAV.
    Barrier(cmd, m_hdrRT.Get(), m_hdrState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    m_hdrState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    Barrier(cmd, depthResource, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Barrier(cmd, normalRes, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Barrier(cmd, m_ssrTex.Get(), m_ssrState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    m_ssrState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    cmd->SetComputeRootSignature(m_ssrRS.Get());
    cmd->SetPipelineState(m_ssrPSO.Get());
    cmd->SetComputeRootConstantBufferView(0, m_ssrCB[fi]->GetGPUVirtualAddress());
    cmd->SetComputeRootDescriptorTable(1, gfx.GetSRVGPUHandle(m_hdrSRVSlot));
    cmd->SetComputeRootDescriptorTable(2, gfx.GetSRVGPUHandle(depthSRVSlot));
    cmd->SetComputeRootDescriptorTable(3, gfx.GetSRVGPUHandle(gbuffer.GetSRVSlot(1)));
    cmd->SetComputeRootDescriptorTable(4, gfx.GetSRVGPUHandle(m_ssrUAVSlot));
    DispatchCompute(cmd, m_width, m_height);
    UAVBarrier(cmd, m_ssrTex.Get());

    // Copy the composited result back into the HDR RT so the rest of the chain is unchanged.
    Barrier(cmd, m_ssrTex.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    m_ssrState = D3D12_RESOURCE_STATE_COPY_SOURCE;
    Barrier(cmd, m_hdrRT.Get(), m_hdrState, D3D12_RESOURCE_STATE_COPY_DEST);
    m_hdrState = D3D12_RESOURCE_STATE_COPY_DEST;
    cmd->CopyResource(m_hdrRT.Get(), m_ssrTex.Get());
    Barrier(cmd, m_hdrRT.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);
    m_hdrState = D3D12_RESOURCE_STATE_RENDER_TARGET;

    // Restore depth + normal for the rest of the frame / next frame's trackers.
    Barrier(cmd, depthResource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    Barrier(cmd, normalRes, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

// ─── Height fog ───────────────────────────────────────────────────────────────

bool PostProcessPass::CreateFogResources(GraphicsDevice& gfx) {
    ID3D12Device* dev = gfx.GetDevice();
    static constexpr DXGI_FORMAT FMT = DXGI_FORMAT_R16G16B16A16_FLOAT;

    m_fogTex = CreateDefaultTex2D(dev, FMT, m_width, m_height,
                                  D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (!m_fogTex) return false;
    m_fogState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    if (m_fogSRVSlot == 0) { m_fogUAVSlot = gfx.AllocateSRVSlot(); m_fogSRVSlot = gfx.AllocateSRVSlot(); }
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavd = {}; uavd.Format = FMT; uavd.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    D3D12_SHADER_RESOURCE_VIEW_DESC srvd = {}; srvd.Format = FMT; srvd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; srvd.Texture2D.MipLevels = 1;
    dev->CreateUnorderedAccessView(m_fogTex.Get(), nullptr, &uavd, gfx.GetSRVCPUHandle(m_fogUAVSlot));
    dev->CreateShaderResourceView (m_fogTex.Get(), &srvd,         gfx.GetSRVCPUHandle(m_fogSRVSlot));

    if (!m_fogRS) {
        D3D12_DESCRIPTOR_RANGE ranges[4] = {};
        ranges[0] = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0 }; // t0 scene HDR
        ranges[1] = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, 0 }; // t1 depth
        ranges[2] = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0, 0 }; // t2 CSM shadow array
        ranges[3] = { D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 0 }; // u0 output
        D3D12_ROOT_PARAMETER params[5] = {};
        params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[0].Descriptor.ShaderRegister = 0;
        params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        for (uint32_t i = 0; i < 4; ++i) {
            params[1 + i].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[1 + i].DescriptorTable.NumDescriptorRanges = 1;
            params[1 + i].DescriptorTable.pDescriptorRanges   = &ranges[i];
            params[1 + i].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;
        }
        D3D12_STATIC_SAMPLER_DESC samps[2] = {};
        samps[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samps[0].AddressU = samps[0].AddressV = samps[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samps[0].MaxLOD = D3D12_FLOAT32_MAX; samps[0].ShaderRegister = 0;
        samps[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        samps[1].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        samps[1].AddressU = samps[1].AddressV = samps[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samps[1].MaxLOD = D3D12_FLOAT32_MAX; samps[1].ShaderRegister = 1;
        samps[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC rsd = {};
        rsd.NumParameters = 5; rsd.pParameters = params;
        rsd.NumStaticSamplers = 2; rsd.pStaticSamplers = samps;
        ComPtr<ID3DBlob> blob, err;
        if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err))) return false;
        if (FAILED(dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&m_fogRS)))) return false;

        auto fogCS = LoadOrCompileShader(L"Resource/Shaders/VolumetricFog.CS.hlsl", L"cs_6_0");
        if (!fogCS) return false;
        D3D12_COMPUTE_PIPELINE_STATE_DESC cpd = {};
        cpd.pRootSignature = m_fogRS.Get();
        cpd.CS = { fogCS->GetBufferPointer(), fogCS->GetBufferSize() };
        if (FAILED(dev->CreateComputePipelineState(&cpd, IID_PPV_ARGS(&m_fogPSO)))) return false;
    }
    if (!m_fogCB[0]) {
        for (uint32_t i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i)
            m_fogCB[i] = CreateUploadCB(dev, FOG_CB_SIZE, &m_fogCBMapped[i]);
    }
    return true;
}

void PostProcessPass::ExecuteFog(ID3D12GraphicsCommandList* cmd,
                                 GraphicsDevice& gfx,
                                 ID3D12Resource* depthResource,
                                 uint32_t depthSRVSlot,
                                 const Matrix4x4& invViewProj,
                                 const Vector3& cameraPos,
                                 const Vector3& cameraForward,
                                 const Vector3& sunDir,
                                 const Vector3& sunColor,
                                 uint32_t shadowSRVSlot,
                                 const Matrix4x4* lightViewProj,
                                 const float* cascadeSplits,
                                 uint32_t frameIndex,
                                 uint32_t vpX, uint32_t vpY, uint32_t vpW, uint32_t vpH) {
    if (!FogEnabled || !m_fogTex || !m_fogPSO || !depthResource) return;
    if (vpW == 0 || vpH == 0) { vpX = 0; vpY = 0; vpW = m_width; vpH = m_height; }
    uint32_t fi = frameIndex % NUM_FRAMES_IN_FLIGHT;

    struct FogCB {
        float invVP[16];
        float camPos[3];        float _p0;
        float viewport[4];
        float rtSize[2];        float _p1[2];
        float sunDir[3];        float _p2;
        float sunColor[3];      float _p3;
        float fogColor[3];      float density;
        float inscatterColor[3]; float heightFalloff;
        float fogHeight; float maxOpacity; float inscatterExp; float _p4;
        float lightViewProj[64];          // 4 × float4x4
        float cascadeSplits[4];
        float cameraForward[3]; float _p5;
        float godRayIntensity; float godRayMaxDist; int godRaySteps; float godRayG;
    };
    FogCB cb = {};
    memcpy(cb.invVP, invViewProj.v, 64);
    cb.camPos[0] = cameraPos.x; cb.camPos[1] = cameraPos.y; cb.camPos[2] = cameraPos.z;
    cb.viewport[0] = (float)vpX; cb.viewport[1] = (float)vpY;
    cb.viewport[2] = (float)vpW; cb.viewport[3] = (float)vpH;
    cb.rtSize[0] = (float)m_width; cb.rtSize[1] = (float)m_height;
    cb.sunDir[0] = sunDir.x; cb.sunDir[1] = sunDir.y; cb.sunDir[2] = sunDir.z;
    cb.sunColor[0] = sunColor.x; cb.sunColor[1] = sunColor.y; cb.sunColor[2] = sunColor.z;
    cb.fogColor[0] = FogColor[0]; cb.fogColor[1] = FogColor[1]; cb.fogColor[2] = FogColor[2];
    cb.density = FogDensity;
    cb.inscatterColor[0] = FogInscatterColor[0]; cb.inscatterColor[1] = FogInscatterColor[1]; cb.inscatterColor[2] = FogInscatterColor[2];
    cb.heightFalloff = FogHeightFalloff;
    cb.fogHeight = FogHeight; cb.maxOpacity = FogMaxOpacity; cb.inscatterExp = FogInscatterExp;
    for (int c = 0; c < 4; ++c) memcpy(cb.lightViewProj + c * 16, lightViewProj[c].v, 64);
    cb.cascadeSplits[0] = cascadeSplits[0]; cb.cascadeSplits[1] = cascadeSplits[1];
    cb.cascadeSplits[2] = cascadeSplits[2]; cb.cascadeSplits[3] = cascadeSplits[3];
    cb.cameraForward[0] = cameraForward.x; cb.cameraForward[1] = cameraForward.y; cb.cameraForward[2] = cameraForward.z;
    cb.godRayIntensity = GodRayIntensity; cb.godRayMaxDist = GodRayMaxDist;
    cb.godRaySteps = GodRaySteps; cb.godRayG = GodRayG;
    memcpy(m_fogCBMapped[fi], &cb, sizeof(cb));

    ID3D12DescriptorHeap* heaps[] = { gfx.GetSRVHeap() };
    cmd->SetDescriptorHeaps(1, heaps);

    Barrier(cmd, m_hdrRT.Get(), m_hdrState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    m_hdrState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    Barrier(cmd, depthResource, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Barrier(cmd, m_fogTex.Get(), m_fogState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    m_fogState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    // CSM shadow array is already in ALL_SHADER_RESOURCE after the RG lighting read → no barrier.
    cmd->SetComputeRootSignature(m_fogRS.Get());
    cmd->SetPipelineState(m_fogPSO.Get());
    cmd->SetComputeRootConstantBufferView(0, m_fogCB[fi]->GetGPUVirtualAddress());
    cmd->SetComputeRootDescriptorTable(1, gfx.GetSRVGPUHandle(m_hdrSRVSlot));
    cmd->SetComputeRootDescriptorTable(2, gfx.GetSRVGPUHandle(depthSRVSlot));
    cmd->SetComputeRootDescriptorTable(3, gfx.GetSRVGPUHandle(shadowSRVSlot));
    cmd->SetComputeRootDescriptorTable(4, gfx.GetSRVGPUHandle(m_fogUAVSlot));
    DispatchCompute(cmd, m_width, m_height);
    UAVBarrier(cmd, m_fogTex.Get());

    Barrier(cmd, m_fogTex.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    m_fogState = D3D12_RESOURCE_STATE_COPY_SOURCE;
    Barrier(cmd, m_hdrRT.Get(), m_hdrState, D3D12_RESOURCE_STATE_COPY_DEST);
    m_hdrState = D3D12_RESOURCE_STATE_COPY_DEST;
    cmd->CopyResource(m_hdrRT.Get(), m_fogTex.Get());
    Barrier(cmd, m_hdrRT.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);
    m_hdrState = D3D12_RESOURCE_STATE_RENDER_TARGET;

    Barrier(cmd, depthResource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
}

// ─── ExecuteFinal ─────────────────────────────────────────────────────────────

void PostProcessPass::ExecuteFinal(ID3D12GraphicsCommandList* cmd,
                                    GraphicsDevice& gfx,
                                    uint32_t frameIndex,
                                    uint32_t vpX, uint32_t vpY,
                                    uint32_t vpW, uint32_t vpH) {
    uint32_t fi = frameIndex % NUM_FRAMES_IN_FLIGHT;

    if (!m_hdrRT) {
        OutputDebugStringA("[PostProcess] ExecuteFinal skipped: hdrRT is null\n");
        return;
    }

    // Explicit .Get() comparison — ComPtr operator!=(nullptr) may not equal .Get()!=nullptr
    ID3D12Resource* bloomAPtr = m_bloomA.Get();
    ID3D12Resource* bloomBPtr = m_bloomB.Get();
    const bool bloomReady = BloomEnabled
                         && (bloomAPtr != nullptr) && (bloomBPtr != nullptr)
                         && (m_bloomCBMapped[fi] != nullptr);

    ID3D12DescriptorHeap* heaps[] = { gfx.GetSRVHeap() };
    cmd->SetDescriptorHeaps(1, heaps);

    // ── Bloom passes (skipped if bloom resources are missing) ──
    if (bloomReady) {
        uint32_t bw = (std::max)(1u, m_width / 2);
        uint32_t bh = (std::max)(1u, m_height / 2);

        struct BloomCB { float inTexW, inTexH, threshold, kawaseOffset; };
        auto bloomCBAddr = [&](uint32_t passIdx) -> D3D12_GPU_VIRTUAL_ADDRESS {
            return m_bloomCB[fi]->GetGPUVirtualAddress() + (D3D12_GPU_VIRTUAL_ADDRESS)(passIdx * 256);
        };

        // Bloom pass 0: HDR → bloomA (threshold + 2× downsample)
        Barrier(cmd, m_hdrRT.Get(), m_hdrState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        m_hdrState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        Barrier(cmd, bloomAPtr, m_bloomAState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        m_bloomAState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        BloomCB cb0 = { 1.0f / m_width, 1.0f / m_height, BloomThreshold, 0.0f };
        memcpy(m_bloomCBMapped[fi] + 0 * 256, &cb0, sizeof(cb0));

        cmd->SetComputeRootSignature(m_bloomRS.Get());
        cmd->SetPipelineState(m_bloomDownPSO.Get());
        cmd->SetComputeRootConstantBufferView(0, bloomCBAddr(0));
        cmd->SetComputeRootDescriptorTable(1, gfx.GetSRVGPUHandle(m_hdrSRVSlot));
        cmd->SetComputeRootDescriptorTable(2, gfx.GetSRVGPUHandle(m_bloomAUAVSlot));
        DispatchCompute(cmd, bw, bh);
        UAVBarrier(cmd, bloomAPtr);

        // Kawase passes: A→B, B→A, A→B
        struct KawasePassDesc {
            ID3D12Resource* src; D3D12_RESOURCE_STATES* srcState; uint32_t srcSRV;
            ID3D12Resource* dst; D3D12_RESOURCE_STATES* dstState; uint32_t dstUAV;
            float offset;
        };
        KawasePassDesc kawasePasses[3] = {
            { bloomAPtr, &m_bloomAState, m_bloomASRVSlot, bloomBPtr, &m_bloomBState, m_bloomBUAVSlot, 0.5f },
            { bloomBPtr, &m_bloomBState, m_bloomBSRVSlot, bloomAPtr, &m_bloomAState, m_bloomAUAVSlot, 1.5f },
            { bloomAPtr, &m_bloomAState, m_bloomASRVSlot, bloomBPtr, &m_bloomBState, m_bloomBUAVSlot, 2.5f },
        };
        for (uint32_t p = 0; p < 3; ++p) {
            auto& kp = kawasePasses[p];
            // Safety: if either resource became invalid, abort bloom
            if (!kp.src || !kp.dst) {
                OutputDebugStringA("[PostProcess] Kawase: null resource mid-loop, aborting bloom\n");
                break;
            }
            Barrier(cmd, kp.src, *kp.srcState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            *kp.srcState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            Barrier(cmd, kp.dst, *kp.dstState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            *kp.dstState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

            BloomCB cbK = { 2.0f / m_width, 2.0f / m_height, 0.0f, kp.offset };
            memcpy(m_bloomCBMapped[fi] + (p + 1) * 256, &cbK, sizeof(cbK));

            cmd->SetPipelineState(m_bloomUpPSO.Get());
            cmd->SetComputeRootConstantBufferView(0, bloomCBAddr(p + 1));
            cmd->SetComputeRootDescriptorTable(1, gfx.GetSRVGPUHandle(kp.srcSRV));
            cmd->SetComputeRootDescriptorTable(2, gfx.GetSRVGPUHandle(kp.dstUAV));
            DispatchCompute(cmd, bw, bh);
            UAVBarrier(cmd, kp.dst);
        }
        // bloomB = final bloom result; transition both to PSR for tonemap
        Barrier(cmd, bloomBPtr, m_bloomBState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        m_bloomBState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        Barrier(cmd, m_hdrRT.Get(), m_hdrState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        m_hdrState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    } else {
        // Bloom resources unavailable — skip bloom, transition HDR directly to PSR
        OutputDebugStringA("[PostProcess] Bloom null — tonemapping without bloom\n");
        Barrier(cmd, m_hdrRT.Get(), m_hdrState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        m_hdrState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    // ── Tonemap + FXAA → swap chain back buffer ──
    float bloomStr = bloomReady ? BloomStrength : 0.0f;
    float tonemapData[8] = {
        bloomStr, Exposure, 1.0f / m_width, 1.0f / m_height,
        FXAAEnabled ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f
    };
    memcpy(m_tonemapCBMapped[fi], tonemapData, sizeof(tonemapData));

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = gfx.GetCurrentRTV();
    cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

    uint32_t w = gfx.GetWidth(), h = gfx.GetHeight();
    D3D12_VIEWPORT viewport = { 0, 0, (float)w, (float)h, 0, 1 };
    D3D12_RECT scissor;
    if (vpW > 0 && vpH > 0)
        scissor = { (LONG)vpX, (LONG)vpY, (LONG)(vpX + vpW), (LONG)(vpY + vpH) };
    else
        scissor = { 0, 0, (LONG)w, (LONG)h };

    cmd->RSSetViewports(1, &viewport);
    cmd->RSSetScissorRects(1, &scissor);
    cmd->SetGraphicsRootSignature(m_tonemapRS.Get());
    cmd->SetPipelineState(m_tonemapPSO.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->SetGraphicsRootConstantBufferView(0, m_tonemapCB[fi]->GetGPUVirtualAddress());
    cmd->SetGraphicsRootDescriptorTable(1, gfx.GetSRVGPUHandle(m_hdrSRVSlot));
    // If bloom is unavailable bind HDR again for the bloom slot (BloomStrength=0 so it contributes nothing)
    uint32_t bloomSRVSlot = bloomReady ? m_bloomBSRVSlot : m_hdrSRVSlot;
    cmd->SetGraphicsRootDescriptorTable(2, gfx.GetSRVGPUHandle(bloomSRVSlot));
    cmd->DrawInstanced(3, 1, 0, 0);

    // Restore HDR RT to RENDER_TARGET for next frame's lighting/particle writes
    Barrier(cmd, m_hdrRT.Get(), m_hdrState, D3D12_RESOURCE_STATE_RENDER_TARGET);
    m_hdrState = D3D12_RESOURCE_STATE_RENDER_TARGET;
}

} // namespace Fujin
