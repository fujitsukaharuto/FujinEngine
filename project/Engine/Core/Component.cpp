#include "Component.h"

namespace Fujin {

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
