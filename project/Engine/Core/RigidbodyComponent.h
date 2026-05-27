#pragma once
#include "Component.h"
#include "Engine/Math/Math.h"

namespace Fujin {

class RigidbodyComponent : public Component {
public:
    // Properties
    float Mass          = 1.0f;
    float Restitution   = 0.3f;   // [0..1]
    float Friction      = 0.5f;   // [0..1]
    float LinearDamping = 0.05f;  // velocity decay per second
    bool  IsKinematic   = false;
    bool  UseGravity    = true;

    // State (written by PhysicsWorld)
    Vector3 Velocity        = {};
    Vector3 AngularVelocity = {};

    void AddForce(const Vector3& f)    { m_forceAccum += f; }
    void AddImpulse(const Vector3& j)  { if (!IsKinematic && Mass > 0) Velocity += j * (1.0f / Mass); }
    void ClearAccumulators()           { m_forceAccum = {}; m_torqueAccum = {}; }

    const char* GetTypeName() const override { return "RigidbodyComponent"; }
    void ToJson(nlohmann::json& j) const override;
    void FromJson(const nlohmann::json& j) override;

    // Accessible to PhysicsWorld
    Vector3 m_forceAccum  = {};
    Vector3 m_torqueAccum = {};
};

} // namespace Fujin
