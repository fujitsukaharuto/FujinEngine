#pragma once
#include "Component.h"
#include "PropertyVisitor.h"
#include "Actor.h"
#include "SceneManager.h"
#include "PawnComponent.h"
#include "TransformComponent.h"
#include "Engine/Input/Input.h"
#include "Engine/Math/Math.h"
#include <cstdint>
#include <cmath>

namespace Fujin {

// PlayerControllerComponent — the engine's analog of a UE5 APlayerController. It lives on its own
// actor (the GameMode spawns one at BeginPlay), reads the player's named input each frame, and
// forwards it to the Pawn it currently possesses. The Pawn (and its movement component) never read
// Input directly, so the same Pawn can be driven by a player here or by an AI controller later —
// the UE5 "controller possesses pawn" separation.
//
// Possession is stored as the pawn's ACTOR id (not a pointer), so destroying the pawn can't dangle
// the controller; we re-resolve it each frame and silently no-op if it's gone.
class PlayerControllerComponent : public Component {
public:
    // Named axes/actions queried each frame (bound in main.cpp). World XZ: forward → +Z, right → +X.
    std::string MoveForwardAxis = "MoveForward";
    std::string MoveRightAxis   = "MoveRight";
    std::string SprintAction    = "Sprint";
    std::string JumpAction      = "Jump";

    // ── Third-person view (UE5 ControlRotation + spring-arm camera) ──────────────────────────────
    // When on, this controller owns a ControlRotation (yaw/pitch) driven by mouse + right stick, makes
    // WASD camera-relative, and produces a spring-arm follow camera (RefreshCamera) that overrides the
    // editor's free camera during Play. Default OFF so a controller alone changes nothing; the auth
    // GameMode flips it on per its ThirdPersonCamera flag when it spawns the controller.
    bool  UseCameraControl  = false;
    bool  RequireMouseHold  = true;    // mouse-look only while RMB held (avoids fighting the editor UI)
    float MouseSensitivity  = 0.15f;   // degrees of look per pixel of mouse motion
    float PadLookSpeed      = 150.0f;  // degrees/sec at full right-stick deflection
    float InitialPitch      = 12.0f;   // starting downward tilt (deg) when possession begins
    float MinPitch          = -35.0f;  // clamp: most you can look up   (deg)
    float MaxPitch          = 70.0f;   // clamp: most you can look down (deg)
    float CameraDistance    = 6.0f;    // spring-arm length behind the pawn (units)
    float CameraHeight      = 1.5f;    // pivot height above the pawn origin (units)
    float CameraLag         = 12.0f;   // position smoothing toward the ideal spot (1/s); 0 = instant

    const char* GetTypeName() const override { return "PlayerControllerComponent"; }
    // Reflect is the single source of truth: it drives the Inspector and (via Component's default
    // ToJson/FromJson) save/load. JSON keys kept verbatim for save-file compatibility.
    void Reflect(IPropertyVisitor& v) override {
        v.Text("moveForwardAxis",  "Move Forward Axis",  &MoveForwardAxis);
        v.Text("moveRightAxis",    "Move Right Axis",    &MoveRightAxis);
        v.Text("sprintAction",     "Sprint Action",      &SprintAction);
        v.Text("jumpAction",       "Jump Action",        &JumpAction);
        v.Bool ("useCameraControl", "Use Camera Control", &UseCameraControl);
        v.Bool ("requireMouseHold", "Require Mouse Hold", &RequireMouseHold);
        v.Float("mouseSensitivity", "Mouse Sensitivity",  &MouseSensitivity, 0.01f,  0.0f,  2.0f);
        v.Float("padLookSpeed",     "Pad Look Speed",     &PadLookSpeed,      5.0f,   0.0f, 720.0f);
        v.Float("initialPitch",     "Initial Pitch",      &InitialPitch,      1.0f, -89.0f, 89.0f);
        v.Float("minPitch",         "Min Pitch",          &MinPitch,          1.0f, -89.0f, 89.0f);
        v.Float("maxPitch",         "Max Pitch",          &MaxPitch,          1.0f, -89.0f, 89.0f);
        v.Float("cameraDistance",   "Camera Distance",    &CameraDistance,    0.1f,   0.0f, 50.0f);
        v.Float("cameraHeight",     "Camera Height",      &CameraHeight,      0.1f, -10.0f, 20.0f);
        v.Float("cameraLag",        "Camera Lag",         &CameraLag,         0.5f,   0.0f, 60.0f);
    }

    // Possess / release a pawn by its owning actor's id (UE5 APlayerController::Possess).
    void     Possess(uint64_t pawnActorId);
    void     UnPossess();
    uint64_t PossessedPawnId() const { return m_pawnId; }

    void EndPlay() override { UnPossess(); }

    void Update(float dt) override {
        PawnComponent* pawn = ResolvePawn();
        if (!pawn) return;

        Input& in = Input::Get();

        // ControlRotation: mouse (RMB-gated so it doesn't fight the editor UI) + right stick rotate
        // the view. Pitch is clamped; yaw wraps to stay bounded over long sessions.
        if (UseCameraControl) {
            if (!RequireMouseHold || in.KeyHeld(Key::MouseRight)) {
                m_yaw   += in.MouseDX() * MouseSensitivity;
                m_pitch += in.MouseDY() * MouseSensitivity;
            }
            m_yaw   += in.PadAxisValue(PadAxis::RightX) * PadLookSpeed * dt;
            m_pitch -= in.PadAxisValue(PadAxis::RightY) * PadLookSpeed * dt;   // stick up = look up
            if (m_pitch < MinPitch) m_pitch = MinPitch;
            if (m_pitch > MaxPitch) m_pitch = MaxPitch;
            if (m_yaw > 180.0f)  m_yaw -= 360.0f;
            if (m_yaw < -180.0f) m_yaw += 360.0f;
        }

        // Move direction from the named axes. With camera control the (right, forward) input is
        // rotated into the control-yaw frame so WASD is camera-relative (UE5); otherwise it stays the
        // world-fixed forward→+Z / right→+X convention CharacterMovementComponent used directly.
        float fwd   = in.GetAxis(MoveForwardAxis);
        float right = in.GetAxis(MoveRightAxis);
        if (UseCameraControl) {
            const float yr = Math::ToRadians(m_yaw);
            const float s = std::sin(yr), c = std::cos(yr);
            pawn->AddMovementInput(Vector3(right * c + fwd * s, 0.0f, -right * s + fwd * c), 1.0f);
        } else {
            pawn->AddMovementInput(Vector3(right, 0.0f, fwd), 1.0f);
        }

        pawn->SetSprintInput(in.ActionHeld(SprintAction));
        pawn->SetJumpInput(in.ActionPressed(JumpAction));   // edge; latched in the pawn until consumed
    }

    // ── Spring-arm follow camera (UE5 CameraComponent on a SpringArm) ────────────────────────────
    // Call AFTER world transforms are refreshed for the frame, so the camera reads the pawn's final
    // position with zero follow lag. Builds the ideal spot behind/above the pawn from ControlRotation
    // and eases the camera toward it (CameraLag). No-op unless enabled and possessing a pawn.
    void RefreshCamera(float dt) {
        if (!UseCameraControl) return;
        PawnComponent* pawn = ResolvePawn();
        if (!pawn) return;
        Actor* pawnActor = pawn->GetOwner();
        auto*  tc = pawnActor ? pawnActor->GetComponent<TransformComponent>() : nullptr;
        if (!tc) return;

        const Vector3 pawnPos = tc->GetWorldMatrix().GetTranslation();
        const Vector3 pivot(pawnPos.x, pawnPos.y + CameraHeight, pawnPos.z);

        // View forward from yaw/pitch, matching DebugCamera's convention (+pitch looks down).
        const float yr = Math::ToRadians(m_yaw), pr = Math::ToRadians(m_pitch);
        const Vector3 fwd(std::sin(yr) * std::cos(pr), std::sin(-pr), std::cos(yr) * std::cos(pr));
        const Vector3 desired = pivot - fwd * CameraDistance;   // pull the camera back along the arm

        if (!m_camInit) { m_camPos = desired; m_camInit = true; }
        else if (CameraLag > 0.0f) {
            const float t = 1.0f - std::exp(-CameraLag * dt);   // framerate-independent ease
            m_camPos = m_camPos + (desired - m_camPos) * t;
        } else {
            m_camPos = desired;
        }
        m_camTarget = pivot;
    }

    // True when this controller is actively driving a follow camera that should override the editor's.
    bool           HasViewCamera()   const { return UseCameraControl && m_pawnId != 0; }
    const Vector3& GetCameraPos()    const { return m_camPos; }
    const Vector3& GetCameraTarget() const { return m_camTarget; }

private:
    // Re-resolve the possessed pawn each frame from its actor id (dangle-safe).
    PawnComponent* ResolvePawn() const {
        if (m_pawnId == 0) return nullptr;
        SceneManager* scene = GetOwner() ? GetOwner()->GetScene() : nullptr;
        if (!scene) return nullptr;
        Actor* pawnActor = scene->FindActorById(m_pawnId);
        return pawnActor ? pawnActor->GetComponent<PawnComponent>() : nullptr;
    }

    uint64_t m_pawnId = 0;   // possessed pawn's actor id (0 = none)

    // ControlRotation + spring-arm camera runtime state (not serialized).
    float   m_yaw = 0.0f, m_pitch = 0.0f;   // ControlRotation in degrees
    Vector3 m_camPos{ 0, 0, 0 }, m_camTarget{ 0, 0, 0 };
    bool    m_camInit = false;              // false until the first RefreshCamera snaps into place
};

inline void PlayerControllerComponent::Possess(uint64_t pawnActorId) {
    if (m_pawnId == pawnActorId) return;
    UnPossess();
    m_pawnId = pawnActorId;
    if (PawnComponent* pawn = ResolvePawn()) {
        pawn->Possess(GetOwner() ? GetOwner()->GetId() : 0);
        // Start the ControlRotation aligned with the pawn's facing so the camera begins behind it,
        // then snap on the first RefreshCamera (no lag-in from a stale spot).
        m_pitch   = InitialPitch;
        m_camInit = false;
        if (Actor* a = pawn->GetOwner()) {
            if (auto* tc = a->GetComponent<TransformComponent>()) {
                const Matrix4x4 m = tc->Rotation.ToMatrix();   // forward = +Z column
                m_yaw = Math::ToDegrees(std::atan2(m.m[0][2], m.m[2][2]));
            }
        }
    }
}

inline void PlayerControllerComponent::UnPossess() {
    if (PawnComponent* pawn = ResolvePawn())
        pawn->UnPossess();
    m_pawnId = 0;
}

inline const bool s_playerControllerRegistered = []() {
    ComponentRegistry::Get().Register("PlayerControllerComponent",
        []() { return std::make_unique<PlayerControllerComponent>(); });
    return true;
}();

} // namespace Fujin
