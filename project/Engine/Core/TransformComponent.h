#pragma once
#include "Component.h"
#include "Engine/Math/Math.h"

namespace Fujin {

class TransformComponent : public Component {
public:
    Vector3    Position = Vector3(0.0f, 0.0f, 0.0f);
    Quaternion Rotation;
    Vector3    Scale    = Vector3(1.0f, 1.0f, 1.0f);

    const char* GetTypeName() const override { return "TransformComponent"; }
    void ToJson(nlohmann::json& j) const override;
    void FromJson(const nlohmann::json& j) override;

    Matrix4x4 GetWorldMatrix() const;
};

} // namespace Fujin
