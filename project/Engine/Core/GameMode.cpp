#include "GameMode.h"
#include "SceneManager.h"
#include "Actor.h"
#include "PawnComponent.h"
#include "PlayerControllerComponent.h"

namespace Fujin {

void GameMode::BeginPlay(SceneManager& world) {
    m_controllerId = 0;
    if (!AutoPossess) return;

    // Find the scene's default pawn — the first PawnComponent flagged AutoPossessPlayer.
    Actor* pawnActor = nullptr;
    for (auto& a : world.GetActors()) {
        if (auto* pawn = a->GetComponent<PawnComponent>()) {
            if (pawn->AutoPossessPlayer) { pawnActor = a.get(); break; }
        }
    }
    if (!pawnActor) return;   // nothing opted in → leave pawns unpossessed (movement falls back)

    // Spawn a player controller actor and possess the default pawn (UE5: GameMode spawns the
    // PlayerController, which then Possess()es the pawn). The controller has no transform, so the
    // editor's play snapshot/restore ignores it; EndPlay destroys it.
    Actor* controller = world.CreateActor("PlayerController");
    auto*  pc = controller->AddComponent<PlayerControllerComponent>();
    m_controllerId = controller->GetId();
    pc->Possess(pawnActor->GetId());
}

void GameMode::EndPlay(SceneManager& world) {
    if (m_controllerId != 0) {
        if (Actor* controller = world.FindActorById(m_controllerId)) {
            if (auto* pc = controller->GetComponent<PlayerControllerComponent>())
                pc->UnPossess();
            world.DestroyActor(controller);   // m_ticking is false here → immediate
        }
        m_controllerId = 0;
    }
}

} // namespace Fujin
