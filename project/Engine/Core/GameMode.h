#pragma once
#include <cstdint>
#include <functional>

namespace Fujin {

class SceneManager;
class Actor;

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

    // When true, the spawned PlayerController drives a third-person spring-arm camera (and makes WASD
    // camera-relative) during Play, overriding the editor's free camera — UE5 PIE. Set false to keep
    // inspecting with the free debug camera while playing.
    bool ThirdPersonCamera = true;

    // The engine's analog of UE5 AGameModeBase::DefaultPawnClass. Since actors here are built from
    // components in code (no spawnable UClass), the "pawn class" is a factory that populates a freshly
    // created actor with the pawn's components (mesh / movement / PawnComponent / …). Set it from
    // main.cpp. When BeginPlay finds NO pawn already placed with AutoPossessPlayer, it creates an
    // actor, runs this spawner, moves it to a PlayerStart, and possesses it — so a scene needs no
    // hand-placed pawn. Leave null to keep the old behaviour exactly (a placed pawn is required).
    std::function<void(Actor&)> DefaultPawnSpawner;

    // Spawn the player controller and possess a pawn. Call after the components' BeginPlay pass so
    // every placed pawn is initialized. Prefers a pawn placed with AutoPossessPlayer; otherwise, if a
    // DefaultPawnSpawner is set, spawns one at a PlayerStart. No-op if AutoPossess is false or neither
    // a placed pawn nor a spawner is available.
    void BeginPlay(SceneManager& world);
    // Unpossess and destroy the controller (and any pawn GameMode spawned). Call after the EndPlay pass.
    void EndPlay(SceneManager& world);

    // The actors GameMode spawned this Play (0 if none) — e.g. so tools can skip them.
    uint64_t SpawnedControllerId() const { return m_controllerId; }
    uint64_t SpawnedPawnId()       const { return m_spawnedPawnId; }

private:
    uint64_t m_controllerId  = 0;   // controller actor spawned this Play (0 = none)
    uint64_t m_spawnedPawnId = 0;   // pawn actor spawned from DefaultPawnSpawner this Play (0 = none)
};

} // namespace Fujin
