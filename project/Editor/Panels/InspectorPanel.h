#pragma once
#include <functional>
#include <unordered_map>
#include <array>
#include <cstdint>

namespace Fujin {

class Actor;
class MaterialManager;
struct Material;

class InspectorPanel {
public:
    void SetMaterialManager(MaterialManager* mgr) { m_matMgr = mgr; }
    void SetEditEffectCallback(std::function<void(Actor*)> cb) { m_onEditEffect = std::move(cb); }
    void Draw(Actor* actor);

private:
    void DrawTransform(Actor* actor);
    void DrawMesh(Actor* actor);
    void DrawLight(Actor* actor);
    void DrawAnimation(Actor* actor);
    void DrawCamera(Actor* actor);
    void DrawParticle(Actor* actor);
    void DrawMaterialProps(Material* mat);

    MaterialManager*            m_matMgr       = nullptr;
    std::function<void(Actor*)> m_onEditEffect;

    // Cached Euler angles (degrees) per actor to avoid Quaternion→Euler round-trip drift
    std::unordered_map<uint64_t, std::array<float, 3>> m_eulerCache;
};

} // namespace Fujin
