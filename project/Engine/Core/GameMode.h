#pragma once
#include <cstdint>

namespace Fujin {

class SceneManager;

// GameMode — the engine's analog of a UE5 AGameModeBase, owned by the SceneManager (the "world").
// Reach it with GetWorld()->GetAuthGameMode() (here: SceneManager::GetAuthGameMode()). At BeginPlay
// it spawns a PlayerController and auto-possesses the scene's default pawn (the PawnComponent whose
// AutoPossessPlayer is set), then tears that controller down at EndPlay. Lightweight and not
// serialized — like the world's TimerManager, it's runtime wiring, configured from main.cpp.
class GameMode {
public:
    // When false, BeginPlay does nothing — pawns stay unpossessed and any movement component falls
    // back to reading Input directly (the pre-framework behaviour). Lets you opt out / A-B test.
    bool AutoPossess = true;

    // Spawn the player controller and possess the auto-possess pawn. Call after the components'
    // BeginPlay pass so every pawn is initialized. No-op if AutoPossess is false or no pawn opts in.
    void BeginPlay(SceneManager& world);
    // Unpossess and destroy the spawned controller. Call after the components' EndPlay pass.
    void EndPlay(SceneManager& world);

    // The controller actor GameMode spawned this Play (0 if none) — e.g. so tools can skip it.
    uint64_t SpawnedControllerId() const { return m_controllerId; }

private:
    uint64_t m_controllerId = 0;   // controller actor spawned this Play (0 = none)
};

} // namespace Fujin
