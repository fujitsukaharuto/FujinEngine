#pragma once
#include <functional>
#include <unordered_map>
#include <array>
#include <cstdint>
#include "Editor/Command/CommandHistory.h"
#include "Editor/Command/TransformCommand.h"

namespace Fujin {

class Actor;
class MaterialManager;
struct Material;

class InspectorPanel {
public:
    void SetMaterialManager(MaterialManager* mgr)   { m_matMgr = mgr; }
    void SetCommandHistory(CommandHistory* ch)       { m_cmdHistory = ch; }
    void SetEditEffectCallback(std::function<void(Actor*)> cb) { m_onEditEffect = std::move(cb); }
    void InvalidateEulerCache(uint64_t actorId)      { m_eulerCache.erase(actorId); }
    void Draw(Actor* actor);

private:
    void DrawTransform(Actor* actor);
    void DrawMesh(Actor* actor);
    void DrawLight(Actor* actor);
    void DrawAnimation(Actor* actor);
    void DrawCamera(Actor* actor);
    void DrawParticle(Actor* actor);
    void DrawRigidbody(Actor* actor);
    void DrawCollider(Actor* actor);
    // Generic, reflection-driven panel for any component that overrides Component::Reflect().
    // Replaces per-type DrawXxx for gameplay/script components — no edits here to add a new one.
    void DrawReflectedComponents(Actor* actor);
    void DrawMaterialProps(Material* mat);

    MaterialManager*            m_matMgr       = nullptr;
    CommandHistory*             m_cmdHistory   = nullptr;
    std::function<void(Actor*)> m_onEditEffect;

    // Cached Euler angles (degrees) per actor to avoid Quaternion→Euler round-trip drift
    std::unordered_map<uint64_t, std::array<float, 3>> m_eulerCache;

    // Undo capture: state at the moment a transform field drag starts
    TransformCommand::State m_capturedTransform;
    bool                    m_transformCaptured = false;
};

} // namespace Fujin
