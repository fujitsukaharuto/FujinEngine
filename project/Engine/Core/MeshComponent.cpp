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
    j["alphaClip"]    = AlphaClip;
}

void MeshComponent::FromJson(const nlohmann::json& j) {
    MeshPath     = j.value("meshPath",     "");
    MaterialPath = j.value("materialPath", "");
    DoubleSided  = j.value("doubleSided",  false);
    AlphaClip    = j.value("alphaClip",    false);
}

} // namespace Fujin
