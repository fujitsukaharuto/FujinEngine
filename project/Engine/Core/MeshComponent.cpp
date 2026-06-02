#include "MeshComponent.h"

namespace Fujin {

static const bool s_meshRegistered = []() {
    ComponentRegistry::Get().Register("MeshComponent", []() {
        return std::make_unique<MeshComponent>();
    });
    return true;
}();

void MeshComponent::ToJson(nlohmann::json& j) const {
    j["meshPath"]     = MeshPath;
    j["materialPath"] = MaterialPath;
    j["doubleSided"]  = DoubleSided;
    j["blendMode"]    = static_cast<int>(Blend);
    j["opacity"]      = Opacity;
    j["castShadow"]   = CastShadow;
}

void MeshComponent::FromJson(const nlohmann::json& j) {
    MeshPath     = j.value("meshPath",     "");
    MaterialPath = j.value("materialPath", "");
    DoubleSided  = j.value("doubleSided",  false);
    Opacity      = j.value("opacity",      1.0f);
    CastShadow   = j.value("castShadow",   true);
    // Backward compat: old scenes use "alphaClip" bool
    if (j.contains("blendMode")) {
        Blend = static_cast<MeshBlendMode>(j["blendMode"].get<int>());
    } else {
        Blend = j.value("alphaClip", false) ? MeshBlendMode::AlphaClip : MeshBlendMode::Opaque;
    }
}

} // namespace Fujin
