#pragma once
#include "Component.h"
#include "PropertyVisitor.h"
#include "Engine/Math/Math.h"

namespace Fujin {

class RigidbodyComponent : public Component {
public:
    // Properties
    float Mass           = 1.0f;
    float Restitution    = 0.3f;
    float Friction       = 0.5f;
    float LinearDamping  = 0.05f;
    float AngularDamping = 0.05f;
    bool  IsKinematic    = false;
    bool  UseGravity     = true;

    // Phase 2: sleep state
    bool  IsSleeping  = false;
    float SleepTimer  = 0.0f;

    // Runtime state
    Vector3 Velocity        = {};
    Vector3 AngularVelocity = {};

    // Inverse inertia tensor diagonal in local space.
    // Set each frame by PhysicsWorld::ComputeInertia from the collider shape.
    // Zero for kinematic bodies.
    Vector3 InvInertiaLocal = { 1.0f, 1.0f, 1.0f };

    void WakeUp() { IsSleeping = false; SleepTimer = 0.0f; }

    void AddForce(const Vector3& f)   { m_forceAccum  += f; }
    void AddTorque(const Vector3& t)  { m_torqueAccum += t; }
    void AddImpulse(const Vector3& j) {
        if (!IsKinematic && Mass > 0.0f) Velocity += j * InvMass();
    }
    void ClearAccumulators() { m_forceAccum = {}; m_torqueAccum = {}; }

    float InvMass() const {
        return (IsKinematic || Mass <= 0.0f) ? 0.0f : 1.0f / Mass;
    }

    // Apply the world-space inverse inertia tensor to v.
    // I^{-1}_{world}(v) = R * (InvInertiaLocal ⊙ (R^T * v))
    Vector3 ApplyInvInertiaWorld(const Quaternion& rot, const Vector3& v) const;

    const char* GetTypeName() const override { return "RigidbodyComponent"; }
    // Reflect drives Inspector + save/load. Keys verbatim from the old serializer. Runtime state
    // (Velocity / AngularVelocity / sleep / inertia / accumulators) is intentionally not reflected.
    void Reflect(IPropertyVisitor& v) override {
        v.Float("mass",           "Mass",            &Mass,           0.01f,  0.001f, 10000.0f);
        v.Float("restitution",    "Restitution",     &Restitution,    0.01f,  0.0f,   1.0f);
        v.Float("friction",       "Friction",        &Friction,       0.01f,  0.0f,   1.0f);
        v.Float("linearDamping",  "Linear Damping",  &LinearDamping,  0.001f, 0.0f,   1.0f);
        v.Float("angularDamping", "Angular Damping", &AngularDamping, 0.001f, 0.0f,   1.0f);
        v.Bool ("isKinematic",    "Kinematic",       &IsKinematic);
        v.Bool ("useGravity",     "Gravity",         &UseGravity);
    }

    Vector3 m_forceAccum  = {};
    Vector3 m_torqueAccum = {};
};

} // namespace Fujin
