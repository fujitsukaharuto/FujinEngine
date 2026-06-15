#pragma once
#include "Component.h"
#include "PropertyVisitor.h"
#include "Actor.h"
#include "Engine/Math/Math.h"
#include <cstdint>

namespace Fujin {

// PawnComponent — the engine's analog of a UE5 APawn (as a component role, since Actor here is a
// non-subclassable ECS container: behaviour lives in components, not a class hierarchy). A Pawn is
// the thing a controller possesses and drives. It does NOT read input itself; instead a
// PlayerControllerComponent (or AI) feeds it each frame via AddMovementInput / AddControllerYawInput
// (UE5 APawn::AddMovementInput), and a movement component (e.g. CharacterMovementComponent) consumes
// the result via GetMovementInput / WantsJump / WantsSprint.
//
// Input is DOUBLE-BUFFERED: contributions accumulate into a "next" buffer during a frame, and the
// pawn promotes next→current in its own Update(). Reads come from "current". This makes the
// producer (controller) and consumer (movement) order-independent across the tick pass — whatever
// the per-actor tick order, the pawn sees a stable, exactly-one-frame-latent input. Without this,
// a controller spawned after the pawn (the GameMode case) would feed input the consumer already
// read this frame.
class PawnComponent : public Component {
public:
    // When true, the auth GameMode auto-possesses this pawn with player 0 at BeginPlay (UE5
    // APawn::AutoPossessPlayer = Player0). Exactly one pawn per scene should set this for the demo.
    bool AutoPossessPlayer = false;

    const char* GetTypeName() const override { return "PawnComponent"; }
    // Reflect is the single source of truth (drives Inspector + default ToJson/FromJson).
    void Reflect(IPropertyVisitor& v) override {
        v.Bool("autoPossessPlayer", "Auto Possess Player", &AutoPossessPlayer);
    }

    // ── Possession (controller ⇄ pawn, by actor id to stay dangle-safe) ──────────────────────────
    void     Possess(uint64_t controllerActorId) { m_controllerId = controllerActorId; }
    void     UnPossess() {
        m_controllerId = 0;
        // Drop any in-flight input so a freshly-unpossessed pawn doesn't coast on stale commands.
        m_inputNext = Vector3(0, 0, 0); m_yawNext = 0.0f; m_jumpNext = false; m_sprintNext = false;
        m_moveInput = Vector3(0, 0, 0); m_yawInput = 0.0f; m_jump = false; m_sprint = false;
    }
    bool     IsPossessed()  const { return m_controllerId != 0; }
    uint64_t ControllerId() const { return m_controllerId; }

    // ── Input producers (called by the possessing controller each frame) ─────────────────────────
    // UE5 APawn::AddMovementInput — accumulate a world-space direction × scale for THIS frame.
    void AddMovementInput(const Vector3& worldDir, float scale) { m_inputNext += worldDir * scale; }
    void AddControllerYawInput(float deg) { m_yawNext += deg; }   // control-rotation yaw delta (deg)
    void SetJumpInput(bool wants)   { if (wants) m_jumpNext = true; }   // edge: latched until swap
    void SetSprintInput(bool wants) { m_sprintNext = wants; }

    // ── Input consumers (called by the movement component) ───────────────────────────────────────
    // Current-frame world move direction, clamped to unit length (magnitude carries throttle).
    Vector3 GetMovementInput() const {
        Vector3 d = m_moveInput;
        float len = d.Length();
        if (len > 1.0f) d = d * (1.0f / len);
        return d;
    }
    float GetYawInput()   const { return m_yawInput; }
    bool  WantsJump()     const { return m_jump; }
    bool  WantsSprint()   const { return m_sprint; }

    // Promote next→current and clear next, once per frame. Order-independent w.r.t. producers/consumers.
    void Update(float /*dt*/) override {
        m_moveInput = m_inputNext;  m_inputNext = Vector3(0, 0, 0);
        m_yawInput  = m_yawNext;    m_yawNext   = 0.0f;
        m_jump      = m_jumpNext;   m_jumpNext  = false;
        m_sprint    = m_sprintNext; m_sprintNext = false;
    }

    void EndPlay() override { UnPossess(); }

private:
    uint64_t m_controllerId = 0;   // possessing controller actor id (0 = unpossessed)

    // "next" = accumulated this frame by the controller; "current" = read by movement (last swap).
    Vector3 m_inputNext{ 0, 0, 0 }, m_moveInput{ 0, 0, 0 };
    float   m_yawNext = 0.0f,       m_yawInput = 0.0f;
    bool    m_jumpNext = false,     m_jump = false;
    bool    m_sprintNext = false,   m_sprint = false;
};

inline const bool s_pawnRegistered = []() {
    ComponentRegistry::Get().Register("PawnComponent",
        []() { return std::make_unique<PawnComponent>(); });
    return true;
}();

} // namespace Fujin
