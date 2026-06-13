#pragma once
#include <vector>
#include <memory>
#include <string>
#include <cstdint>
#include <utility>
#include <unordered_map>
#include "Actor.h"
#include "TimerManager.h"
#include "GameMode.h"

namespace Fujin {

class PhysicsWorld;

class SceneManager {
public:
    Actor* CreateActor(const std::string& name = "Actor");
    void   DestroyActor(Actor* actor);
    void   Clear();

    Actor* FindActorById(uint64_t id) const;
    Actor* FindActorByName(const std::string& name) const;

    // Refresh every TransformComponent::CachedWorld from the parent hierarchy.
    // Call once after any batch of local-transform edits, before physics / rendering read world.
    void UpdateWorldTransforms();

    // Gameplay tick (forwarded to every component). Call only while the editor is in Play:
    //   BeginPlay on play start, Update(dt) each frame before physics, EndPlay on stop.
    // Spawning and destroying actors from inside a component callback IS supported: a new actor
    // (CreateActor) starts ticking next frame; DestroyActor is deferred to the end of the pass
    // (FlushPendingDestroy) so it never invalidates the in-progress iteration.
    void BeginPlay();
    void Update(float dt);
    void EndPlay();

    // Actually remove actors (and components) queued for destruction during a tick. Called
    // automatically at the end of each tick pass; safe to call manually at a frame-safe point.
    void FlushPendingDestroy();

    bool IsTicking() const { return m_ticking; }
    // Queue a component for deferred removal (used by Actor::RemoveComponent during a tick).
    void QueueRemoveComponent(Actor* actor, Component* c);

    // This world's timer manager (UE5 GetWorld()->GetTimerManager() analog). Ticked once per frame
    // inside Update(dt); all timers are cleared on EndPlay so they never carry into the next Play.
    // Reach it from gameplay via GetOwner()->GetScene()->GetTimerManager().
    TimerManager&       GetTimerManager()       { return m_timers; }
    const TimerManager& GetTimerManager() const { return m_timers; }

    // This world's authoritative GameMode (UE5 GetWorld()->GetAuthGameMode() analog). Runs at
    // BeginPlay to spawn a PlayerController and possess the default pawn; tears it down at EndPlay.
    // Configure from main.cpp (e.g. AutoPossess). Not serialized — like the TimerManager.
    GameMode&       GetAuthGameMode()       { return m_gameMode; }
    const GameMode& GetAuthGameMode() const { return m_gameMode; }

    // The physics world backing this scene (set by main.cpp; not owned). Gameplay reaches spatial
    // queries through it — e.g. CharacterMovementComponent ray-casts for the ground to step up / climb
    // slopes: GetOwner()->GetScene()->GetPhysicsWorld()->Raycast(...). Null until wired (graceful).
    void          SetPhysicsWorld(PhysicsWorld* p) { m_physics = p; }
    PhysicsWorld* GetPhysicsWorld() const          { return m_physics; }

    const std::vector<std::unique_ptr<Actor>>& GetActors() const { return m_actors; }

    // For SceneSerializer: insert a pre-built Actor with a specific ID
    Actor* LoadActor(std::unique_ptr<Actor> actor);
    // After loading all actors, wire up parent-child links by ID
    void ResolveParentLinks(const std::vector<std::pair<uint64_t, uint64_t>>& links);

    uint64_t GetNextId() const  { return m_nextId; }
    void     SetNextId(uint64_t id) { m_nextId = id; }

private:
    // Run `fn(component)` over every component of every actor, snapshotting the actor list and each
    // actor's component list first so that spawning actors / adding components mid-pass can't
    // invalidate the iteration. DestroyActor calls during the pass are deferred (see m_ticking),
    // then applied by FlushPendingDestroy once the pass completes.
    template<typename Fn>
    void TickPass(Fn&& fn) {
        std::vector<Actor*> actors;
        actors.reserve(m_actors.size());
        for (auto& a : m_actors) actors.push_back(a.get());

        m_ticking = true;
        for (Actor* a : actors) {
            std::vector<Component*> comps;
            comps.reserve(a->GetComponents().size());
            for (auto& c : a->GetComponents()) comps.push_back(c.get());
            for (Component* c : comps) fn(c);
        }
        m_ticking = false;

        FlushPendingDestroy();
    }

    std::vector<std::unique_ptr<Actor>> m_actors;
    std::unordered_map<uint64_t, Actor*> m_byId;   // id → live actor (kept in sync with m_actors)
    uint64_t m_nextId = 1;

    bool                  m_ticking = false;   // true while inside a TickPass → removals defer
    std::vector<uint64_t> m_pendingDestroy;    // actor ids queued for removal after the pass
    std::vector<std::pair<uint64_t, Component*>> m_pendingRemoveComp;  // (actorId, component) removals

    TimerManager  m_timers;          // world timer manager, ticked in Update, cleared on EndPlay/Clear
    PhysicsWorld* m_physics = nullptr; // backing physics world for spatial queries (not owned)
    GameMode      m_gameMode;         // auth GameMode: spawns/possesses on BeginPlay, tears down on EndPlay
};

} // namespace Fujin
