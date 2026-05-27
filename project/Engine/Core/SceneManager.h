#pragma once
#include <vector>
#include <memory>
#include <string>
#include <cstdint>
#include <utility>
#include "Actor.h"

namespace Fujin {

class SceneManager {
public:
    Actor* CreateActor(const std::string& name = "Actor");
    void   DestroyActor(Actor* actor);
    void   Clear();

    Actor* FindActorById(uint64_t id) const;
    Actor* FindActorByName(const std::string& name) const;

    const std::vector<std::unique_ptr<Actor>>& GetActors() const { return m_actors; }

    // For SceneSerializer: insert a pre-built Actor with a specific ID
    Actor* LoadActor(std::unique_ptr<Actor> actor);
    // After loading all actors, wire up parent-child links by ID
    void ResolveParentLinks(const std::vector<std::pair<uint64_t, uint64_t>>& links);

    uint64_t GetNextId() const  { return m_nextId; }
    void     SetNextId(uint64_t id) { m_nextId = id; }

private:
    std::vector<std::unique_ptr<Actor>> m_actors;
    uint64_t m_nextId = 1;
};

} // namespace Fujin
