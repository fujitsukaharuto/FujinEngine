#include "ColliderComponent.h"
#include "Component.h"

namespace Fujin {

static const bool s_colliderRegistered = []() {
    ComponentRegistry::Get().Register("ColliderComponent", []() {
        return std::make_unique<ColliderComponent>();
    });
    return true;
}();

// ToJson/FromJson now come from Component's Reflect-driven defaults (see ColliderComponent::Reflect):
// the new Enum / EnumArray visitor methods serialize shape/channel as ints and responses as an int
// array — the exact same keys/types the hand-written serializer used, so existing scenes still load.

} // namespace Fujin
