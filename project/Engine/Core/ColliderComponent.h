#pragma once
#include "Component.h"
#include "Engine/Math/Math.h"

namespace Fujin {

enum class ColliderShape { Sphere, AABB, Capsule };

class ColliderComponent : public Component {
public:
    ColliderShape Shape       = ColliderShape::AABB;
    Vector3       Offset      = {};
    bool          IsTrigger   = false;

    // Sphere / Capsule radius
    float   Radius      = 0.5f;
    // AABB half-extents
    Vector3 HalfExtents = { 0.5f, 0.5f, 0.5f };
    // Capsule: half-height of the cylinder (not including end caps), axis = local Y
    float   HalfHeight  = 1.0f;

    const char* GetTypeName() const override { return "ColliderComponent"; }
    void ToJson(nlohmann::json& j) const override;
    void FromJson(const nlohmann::json& j) override;
};

} // namespace Fujin
