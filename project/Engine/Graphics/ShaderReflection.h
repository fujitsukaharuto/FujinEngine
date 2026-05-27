#pragma once
#include "ShaderReflectionTypes.h"
#include <d3d12.h>    // Windows types required by dxcapi.h
#include <dxcapi.h>
#include <wrl/client.h>

namespace Fujin {

using Microsoft::WRL::ComPtr;

// Reflect the first cbuffer of a compiled DXIL shader blob.
// Skips float4x4 matrices and '_'-prefixed padding variables.
// Falls back to GBufferPassFallbackLayout() on any failure.
CBLayout ReflectShaderCB(IDxcBlob* blob);

} // namespace Fujin
