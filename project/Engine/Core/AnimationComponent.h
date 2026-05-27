#pragma once
#include "Component.h"
#include "Engine/Animation/AnimationTypes.h"
#include <array>
#include <string>

namespace Fujin {

class AnimationComponent : public Component {
public:
    std::string ClipName;          // "" → play the first clip found
    float       Speed    = 1.0f;
    bool        Loop     = true;
    bool        Playing  = true;

    float       TimeOffset = 0.0f;  // phase offset in seconds (shifts start point)

    // Runtime state (managed by SceneRenderer — do not set manually).
    float       Time     = 0.0f;   // elapsed playback time in seconds
    std::array<Matrix4x4, MAX_BONES> BonePalette;
    bool        PaletteReady = false;

    AnimationComponent() { BonePalette.fill(Matrix4x4::Identity); }

    const char* GetTypeName() const override { return "AnimationComponent"; }
    void ToJson(nlohmann::json& j)        const override;
    void FromJson(const nlohmann::json& j)      override;
};

} // namespace Fujin
