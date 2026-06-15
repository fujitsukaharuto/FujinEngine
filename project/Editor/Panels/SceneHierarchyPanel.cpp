#include "SceneHierarchyPanel.h"
#include "Engine/Core/SceneManager.h"
#include "Engine/Core/Actor.h"
#include "Engine/Core/TransformComponent.h"
#include "imgui.h"

namespace Fujin {

void SceneHierarchyPanel::Draw(SceneManager& scene) {
    ImGui::Begin("World Outliner");

    if (ImGui::Button("+ Actor")) {
        Actor* a = scene.CreateActor("Actor");
        a->AddComponent<TransformComponent>();
        m_selected = a;
    }
    ImGui::SameLine();
    if (ImGui::Button("Duplicate") && m_selected) {
        m_selected = scene.DuplicateActor(m_selected);   // select the new copy
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete") && m_selected) {
        scene.DestroyActor(m_selected);
        m_selected = nullptr;
    }
    ImGui::Separator();

    for (auto& actor : scene.GetActors()) {
        if (actor->GetParent() == nullptr)
            DrawActorNode(actor.get());
    }

    // Click on empty area to deselect
    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
        && !ImGui::IsAnyItemHovered()) {
        m_selected = nullptr;
    }

    ImGui::End();
}

void SceneHierarchyPanel::DrawActorNode(Actor* actor) {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
                             | ImGuiTreeNodeFlags_SpanAvailWidth
                             | ImGuiTreeNodeFlags_DefaultOpen;
    if (actor->GetChildren().empty()) flags |= ImGuiTreeNodeFlags_Leaf;
    if (actor == m_selected)         flags |= ImGuiTreeNodeFlags_Selected;

    // Prefix: show child indicator
    const char* prefix = actor->GetParent() ? "[C] " : "";

    bool open = ImGui::TreeNodeEx(
        reinterpret_cast<void*>(static_cast<uintptr_t>(actor->GetId())),
        flags, "%s%s", prefix, actor->GetName().c_str());

    if (ImGui::IsItemHovered() && actor->GetParent())
        ImGui::SetTooltip("Parent: %s", actor->GetParent()->GetName().c_str());

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        m_selected = actor;

    if (open) {
        for (auto* child : actor->GetChildren())
            DrawActorNode(child);
        ImGui::TreePop();
    }
}

} // namespace Fujin
