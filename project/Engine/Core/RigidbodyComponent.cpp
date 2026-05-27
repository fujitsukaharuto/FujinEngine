#include "RigidbodyComponent.h"
#include "Component.h"

namespace Fujin {

static const bool s_rigidbodyRegistered = []() {
    ComponentRegistry::Get().Register("RigidbodyComponent", []() {
        return std::make_unique<RigidbodyComponent>();
    });
    return true;
}();

void RigidbodyComponent::ToJson(nlohmann::json& j) const {
    j["mass"]          = Mass;
    j["restitution"]   = Restitution;
    j["friction"]      = Friction;
    j["linearDamping"] = LinearDamping;
    j["isKinematic"]   = IsKinematic;
    j["useGravity"]    = UseGravity;
}

void RigidbodyComponent::FromJson(const nlohmann::json& j) {
    if (j.contains("mass"))          Mass          = j["mass"].get<float>();
    if (j.contains("restitution"))   Restitution   = j["restitution"].get<float>();
    if (j.contains("friction"))      Friction      = j["friction"].get<float>();
    if (j.contains("linearDamping")) LinearDamping = j["linearDamping"].get<float>();
    if (j.contains("isKinematic"))   IsKinematic   = j["isKinematic"].get<bool>();
    if (j.contains("useGravity"))    UseGravity    = j["useGravity"].get<bool>();
}

} // namespace Fujin
