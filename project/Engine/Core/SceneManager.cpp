#include "SceneManager.h"
#include <algorithm>

namespace Fujin {

Actor* SceneManager::CreateActor(const std::string& name) {
    auto actor = std::make_unique<Actor>(name, m_nextId++);
    Actor* ptr = actor.get();
    m_actors.push_back(std::move(actor));
    return ptr;
}

void SceneManager::DestroyActor(Actor* actor) {
    actor->SetParent(nullptr);
    // Detach children (they stay in the scene, just become root actors)
    std::vector<Actor*> children = actor->GetChildren();
    for (auto* child : children)
        child->SetParent(nullptr);

    m_actors.erase(
        std::remove_if(m_actors.begin(), m_actors.end(),
            [actor](const std::unique_ptr<Actor>& a) { return a.get() == actor; }),
        m_actors.end());
}

void SceneManager::Clear() {
    m_actors.clear();
    m_nextId = 1;
}

Actor* SceneManager::FindActorById(uint64_t id) const {
    for (auto& a : m_actors)
        if (a->GetId() == id) return a.get();
    return nullptr;
}

Actor* SceneManager::FindActorByName(const std::string& name) const {
    for (auto& a : m_actors)
        if (a->GetName() == name) return a.get();
    return nullptr;
}

Actor* SceneManager::LoadActor(std::unique_ptr<Actor> actor) {
    Actor* ptr = actor.get();
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
