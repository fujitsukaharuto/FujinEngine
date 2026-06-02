#include "Actor.h"
#include "SceneManager.h"
#include <algorithm>

namespace Fujin {

Actor::Actor(std::string name, uint64_t id)
    : m_name(std::move(name)), m_id(id) {}

void Actor::RemoveComponent(Component* c) {
    if (!c) return;
    if (m_scene && m_scene->IsTicking()) { m_scene->QueueRemoveComponent(this, c); return; }
    EraseComponentNow(c);
}

void Actor::EraseComponentNow(Component* c) {
    m_components.erase(
        std::remove_if(m_components.begin(), m_components.end(),
            [c](const std::unique_ptr<Component>& p) { return p.get() == c; }),
        m_components.end());
}

void Actor::SetParent(Actor* parent) {
    if (m_parent == parent) return;
    // Reject reparenting that would create a cycle (parent is this, or a descendant of this).
    // A cycle makes GetWorldTransform()/UpdateWorldTransforms() recurse forever → stack overflow.
    for (Actor* a = parent; a; a = a->m_parent)
        if (a == this) return;
    if (m_parent) {
        auto& siblings = m_parent->m_children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), this), siblings.end());
    }
    m_parent = parent;
    if (m_parent) m_parent->m_children.push_back(this);
}

void Actor::ToJson(nlohmann::json& j) const {
    j["id"]       = m_id;
    j["name"]     = m_name;
    j["parentId"] = m_parent ? m_parent->GetId() : uint64_t(0);
    j["components"] = nlohmann::json::array();
    for (auto& c : m_components) {
        nlohmann::json cj;
        cj["type"] = c->GetTypeName();
        c->ToJson(cj);
        j["components"].push_back(std::move(cj));
    }
}

void Actor::FromJson(const nlohmann::json& j) {
    m_name = j.value("name", m_name);
    if (!j.contains("components")) return;
    for (auto& cj : j["components"]) {
        std::string type = cj.value("type", "");
        auto comp = ComponentRegistry::Get().Create(type);
        if (comp) {
            comp->m_owner = this;
            comp->FromJson(cj);
            m_components.push_back(std::move(comp));
        }
    }
}

} // namespace Fujin
