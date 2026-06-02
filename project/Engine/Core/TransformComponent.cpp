#include "TransformComponent.h"
#include "Actor.h"

namespace Fujin {

static const bool s_transformRegistered = []() {
    ComponentRegistry::Get().Register("TransformComponent", []() {
        return std::make_unique<TransformComponent>();
    });
    return true;
}();

void TransformComponent::ToJson(nlohmann::json& j) const {
    j["position"] = { Position.x, Position.y, Position.z };
    j["rotation"] = { Rotation.x, Rotation.y, Rotation.z, Rotation.w };
    j["scale"]    = { Scale.x,    Scale.y,    Scale.z };
}

void TransformComponent::FromJson(const nlohmann::json& j) {
    if (j.contains("position")) {
        auto& p = j["position"];
        Position = { p[0].get<float>(), p[1].get<float>(), p[2].get<float>() };
    }
    if (j.contains("rotation")) {
        auto& r = j["rotation"];
        Rotation = { r[0].get<float>(), r[1].get<float>(), r[2].get<float>(), r[3].get<float>() };
    }
    if (j.contains("scale")) {
        auto& s = j["scale"];
        Scale = { s[0].get<float>(), s[1].get<float>(), s[2].get<float>() };
    }
}

Transform TransformComponent::GetWorldTransform() const {
    Transform local{ Position, Rotation, Scale };
    Actor* owner = GetOwner();
    if (owner && owner->GetParent()) {
        if (auto* parentTransform = owner->GetParent()->GetComponent<TransformComponent>())
            return parentTransform->GetWorldTransform() * local;   // world = parent * local
    }
    return local;
}

Matrix4x4 TransformComponent::GetWorldMatrix() const {
    return GetWorldTransform().ToMatrix();
}

} // namespace Fujin
