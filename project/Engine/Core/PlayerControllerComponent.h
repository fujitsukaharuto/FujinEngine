#pragma once
#include "Component.h"
#include "PropertyVisitor.h"
#include "Actor.h"
#include "SceneManager.h"
#include "PawnComponent.h"
#include "Engine/Input/Input.h"
#include "Engine/Math/Math.h"
#include <cstdint>

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

    const char* GetTypeName() const override { return "PlayerControllerComponent"; }
    void ToJson(nlohmann::json& j) const override {
        j["moveForwardAxis"] = MoveForwardAxis; j["moveRightAxis"] = MoveRightAxis;
        j["sprintAction"]    = SprintAction;    j["jumpAction"]    = JumpAction;
    }
    void FromJson(const nlohmann::json& j) override {
        MoveForwardAxis = j.value("moveForwardAxis", MoveForwardAxis);
        MoveRightAxis   = j.value("moveRightAxis",   MoveRightAxis);
        SprintAction    = j.value("sprintAction",    SprintAction);
        JumpAction      = j.value("jumpAction",      JumpAction);
    }
    void Reflect(IPropertyVisitor& v) override {
        v.Text("Move Forward Axis", &MoveForwardAxis);
        v.Text("Move Right Axis",   &MoveRightAxis);
        v.Text("Sprint Action",     &SprintAction);
        v.Text("Jump Action",       &JumpAction);
    }

    // Possess / release a pawn by its owning actor's id (UE5 APlayerController::Possess).
    void     Possess(uint64_t pawnActorId);
    void     UnPossess();
    uint64_t PossessedPawnId() const { return m_pawnId; }

    void EndPlay() override { UnPossess(); }

    void Update(float /*dt*/) override {
        PawnComponent* pawn = ResolvePawn();
        if (!pawn) return;

        Input& in = Input::Get();
        // Build a world-space move direction from the named axes and feed it to the pawn. forward
        // maps to +Z, right to +X — the same convention CharacterMovementComponent used directly.
        float fwd   = in.GetAxis(MoveForwardAxis);
        float right = in.GetAxis(MoveRightAxis);
        pawn->AddMovementInput(Vector3(right, 0.0f, fwd), 1.0f);

        pawn->SetSprintInput(in.ActionHeld(SprintAction));
        pawn->SetJumpInput(in.ActionPressed(JumpAction));   // edge; latched in the pawn until consumed
    }

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
};

inline void PlayerControllerComponent::Possess(uint64_t pawnActorId) {
    if (m_pawnId == pawnActorId) return;
    UnPossess();
    m_pawnId = pawnActorId;
    if (PawnComponent* pawn = ResolvePawn())
        pawn->Possess(GetOwner() ? GetOwner()->GetId() : 0);
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
