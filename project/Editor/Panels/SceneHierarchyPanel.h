#pragma once

namespace Fujin {

class SceneManager;
class Actor;

class SceneHierarchyPanel {
public:
    void   Draw(SceneManager& scene);
    Actor* GetSelectedActor() const  { return m_selected; }
    void   SetSelectedActor(Actor* a) { m_selected = a; }

private:
    void DrawActorNode(Actor* actor);
    Actor* m_selected = nullptr;
};

} // namespace Fujin
