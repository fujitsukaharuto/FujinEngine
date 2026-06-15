#include "Component.h"
#include "JsonVisitor.h"

namespace Fujin {

// Default serialization: walk the component's reflected properties, writing/reading each by its key.
// Reflect() is non-const (it hands out mutable field pointers for the editor); the write path only
// reads those values, so the const_cast is safe.
void Component::ToJson(nlohmann::json& j) const {
    JsonWriteVisitor w{ j };
    const_cast<Component*>(this)->Reflect(w);
}

void Component::FromJson(const nlohmann::json& j) {
    JsonReadVisitor r{ j };
    Reflect(r);
}

ComponentRegistry& ComponentRegistry::Get() {
    static ComponentRegistry instance;
    return instance;
}

void ComponentRegistry::Register(const std::string& typeName, FactoryFn factory) {
    m_factories[typeName] = std::move(factory);
}

std::unique_ptr<Component> ComponentRegistry::Create(const std::string& typeName) const {
    auto it = m_factories.find(typeName);
    if (it == m_factories.end()) return nullptr;
    return it->second();
}

} // namespace Fujin
