#include "AnimationComponent.h"

namespace Fujin {

static const bool s_animRegistered = []() {
    ComponentRegistry::Get().Register("AnimationComponent", []() {
        return std::make_unique<AnimationComponent>();
    });
    return true;
}();

void AnimationComponent::ToJson(nlohmann::json& j) const {
    j["clipName"]   = ClipName;
    j["speed"]      = Speed;
    j["timeOffset"] = TimeOffset;
    j["loop"]       = Loop;
    j["playing"]    = Playing;
}

void AnimationComponent::FromJson(const nlohmann::json& j) {
    ClipName   = j.value("clipName",   "");
    Speed      = j.value("speed",      1.0f);
    TimeOffset = j.value("timeOffset", 0.0f);
    Loop       = j.value("loop",       true);
    Playing    = j.value("playing",    true);
}

} // namespace Fujin
