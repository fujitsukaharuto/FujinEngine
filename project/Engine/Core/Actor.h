#pragma once
#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <type_traits>
#include <algorithm>
#include "Component.h"

namespace Fujin {

class SceneManager;

class Actor {
public:
    explicit Actor(std::string name, uint64_t id);

    // The scene (world) that owns this actor — set by SceneManager on creation/load.
    // Gameplay code reaches spawning/destruction through it: GetOwner()->GetScene()->...
    SceneManager* GetScene() const { return m_scene; }

    template<typename T, typename... Args>
    T* AddComponent(Args&&... args) {
        static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");
        auto comp = std::make_unique<T>(std::forward<Args>(args)...);
        comp->m_owner = this;
        T* ptr = comp.get();
        m_components.push_back(std::move(comp));
        return ptr;
    }

    template<typename T>
    T* GetComponent() const {
        for (auto& c : m_components)
            if (auto* p = dynamic_cast<T*>(c.get())) return p;
        return nullptr;
    }

    template<typename T>
    bool HasComponent() const { return GetComponent<T>() != nullptr; }

    template<typename T>
    void RemoveComponent() {
        auto it = std::find_if(m_components.begin(), m_components.end(),
            [](const std::unique_ptr<Component>& c) { return dynamic_cast<T*>(c.get()) != nullptr; });
        if (it != m_components.end())
            RemoveComponent(it->get());
    }

    // Remove a specific component. Deferred to the end of the pass if called during a tick
    // (FlushPendingDestroy applies it) so it never invalidates the in-progress component iteration;
    // immediate otherwise (e.g. from the editor Inspector).
    void RemoveComponent(Component* c);

    const std::string& GetName() const { return m_name; }
    void               SetName(const std::string& name) { m_name = name; }
    uint64_t           GetId()   const { return m_id; }

    Actor*                    GetParent()   const { return m_parent; }
    void                      SetParent(Actor* parent);
    const std::vector<Actor*>& GetChildren() const { return m_children; }

    const std::vector<std::unique_ptr<Component>>& GetComponents() const { return m_components; }

    void ToJson(nlohmann::json& j) const;
    void FromJson(const nlohmann::json& j);

private:
    friend class SceneManager;   // sets m_scene; calls EraseComponentNow when flushing deferrals

    void EraseComponentNow(Component* c);   // actual erase (no deferral)

    std::string   m_name;
    uint64_t      m_id;
    SceneManager* m_scene  = nullptr;
    Actor*        m_parent = nullptr;
    std::vector<Actor*>                     m_children;
    std::vector<std::unique_ptr<Component>> m_components;
};

} // namespace Fujin
