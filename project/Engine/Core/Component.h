#pragma once
#include <memory>
#include <functional>
#include <unordered_map>
#include <string>
#include <json.hpp>

namespace Fujin {

class Actor;
class IPropertyVisitor;

class Component {
public:
    virtual ~Component() = default;
    virtual const char* GetTypeName() const = 0;

    // Save/load. The default implementations drive Reflect() through JsonWriteVisitor/JsonReadVisitor
    // (so a component that declares its fields in Reflect() round-trips with no bespoke code). Override
    // only when serialization needs to diverge from the reflected set — e.g. a serialized-but-hidden
    // field or a legacy-key migration (see FootIKComponent).
    virtual void ToJson(nlohmann::json& j) const;
    virtual void FromJson(const nlohmann::json& j);

    // Gameplay lifecycle (UE5-style). Driven by SceneManager only while the editor is in Play:
    //   BeginPlay : once when Play starts.
    //   Update    : every frame during Play, before the physics step (PrePhysics tick group).
    //   EndPlay   : once when Play stops.
    // Default no-ops so existing components are unaffected; override to add behaviour.
    virtual void BeginPlay() {}
    virtual void Update(float /*dt*/) {}
    virtual void EndPlay() {}

    // Declare editable fields for the editor's generic Inspector (override to expose properties).
    // A component that overrides this needs no bespoke DrawXxx in InspectorPanel.
    virtual void Reflect(IPropertyVisitor& /*v*/) {}

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
