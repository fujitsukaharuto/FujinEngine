#include "CameraComponent.h"

namespace Fujin {

static const bool s_camRegistered = []() {
    ComponentRegistry::Get().Register("CameraComponent", []() {
        return std::make_unique<CameraComponent>();
    });
    return true;
}();

// ToJson/FromJson now come from Component's Reflect-driven defaults (see CameraComponent::Reflect).

} // namespace Fujin
