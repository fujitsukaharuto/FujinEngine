#include "SceneManager.h"
#include "TransformComponent.h"
#include <algorithm>

namespace Fujin {

void SceneManager::UpdateWorldTransforms() {
    // GetWorldTransform() walks up to the root, so every actor is resolved correctly regardless
    // of ordering in m_actors. Scenes here are small; a topological pass can replace this later.
    for (auto& a : m_actors) {
        if (auto* tc = a->GetComponent<TransformComponent>())
            tc->CachedWorld = tc->GetWorldTransform();
    }
}

void SceneManager::BeginPlay()      { TickPass([](Component* c)   { c->BeginPlay(); }); }
void SceneManager::Update(float dt) { TickPass([dt](Component* c) { c->Update(dt);  }); }
void SceneManager::EndPlay()        { TickPass([](Component* c)   { c->EndPlay();   }); }

void SceneManager::QueueRemoveComponent(Actor* actor, Component* c) {
    if (actor && c) m_pendingRemoveComp.push_back({ actor->GetId(), c });
}

void SceneManager::FlushPendingDestroy() {
    // Component removals first: every owning actor is still alive at this point (actor destroys are
    // also deferred to this flush), so each queued Component* is valid. EraseComponentNow is a no-op
    // if the component was already removed (de-dup).
    if (!m_pendingRemoveComp.empty()) {
        std::vector<std::pair<uint64_t, Component*>> removals;
        removals.swap(m_pendingRemoveComp);
        for (auto& [actorId, comp] : removals)
            if (Actor* a = FindActorById(actorId)) a->EraseComponentNow(comp);
    }

    if (m_pendingDestroy.empty()) return;
    // m_ticking is false here, so each DestroyActor below takes the immediate path. Resolving the
    // id (rather than caching a pointer) de-dupes: a second request for the same actor finds null.
    std::vector<uint64_t> ids;
    ids.swap(m_pendingDestroy);
    for (uint64_t id : ids)
        if (Actor* a = FindActorById(id)) DestroyActor(a);
}

Actor* SceneManager::CreateActor(const std::string& name) {
    auto actor = std::make_unique<Actor>(name, m_nextId++);
    Actor* ptr = actor.get();
    ptr->m_scene = this;
    m_byId[ptr->GetId()] = ptr;
    m_actors.push_back(std::move(actor));
    return ptr;
}

void SceneManager::DestroyActor(Actor* actor) {
    if (!actor) return;
    // Inside a tick pass, defer: removing the actor now would invalidate the in-progress iteration
    // and dangle any cached pointer. FlushPendingDestroy applies it once the pass finishes.
    if (m_ticking) { m_pendingDestroy.push_back(actor->GetId()); return; }

    actor->SetParent(nullptr);
    // Detach children (they stay in the scene, just become root actors)
    std::vector<Actor*> children = actor->GetChildren();
    for (auto* child : children)
        child->SetParent(nullptr);

    m_byId.erase(actor->GetId());   // drop the id mapping before the actor is freed
    m_actors.erase(
        std::remove_if(m_actors.begin(), m_actors.end(),
            [actor](const std::unique_ptr<Actor>& a) { return a.get() == actor; }),
        m_actors.end());
}

void SceneManager::Clear() {
    m_actors.clear();
    m_byId.clear();
    m_pendingDestroy.clear();
    m_pendingRemoveComp.clear();
    m_nextId = 1;
}

Actor* SceneManager::FindActorById(uint64_t id) const {
    auto it = m_byId.find(id);
    return it != m_byId.end() ? it->second : nullptr;
}

Actor* SceneManager::FindActorByName(const std::string& name) const {
    for (auto& a : m_actors)
        if (a->GetName() == name) return a.get();
    return nullptr;
}

Actor* SceneManager::LoadActor(std::unique_ptr<Actor> actor) {
    Actor* ptr = actor.get();
    ptr->m_scene = this;
    m_byId[ptr->GetId()] = ptr;
    m_actors.push_back(std::move(actor));
    return ptr;
}

void SceneManager::ResolveParentLinks(const std::vector<std::pair<uint64_t, uint64_t>>& links) {
    for (auto& [childId, parentId] : links) {
        if (parentId == 0) continue;
        Actor* child  = FindActorById(childId);
        Actor* parent = FindActorById(parentId);
        if (child && parent) child->SetParent(parent);
    }
}

} // namespace Fujin
