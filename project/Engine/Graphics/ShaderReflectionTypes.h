#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace Fujin {

// Suggested ImGui widget for a shader variable.
enum class ParamWidget {
    DragFloat,
    Slider01,    // float in [0, 1]
    DragFloat2,
    DragFloat3,
    DragFloat4,
    Color3,      // float3 as RGB
    Color4,      // float4 as RGBA
};

struct ShaderParam {
    std::string Name;
    uint32_t    ByteOffset; // byte offset within Material::ParamData
    uint32_t    Cols;       // number of float components (1–4)
    ParamWidget Widget;
};

struct CBLayout {
    uint32_t                 MaterialOffset; // byte offset in GPU cbuffer where mat params start
    uint32_t                 MaterialSize;   // byte count of the material param region
    std::vector<ShaderParam> Params;         // editable params only
};

// Hardcoded fallback layout matching GBufferPass.PS.hlsl.
CBLayout GBufferPassFallbackLayout();

} // namespace Fujin
