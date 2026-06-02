#pragma once
#include "Component.h"
#include <string>

namespace Fujin {

enum class MeshBlendMode : uint8_t {
    Opaque      = 0,
    AlphaClip   = 1,
    Translucent = 2,
};

class MeshComponent : public Component {
public:
    std::string   MeshPath;
    std::string   MaterialPath;
    bool          DoubleSided = false;
    MeshBlendMode Blend       = MeshBlendMode::Opaque;
    float         Opacity     = 1.0f;
    bool          CastShadow  = true;   // UE5-style per-primitive shadow toggle

    const char* GetTypeName() const override { return "MeshComponent"; }
    void ToJson(nlohmann::json& j) const override;
    void FromJson(const nlohmann::json& j) override;
};

} // namespace Fujin
