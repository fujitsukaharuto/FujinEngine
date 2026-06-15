#pragma once
#include "Component.h"
#include "Engine/Math/Math.h"
#include <string>

namespace Fujin {

class TransformComponent : public Component {
public:
    Vector3    Position = Vector3(0.0f, 0.0f, 0.0f);   // local (relative to parent — or to the socket)
    Quaternion Rotation;                               // local
    Vector3    Scale    = Vector3(1.0f, 1.0f, 1.0f);   // local

    // Bone socket attachment (UE5-style). When non-empty AND the parent actor has an AnimationComponent
    // with this bone, the local transform above is relative to that posed bone instead of the parent's
    // root, so the actor rides the bone (weapon-in-hand). Empty ⇒ plain parent-root attachment.
    std::string AttachSocket;

    // World transform snapshot, refreshed by SceneManager::UpdateWorldTransforms() each frame.
    // Hot paths (physics, rendering) read this instead of recursing the parent chain.
    Transform  CachedWorld;

    const char* GetTypeName() const override { return "TransformComponent"; }
    void ToJson(nlohmann::json& j) const override;
    void FromJson(const nlohmann::json& j) override;

    // Authoritative world transform: composes the parent chain (local → world).
    Transform GetWorldTransform() const;
    Matrix4x4 GetWorldMatrix() const;
};

} // namespace Fujin
