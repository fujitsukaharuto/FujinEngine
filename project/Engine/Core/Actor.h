#pragma once
#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <type_traits>
#include "Component.h"

namespace Fujin {

class Actor {
public:
    explicit Actor(std::string name, uint64_t id);

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
    std::string m_name;
    uint64_t    m_id;
    Actor*      m_parent = nullptr;
    std::vector<Actor*>                     m_children;
    std::vector<std::unique_ptr<Component>> m_components;
};

} // namespace Fujin
