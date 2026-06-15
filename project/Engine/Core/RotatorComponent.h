#pragma once
#include "Component.h"
#include "PropertyVisitor.h"
#include "Actor.h"
#include "TransformComponent.h"
#include "Engine/Math/Math.h"

namespace Fujin {

// Minimal gameplay component demonstrating the Tick layer: while the editor is in Play it spins
// its owner about the local Y axis at DegreesPerSecond. Logic lives in C++ (Update); this is the
// engine's analog of a UE5 C++ ActorComponent (no Blueprint/visual scripting). It can be added in
// code (see SetupTestScene) or from the editor's Add Component menu ("Rotator (Script)"), and its
// DegreesPerSecond is editable in the Inspector — the UE5 "C++ component + Details" pattern.
class RotatorComponent : public Component {
public:
    float DegreesPerSecond = 90.0f;   // spin speed about local +Y

    const char* GetTypeName() const override { return "RotatorComponent"; }

    // Single source of truth: declaring DegreesPerSecond here drives both the Inspector and save/load
    // (Component's default ToJson/FromJson route through Reflect) — no DrawXxx, no ToJson/FromJson.
    void Reflect(IPropertyVisitor& v) override {
        v.Float("degreesPerSecond", "Degrees / Second", &DegreesPerSecond, 1.0f, -1440.0f, 1440.0f);
    }

    void Update(float dt) override {
        auto* tc = GetOwner()->GetComponent<TransformComponent>();
        if (!tc) return;
        Quaternion spin = Quaternion::FromAxisAngle(Vector3(0.0f, 1.0f, 0.0f),
                                                    Math::ToRadians(DegreesPerSecond) * dt);
        tc->Rotation = tc->Rotation * spin;   // compose in local space
        tc->Rotation.Normalize();
    }
};

// Register for serialization so a Rotator added via the editor round-trips through save/load.
// inline (C++17) ⇒ one definition across all TUs that include this header, initialised once.
inline const bool s_rotatorRegistered = []() {
    ComponentRegistry::Get().Register("RotatorComponent",
        []() { return std::make_unique<RotatorComponent>(); });
    return true;
}();

} // namespace Fujin
