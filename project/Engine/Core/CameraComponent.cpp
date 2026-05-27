#include "CameraComponent.h"

namespace Fujin {

static const bool s_camRegistered = []() {
    ComponentRegistry::Get().Register("CameraComponent", []() {
        return std::make_unique<CameraComponent>();
    });
    return true;
}();

void CameraComponent::ToJson(nlohmann::json& j) const {
    j["fov"]      = FOV;
    j["nearClip"] = NearClip;
    j["farClip"]  = FarClip;
    j["isActive"] = IsActive;
}

void CameraComponent::FromJson(const nlohmann::json& j) {
    FOV      = j.value("fov",      60.0f);
    NearClip = j.value("nearClip", 0.1f);
    FarClip  = j.value("farClip",  1000.0f);
    IsActive = j.value("isActive", true);
}

} // namespace Fujin
