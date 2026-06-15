#pragma once
#include "Component.h"
#include "PropertyVisitor.h"
#include "Actor.h"
#include "SceneManager.h"
#include "TransformComponent.h"
#include "AnimationComponent.h"
#include "PawnComponent.h"
#include "Engine/Input/Input.h"
#include "Engine/Physics/PhysicsWorld.h"
#include "Engine/Math/Math.h"
#include <cmath>

namespace Fujin {

// Character movement (the engine's analog of a UE5 CharacterMovementComponent, simplified).
// While in Play it reads the named axes "MoveForward"/"MoveRight" (WASD / left stick) to walk the
// owner on the XZ plane, accelerating/decelerating toward MaxSpeed, turns the body to face the
// movement direction, and applies gravity + grounding. The current horizontal speed is published
// to the owner's AnimationComponent as the "Speed" param each frame, which drives a 1D blend space
// or state machine (Survey → Walk → Run). v1 is physics-independent (spawn-height floor, like
// PlayerMovementComponent) — reaching PhysicsWorld from the Tick layer is a separate task.
class CharacterMovementComponent : public Component {
public:
    float WalkSpeed    = 1.5f;    // default target speed (no sprint) — matches the Walk blend sample
    float MaxSpeed     = 4.0f;    // sprint target speed while the "Sprint" action is held — Run sample
    float Acceleration = 16.0f;   // how fast speed ramps toward the target (units/s^2)
    float TurnRate     = 12.0f;   // body yaw slerp rate toward the movement direction (1/s)
    float YawOffset    = 0.0f;    // degrees added to facing (correct a model that faces != +Z)
    float Gravity      = -12.0f;  // downward acceleration
    float JumpVelocity = 5.0f;    // initial upward speed on the "Jump" action
    float MaxStepHeight = 0.4f;   // tallest step/ledge climbable WITHOUT jumping; taller = blocked wall
    float StepSmoothness = 12.0f; // how fast the visual height eases to a new step (1/s); 0 = instant snap

    const char* GetTypeName() const override { return "CharacterMovementComponent"; }
    // Reflect declares every field once (key + label); Component's default ToJson/FromJson drive
    // save/load from it. JSON keys are kept verbatim so existing scenes/saves stay compatible.
    void Reflect(IPropertyVisitor& v) override {
        v.Float("walkSpeed",      "Walk Speed",      &WalkSpeed,    0.1f,  0.0f,  50.0f);
        v.Float("maxSpeed",       "Run Speed",       &MaxSpeed,     0.1f,  0.0f,  50.0f);
        v.Float("acceleration",   "Acceleration",    &Acceleration, 0.5f,  0.0f, 200.0f);
        v.Float("turnRate",       "Turn Rate",       &TurnRate,     0.5f,  0.0f,  60.0f);
        v.Float("yawOffset",      "Yaw Offset",      &YawOffset,    1.0f, -180.0f, 180.0f);
        v.Float("gravity",        "Gravity",         &Gravity,       0.1f, -50.0f,  0.0f);
        v.Float("jumpVelocity",   "Jump Velocity",   &JumpVelocity,  0.1f,  0.0f,  50.0f);
        v.Float("maxStepHeight",  "Max Step Height", &MaxStepHeight,  0.05f, 0.0f,  5.0f);
        v.Float("stepSmoothness", "Step Smoothness", &StepSmoothness, 0.5f,  0.0f, 60.0f);
    }

    void BeginPlay() override {
        auto* tc = GetOwner()->GetComponent<TransformComponent>();
        const float y = tc ? tc->Position.y : 0.0f;   // spawn height = the "floor"
        m_groundY = y;
        m_footY   = y;   // logical foot height (snaps instantly when stepping)
        m_visualY = y;   // rendered height (eases toward m_footY for smooth steps)
        m_vy      = 0.0f;
        m_speed   = 0.0f;
    }

    void Update(float dt) override {
        auto* tc = GetOwner()->GetComponent<TransformComponent>();
        if (!tc) return;
        Input& in = Input::Get();

        // Movement intent comes from the possessing controller via a PawnComponent (UE5 controller →
        // pawn → movement). When there is no possessed pawn we fall back to reading Input directly,
        // preserving the pre-framework behaviour exactly (zero regression for pawn-less actors).
        Vector3 dir;
        bool sprintHeld, jumpPressed;
        if (PawnComponent* pawn = GetOwner()->GetComponent<PawnComponent>(); pawn && pawn->IsPossessed()) {
            dir         = pawn->GetMovementInput();   // world XZ, already clamped to unit length
            sprintHeld  = pawn->WantsSprint();
            jumpPressed = pawn->WantsJump();
        } else {
            dir         = Vector3(in.GetAxis("MoveRight"), 0.0f, in.GetAxis("MoveForward"));
            sprintHeld  = in.ActionHeld("Sprint");
            jumpPressed = in.ActionPressed("Jump");
        }

        float inputMag = dir.Length();
        if (inputMag > 1.0f) { dir = dir * (1.0f / inputMag); inputMag = 1.0f; }
        else if (inputMag > 1e-3f) { dir = dir * (1.0f / inputMag); }

        // Hold "Sprint" (Shift) to run; otherwise walk. The target speed picks Walk vs Run so the
        // blend space lands on the matching clip (WalkSpeed = Walk sample, MaxSpeed = Run sample).
        const float topSpeed = sprintHeld ? MaxSpeed : WalkSpeed;
        // Accelerate / decelerate the scalar speed toward the input-scaled target.
        float target = inputMag * topSpeed;
        if (m_speed < target) m_speed = (std::min)(target, m_speed + Acceleration * dt);
        else                  m_speed = (std::max)(target, m_speed - Acceleration * dt);

        // Move and turn the body to face the movement direction (only when there is input). Remember
        // the pre-move XZ so we can cancel the step if it turns out to be a too-tall wall (below).
        const float prevX = tc->Position.x, prevZ = tc->Position.z;
        if (inputMag > 1e-3f) {
            tc->Position.x += dir.x * m_speed * dt;
            tc->Position.z += dir.z * m_speed * dt;

            float targetYaw = std::atan2(dir.x, dir.z) + Math::ToRadians(YawOffset);
            Quaternion want = Quaternion::FromAxisAngle(Vector3(0.0f, 1.0f, 0.0f), targetYaw);
            float t = TurnRate * dt; if (t > 1.0f) t = 1.0f;
            tc->Rotation = Quaternion::Slerp(tc->Rotation, want, t);
            tc->Rotation.Normalize();
        }

        // ── Ground following: find the floor under the (new) feet with a downward ray against world
        // geometry. m_footY is the LOGICAL foot height (commits instantly so the next step is measured
        // from the step we just took, never blocking mid-climb); the rendered height m_visualY eases
        // toward it so steps look smooth. Falls back to the spawn height when there's no physics world
        // or nothing underfoot, preserving the old flat-floor behaviour. ──
        float groundY = m_groundY;     // spawn-height fallback
        if (auto* scene = GetOwner()->GetScene()) {
            if (PhysicsWorld* phys = scene->GetPhysicsWorld()) {
                RayHit hit;
                const float up   = MaxStepHeight + 0.05f;                 // start the ray above the feet
                const float span = up + MaxStepHeight + 0.05f;            // ...and reach below them
                Vector3 origin(tc->Position.x, m_footY + up, tc->Position.z);
                if (phys->Raycast(origin, Vector3(0.0f, -1.0f, 0.0f), span, hit, GetOwner()))
                    groundY = hit.Point.y;
            }
        }

        const float delta = groundY - m_footY;   // + ground above feet (step up), - below (descend/air)
        if (jumpPressed && m_vy <= 0.0f && delta <= MaxStepHeight && delta >= -MaxStepHeight)
            m_vy = JumpVelocity;                  // jump from a grounded stance

        bool airborne = false;
        if (m_vy > 0.0f) {
            // Rising (jump): integrate up; land when we sink back to within step range of the ground.
            m_vy += Gravity * dt;
            m_footY += m_vy * dt;
            if (m_vy <= 0.0f && m_footY <= groundY + MaxStepHeight) { m_footY = groundY; m_vy = 0.0f; }
            else airborne = true;
        } else if (delta > MaxStepHeight) {
            // Ground ahead rises more than MaxStepHeight = a wall / too-tall step: block the move so the
            // character can't climb it without jumping. Cancel this frame's horizontal advance.
            tc->Position.x = prevX;
            tc->Position.z = prevZ;
        } else if (delta >= -MaxStepHeight) {
            // Within step range below/above → commit to standing on the ground (step / slope, instant).
            m_footY = groundY;
            m_vy    = 0.0f;
        } else {
            // Ground is far below (walked off a ledge): fall under gravity until it catches up.
            m_vy += Gravity * dt;
            m_footY += m_vy * dt;
            if (m_footY < groundY) { m_footY = groundY; m_vy = 0.0f; }
            else airborne = true;
        }

        // Smoothly ease the rendered height toward the logical foot height for grounded step changes;
        // while airborne, track it exactly so jumps / falls stay responsive (no rubber-banding).
        if (airborne || StepSmoothness <= 0.0f) {
            m_visualY = m_footY;
        } else {
            float t = 1.0f - std::exp(-StepSmoothness * dt);   // framerate-independent ease
            m_visualY += (m_footY - m_visualY) * t;
            if (std::fabs(m_footY - m_visualY) < 1e-4f) m_visualY = m_footY;
        }
        tc->Position.y = m_visualY;

        // Publish speed to the animation graph (drives Survey → Walk → Run).
        if (auto* anim = GetOwner()->GetComponent<AnimationComponent>())
            anim->SetParam("Speed", m_speed);
    }

private:
    float m_groundY = 0.0f;   // spawn height (fallback floor when no geometry underfoot)
    float m_footY   = 0.0f;   // logical foot height — commits instantly on step/slope
    float m_visualY = 0.0f;   // rendered height — eases toward m_footY for smooth steps
    float m_vy      = 0.0f;
    float m_speed   = 0.0f;
};

inline const bool s_characterMovementRegistered = []() {
    ComponentRegistry::Get().Register("CharacterMovementComponent",
        []() { return std::make_unique<CharacterMovementComponent>(); });
    return true;
}();

} // namespace Fujin
