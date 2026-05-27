#include "ShaderReflection.h"  // pulls in ShaderReflectionTypes.h + dxcapi.h
#include <d3d12shader.h>
#include <algorithm>
#include <cctype>
#include <string>

namespace Fujin {

namespace {

ParamWidget GuessWidget(const std::string& name, uint32_t cols) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });

    bool isColor = lower.find("color")    != std::string::npos ||
                   lower.find("colour")   != std::string::npos ||
                   lower.find("albedo")   != std::string::npos ||
                   lower.find("emissive") != std::string::npos ||
                   lower.find("tint")     != std::string::npos;

    bool isRange01 = lower.find("metallic")  != std::string::npos ||
                     lower.find("roughness") != std::string::npos ||
                     lower == "ao"                                 ||
                     lower.find("alpha")     != std::string::npos ||
                     lower.find("opacity")   != std::string::npos ||
                     lower.find("factor")    != std::string::npos;

    if (isColor) {
        if (cols == 4) return ParamWidget::Color4;
        if (cols == 3) return ParamWidget::Color3;
    }
    if (cols == 1) return isRange01 ? ParamWidget::Slider01 : ParamWidget::DragFloat;
    if (cols == 2) return ParamWidget::DragFloat2;
    if (cols == 3) return ParamWidget::DragFloat3;
    return ParamWidget::DragFloat4;
}

} // anonymous namespace

CBLayout ReflectShaderCB(IDxcBlob* blob) {
    if (!blob) return GBufferPassFallbackLayout();

    ComPtr<IDxcUtils> utils;
    if (FAILED(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils))))
        return GBufferPassFallbackLayout();

    DxcBuffer buf = {};
    buf.Ptr      = blob->GetBufferPointer();
    buf.Size     = blob->GetBufferSize();
    buf.Encoding = 0;

    ComPtr<ID3D12ShaderReflection> refl;
    if (FAILED(utils->CreateReflection(&buf, IID_PPV_ARGS(&refl))))
        return GBufferPassFallbackLayout();

    D3D12_SHADER_DESC shaderDesc = {};
    if (FAILED(refl->GetDesc(&shaderDesc)) || shaderDesc.ConstantBuffers == 0)
        return GBufferPassFallbackLayout();

    // First cbuffer is the per-object/material one.
    ID3D12ShaderReflectionConstantBuffer* cb = refl->GetConstantBufferByIndex(0);
    D3D12_SHADER_BUFFER_DESC cbDesc = {};
    if (FAILED(cb->GetDesc(&cbDesc))) return GBufferPassFallbackLayout();

    CBLayout layout = {};
    uint32_t materialOffset = UINT32_MAX;

    for (UINT i = 0; i < cbDesc.Variables; ++i) {
        ID3D12ShaderReflectionVariable* var = cb->GetVariableByIndex(i);
        D3D12_SHADER_VARIABLE_DESC vd = {};
        if (FAILED(var->GetDesc(&vd))) continue;

        // Skip padding variables (name starts with '_').
        if (vd.Name && vd.Name[0] == '_') continue;

        ID3D12ShaderReflectionType* type = var->GetType();
        D3D12_SHADER_TYPE_DESC td = {};
        if (FAILED(type->GetDesc(&td))) continue;

        // Skip matrices.
        if (td.Class == D3D_SVC_MATRIX_ROWS || td.Class == D3D_SVC_MATRIX_COLUMNS) continue;

        // Record the offset of the first material variable.
        if (materialOffset == UINT32_MAX) materialOffset = vd.StartOffset;

        uint32_t cols = td.Columns;
        if (cols == 0 || cols > 4) continue;

        ShaderParam p;
        p.Name       = vd.Name;
        p.ByteOffset = vd.StartOffset - materialOffset;
        p.Cols       = cols;
        p.Widget     = GuessWidget(vd.Name, cols);
        layout.Params.push_back(std::move(p));
    }

    if (materialOffset == UINT32_MAX || layout.Params.empty())
        return GBufferPassFallbackLayout();

    layout.MaterialOffset = materialOffset;
    layout.MaterialSize   = cbDesc.Size - materialOffset;
    return layout;
}

CBLayout GBufferPassFallbackLayout() {
    CBLayout layout;
    layout.MaterialOffset = 128;
    layout.MaterialSize   = 32;
    layout.Params = {
        { "AlbedoColor", 0,  3, ParamWidget::Color3   },
        { "Metallic",    12, 1, ParamWidget::Slider01  },
        { "Roughness",   16, 1, ParamWidget::Slider01  },
        { "AO",          20, 1, ParamWidget::Slider01  },
    };
    return layout;
}

} // namespace Fujin
