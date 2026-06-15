#include "GameMode.h"
#include "SceneManager.h"
#include "Actor.h"
#include "TransformComponent.h"
#include "PawnComponent.h"
#include "PlayerControllerComponent.h"
#include "PlayerStartComponent.h"

namespace Fujin {

void GameMode::BeginPlay(SceneManager& world) {
    m_controllerId  = 0;
    m_spawnedPawnId = 0;
    if (!AutoPossess) return;

    // Prefer a pawn already placed in the scene with AutoPossessPlayer (UE5: a hand-placed pawn wins
    // over spawning DefaultPawnClass).
    Actor* pawnActor = nullptr;
    for (auto& a : world.GetActors()) {
        if (auto* pawn = a->GetComponent<PawnComponent>()) {
            if (pawn->AutoPossessPlayer) { pawnActor = a.get(); break; }
        }
    }

    // No placed pawn → spawn the default pawn at a PlayerStart (UE5 GameMode::SpawnDefaultPawnFor).
    if (!pawnActor && DefaultPawnSpawner) {
        // Find the spawn transform: the first PlayerStart for player 0, else the origin.
        Vector3    startPos(0.0f, 0.0f, 0.0f);
        Quaternion startRot = Quaternion::Identity;
        for (auto& a : world.GetActors()) {
            auto* start = a->GetComponent<PlayerStartComponent>();
            if (!start || start->PlayerIndex != 0) continue;
            if (auto* st = a->GetComponent<TransformComponent>()) {
                startPos = st->Position;
                startRot = st->Rotation;
            }
            break;
        }

        // Build the pawn from the factory, then place it at the start. The spawner owns which
        // components the pawn has (including its TransformComponent); we only set the spawn pose.
        Actor* spawned = world.CreateActor("PlayerPawn");
        DefaultPawnSpawner(*spawned);
        auto* st = spawned->GetComponent<TransformComponent>();
        if (!st) st = spawned->AddComponent<TransformComponent>();   // ensure it has a transform
        st->Position = startPos;
        st->Rotation = startRot;

        // The components' BeginPlay pass already ran (we're called after it), so initialize this
        // freshly spawned pawn's components here — same lifecycle a placed pawn got.
        for (auto& c : spawned->GetComponents()) c->BeginPlay();

        m_spawnedPawnId = spawned->GetId();
        pawnActor       = spawned;
    }

    if (!pawnActor) return;   // nothing to possess → leave pawns unpossessed (movement falls back)

    // Spawn a player controller actor and possess the pawn (UE5: GameMode spawns the PlayerController,
    // which then Possess()es the pawn). The controller has no transform, so the editor's play
    // snapshot/restore ignores it; EndPlay destroys it (and the spawned pawn, if any).
    Actor* controller = world.CreateActor("PlayerController");
    auto*  pc = controller->AddComponent<PlayerControllerComponent>();
    pc->UseCameraControl = ThirdPersonCamera;   // drive the follow camera unless the GameMode opts out
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

    // Tear down the pawn we spawned this Play so it doesn't linger after Stop (placed pawns are left
    // alone — the editor restores their transform). Its components already got EndPlay via the pass.
    if (m_spawnedPawnId != 0) {
        if (Actor* pawn = world.FindActorById(m_spawnedPawnId))
            world.DestroyActor(pawn);
        m_spawnedPawnId = 0;
    }
}

} // namespace Fujin
