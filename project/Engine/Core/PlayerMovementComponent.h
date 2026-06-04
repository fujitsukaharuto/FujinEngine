#pragma once
#include "Component.h"
#include "PropertyVisitor.h"
#include "Actor.h"
#include "TransformComponent.h"
#include "Engine/Input/Input.h"
#include "Engine/Math/Math.h"

namespace Fujin {

// Demo gameplay component showing the input system driving an actor (the engine's analog of a UE5
// C++ Pawn + movement). While in Play it reads the named axes "MoveForward"/"MoveRight" (bound to
// WASD / left stick in main.cpp) to slide on the XZ plane, and the "Jump" action (Space / gamepad A)
// to hop with simple gravity — self-contained, no physics dependency. Logic is in Update()/BeginPlay()
// (the Tick layer); MoveSpeed etc. show up in the Inspector via Reflect().
class PlayerMovementComponent : public Component {
public:
    float MoveSpeed    = 4.0f;    // world units / second
    float JumpVelocity = 5.0f;    // initial upward speed on jump
    float Gravity      = -12.0f;  // downward acceleration

    const char* GetTypeName() const override { return "PlayerMovementComponent"; }
    void ToJson(nlohmann::json& j) const override {
        j["moveSpeed"] = MoveSpeed; j["jumpVelocity"] = JumpVelocity; j["gravity"] = Gravity;
    }
    void FromJson(const nlohmann::json& j) override {
        MoveSpeed    = j.value("moveSpeed",    MoveSpeed);
        JumpVelocity = j.value("jumpVelocity", JumpVelocity);
        Gravity      = j.value("gravity",      Gravity);
    }
    void Reflect(IPropertyVisitor& v) override {
        v.Float("Move Speed",    &MoveSpeed,    0.1f, 0.0f, 50.0f);
        v.Float("Jump Velocity", &JumpVelocity, 0.1f, 0.0f, 50.0f);
        v.Float("Gravity",       &Gravity,      0.1f, -50.0f, 0.0f);
    }

    void BeginPlay() override {
        auto* tc = GetOwner()->GetComponent<TransformComponent>();
        m_groundY = tc ? tc->Position.y : 0.0f;   // remember spawn height as the "floor"
        m_vy = 0.0f;
    }

    void Update(float dt) override {
        auto* tc = GetOwner()->GetComponent<TransformComponent>();
        if (!tc) return;
        Input& in = Input::Get();

        // Horizontal movement from named axes (WASD or left stick).
        float fwd   = in.GetAxis("MoveForward");
        float right = in.GetAxis("MoveRight");
        tc->Position.x += right * MoveSpeed * dt;
        tc->Position.z += fwd   * MoveSpeed * dt;

        // Jump + gravity (named action). Grounded check against the spawn height.
        bool grounded = (tc->Position.y <= m_groundY + 1e-3f) && m_vy <= 0.0f;
        if (grounded) {
            tc->Position.y = m_groundY;
            m_vy = 0.0f;
            if (in.ActionPressed("Jump")) m_vy = JumpVelocity;
        }
        m_vy += Gravity * dt;
        tc->Position.y += m_vy * dt;
        if (tc->Position.y < m_groundY) { tc->Position.y = m_groundY; m_vy = 0.0f; }
    }

private:
    float m_groundY = 0.0f;
    float m_vy      = 0.0f;
};

inline const bool s_playerMovementRegistered = []() {
    ComponentRegistry::Get().Register("PlayerMovementComponent",
        []() { return std::make_unique<PlayerMovementComponent>(); });
    return true;
}();

} // namespace Fujin
