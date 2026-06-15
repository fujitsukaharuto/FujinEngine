#include "TransformComponent.h"
#include "Actor.h"
#include "AnimationComponent.h"

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
    if (!AttachSocket.empty()) j["attachSocket"] = AttachSocket;
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
    AttachSocket = j.value("attachSocket", AttachSocket);
}

Transform TransformComponent::GetWorldTransform() const {
    Transform local{ Position, Rotation, Scale };
    Actor* owner  = GetOwner();
    Actor* parent = owner ? owner->GetParent() : nullptr;
    if (!parent) return local;

    auto* parentTransform = parent->GetComponent<TransformComponent>();
    Transform parentWorld = parentTransform ? parentTransform->GetWorldTransform() : Transform{};

    // Bone socket: ride a named bone on the parent's skeletal mesh. The cached bone global is mesh-space
    // (pre actor scale), so composing with the parent's world matches how the mesh itself is skinned.
    // Falls back to plain parent-root attachment when the socket can't be resolved (zero regression).
    if (!AttachSocket.empty()) {
        if (auto* anim = parent->GetComponent<AnimationComponent>()) {
            Matrix4x4 boneModel;
            if (anim->TryGetSocketModelTransform(AttachSocket, boneModel))
                return parentWorld * Transform::FromMatrix(boneModel) * local;
        }
    }
    return parentWorld * local;   // world = parent * local
}

Matrix4x4 TransformComponent::GetWorldMatrix() const {
    return GetWorldTransform().ToMatrix();
}

} // namespace Fujin
