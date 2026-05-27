#pragma once
#include <memory>
#include <functional>
#include <unordered_map>
#include <string>
#include <json.hpp>

namespace Fujin {

class Actor;

class Component {
public:
    virtual ~Component() = default;
    virtual const char* GetTypeName() const = 0;
    virtual void ToJson(nlohmann::json& j) const = 0;
    virtual void FromJson(const nlohmann::json& j) = 0;

    Actor* GetOwner() const { return m_owner; }

private:
    friend class Actor;
    Actor* m_owner = nullptr;
};

class ComponentRegistry {
public:
    using FactoryFn = std::function<std::unique_ptr<Component>()>;

    static ComponentRegistry& Get();
    void Register(const std::string& typeName, FactoryFn factory);
    std::unique_ptr<Component> Create(const std::string& typeName) const;

private:
    std::unordered_map<std::string, FactoryFn> m_factories;
};

} // namespace Fujin
