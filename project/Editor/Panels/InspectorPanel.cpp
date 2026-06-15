#include "InspectorPanel.h"
#include "Engine/Core/Actor.h"
#include "Engine/Core/TransformComponent.h"
#include "Engine/Core/MeshComponent.h"
#include "Engine/Core/AnimationComponent.h"
#include "Engine/Core/LightComponent.h"
#include "Engine/Core/CameraComponent.h"
#include "Engine/Core/ParticleComponent.h"
#include "Engine/Core/RigidbodyComponent.h"
#include "Engine/Core/ColliderComponent.h"
#include "Engine/Core/RotatorComponent.h"
#include "Engine/Core/PropertyVisitor.h"
#include "Engine/Renderer/Material/MaterialManager.h"
#include "Engine/Math/Math.h"
#include "imgui.h"
#include <cmath>
#include <cstring>
#include <string>

namespace Fujin {

void InspectorPanel::Draw(Actor* actor) {
    ImGui::Begin("Details");
    if (!actor) { ImGui::End(); return; }

    // Name
    char nameBuf[128] = {};
    strncpy_s(nameBuf, sizeof(nameBuf), actor->GetName().c_str(), _TRUNCATE);
    if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
        actor->SetName(nameBuf);
    ImGui::Text("ID: %llu", static_cast<unsigned long long>(actor->GetId()));
    ImGui::Separator();

    DrawTransform(actor);
    DrawMesh(actor);
    DrawAnimation(actor);
    DrawParticle(actor);
    // Light / Camera / Rigidbody / Collider now render here (Reflect-driven, incl. Enum/EnumArray).
    DrawReflectedComponents(actor);

    // Add Component popup
    ImGui::Separator();
    if (ImGui::Button("Add Component"))
        ImGui::OpenPopup("AddComp");
    if (ImGui::BeginPopup("AddComp")) {
        if (!actor->HasComponent<TransformComponent>() && ImGui::MenuItem("Transform"))
            actor->AddComponent<TransformComponent>();
        if (!actor->HasComponent<MeshComponent>() && ImGui::MenuItem("Mesh"))
            actor->AddComponent<MeshComponent>();
        if (!actor->HasComponent<AnimationComponent>() && ImGui::MenuItem("Animation"))
            actor->AddComponent<AnimationComponent>();
        if (!actor->HasComponent<LightComponent>() && ImGui::MenuItem("Light"))
            actor->AddComponent<LightComponent>();
        if (!actor->HasComponent<CameraComponent>() && ImGui::MenuItem("Camera"))
            actor->AddComponent<CameraComponent>();
        if (!actor->HasComponent<ParticleComponent>() && ImGui::MenuItem("Particle"))
            actor->AddComponent<ParticleComponent>();
        if (!actor->HasComponent<RigidbodyComponent>() && ImGui::MenuItem("Rigidbody"))
            actor->AddComponent<RigidbodyComponent>();
        if (!actor->HasComponent<ColliderComponent>() && ImGui::MenuItem("Collider"))
            actor->AddComponent<ColliderComponent>();
        if (!actor->HasComponent<RotatorComponent>() && ImGui::MenuItem("Rotator (Script)"))
            actor->AddComponent<RotatorComponent>();
        ImGui::EndPopup();
    }

    ImGui::End();
}

void InspectorPanel::DrawTransform(Actor* actor) {
    auto* t = actor->GetComponent<TransformComponent>();
    if (!t) return;
    if (!ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) return;

    uint64_t actorId = actor->GetId();

    auto CaptureIfNeeded = [&]() {
        if (m_cmdHistory && !m_transformCaptured) {
            m_capturedTransform = { t->Position, t->Rotation, t->Scale };
            m_transformCaptured = true;
        }
    };
    auto CommitIfDone = [&]() {
        if (m_cmdHistory && m_transformCaptured) {
            m_cmdHistory->Push(std::make_unique<TransformCommand>(
                t, m_capturedTransform, TransformCommand::State{ t->Position, t->Rotation, t->Scale }));
            m_transformCaptured = false;
        }
    };

    // Position
    ImGui::DragFloat3("Position", &t->Position.x, 0.1f);
    if (ImGui::IsItemActivated())           CaptureIfNeeded();
    if (ImGui::IsItemDeactivatedAfterEdit()) CommitIfDone();

    // Rotation — use cached Euler to avoid Quaternion→Euler round-trip drift every frame.
    auto it = m_eulerCache.find(actorId);
    if (it == m_eulerCache.end()) {
        auto& q = t->Rotation;
        float sinr = 2.0f * (q.w * q.x + q.y * q.z);
        float cosr = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);
        float ex   = Math::ToDegrees(std::atan2(sinr, cosr));

        float sinp = 2.0f * (q.w * q.y - q.z * q.x);
        float ey   = (std::abs(sinp) >= 1.0f)
                   ? Math::ToDegrees(std::copysign(Math::HALF_PI, sinp))
                   : Math::ToDegrees(std::asin(sinp));

        float siny = 2.0f * (q.w * q.z + q.x * q.y);
        float cosy = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);
        float ez   = Math::ToDegrees(std::atan2(siny, cosy));

        m_eulerCache[actorId] = { ex, ey, ez };
        it = m_eulerCache.find(actorId);
    }

    auto& cached = it->second;
    float euler[3] = { cached[0], cached[1], cached[2] };
    if (ImGui::IsItemActivated())           CaptureIfNeeded(); // won't fire here, but kept for symmetry
    if (ImGui::DragFloat3("Rotation", euler, 0.5f)) {
        cached[0] = euler[0];
        cached[1] = euler[1];
        cached[2] = euler[2];
        t->Rotation = Quaternion::FromEuler(
            Math::ToRadians(euler[1]),
            Math::ToRadians(euler[2]),
            Math::ToRadians(euler[0]));
    }
    if (ImGui::IsItemActivated())           CaptureIfNeeded();
    if (ImGui::IsItemDeactivatedAfterEdit()) CommitIfDone();

    // Scale
    ImGui::DragFloat3("Scale", &t->Scale.x, 0.05f, 0.001f, 1000.0f);
    if (ImGui::IsItemActivated())           CaptureIfNeeded();
    if (ImGui::IsItemDeactivatedAfterEdit()) CommitIfDone();
}

void InspectorPanel::DrawMesh(Actor* actor) {
    auto* m = actor->GetComponent<MeshComponent>();
    if (!m) return;
    bool open = ImGui::CollapsingHeader("Mesh Component", ImGuiTreeNodeFlags_DefaultOpen);
    if (ImGui::BeginPopupContextItem("##mesh_ctx")) {
        if (ImGui::MenuItem("Remove Component")) actor->RemoveComponent<MeshComponent>();
        ImGui::EndPopup();
    }
    if (!open || !actor->HasComponent<MeshComponent>()) return;

    char pathBuf[256] = {};
    strncpy_s(pathBuf, sizeof(pathBuf), m->MeshPath.c_str(), _TRUNCATE);
    if (ImGui::InputText("Mesh Path", pathBuf, sizeof(pathBuf)))
        m->MeshPath = pathBuf;
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
            m->MeshPath = static_cast<const char*>(payload->Data);
        ImGui::EndDragDropTarget();
    }

    // Material path row + Create button
    strncpy_s(pathBuf, sizeof(pathBuf), m->MaterialPath.c_str(), _TRUNCATE);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 60.0f);
    if (ImGui::InputText("##matpath", pathBuf, sizeof(pathBuf)))
        m->MaterialPath = pathBuf;
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
            m->MaterialPath = static_cast<const char*>(payload->Data);
        ImGui::EndDragDropTarget();
    }
    ImGui::SameLine();
    ImGui::TextUnformatted("Material");

    // Per-mesh render flags
    ImGui::Checkbox("Double Sided", &m->DoubleSided);
    ImGui::SameLine();
    ImGui::Checkbox("Cast Shadow", &m->CastShadow);
    {
        static const char* kBlendLabels[] = { "Opaque", "Alpha Clip", "Translucent" };
        int blendIdx = static_cast<int>(m->Blend);
        if (ImGui::Combo("Blend", &blendIdx, kBlendLabels, 3))
            m->Blend = static_cast<MeshBlendMode>(blendIdx);
    }
    if (m->Blend == MeshBlendMode::Translucent) {
        ImGui::SameLine();
        ImGui::SliderFloat("Opacity", &m->Opacity, 0.0f, 1.0f);
    }

    // If no path is set, offer a quick "New" button.
    if (m->MaterialPath.empty()) {
        if (ImGui::SmallButton("New Material")) {
            m->MaterialPath = "Resource/Materials/new_material.mat.json";
        }
    }

    // Material property editor
    if (m_matMgr && !m->MaterialPath.empty()) {
        Material* mat = m_matMgr->LoadOrCreate(m->MaterialPath);
        DrawMaterialProps(mat);
    }
}

void InspectorPanel::DrawMaterialProps(Material* mat) {
    if (!mat || !m_matMgr) return;
    if (!ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) return;

    // Name
    char nameBuf[128] = {};
    strncpy_s(nameBuf, sizeof(nameBuf), mat->Name.c_str(), _TRUNCATE);
    if (ImGui::InputText("Name##mat", nameBuf, sizeof(nameBuf)))
        mat->Name = nameBuf;

    // Auto-generated parameter widgets driven by shader reflection.
    const CBLayout& layout = m_matMgr->GetLayout();
    uint8_t* data = mat->ParamData.data();
    const size_t dataSize = mat->ParamData.size();

    for (const auto& param : layout.Params) {
        if (param.ByteOffset + param.Cols * 4 > dataSize) continue;
        float* ptr = reinterpret_cast<float*>(data + param.ByteOffset);

        switch (param.Widget) {
        case ParamWidget::Color3:
            ImGui::ColorEdit3(param.Name.c_str(), ptr);
            break;
        case ParamWidget::Color4:
            ImGui::ColorEdit4(param.Name.c_str(), ptr);
            break;
        case ParamWidget::Slider01:
            ImGui::SliderFloat(param.Name.c_str(), ptr, 0.0f, 1.0f);
            break;
        case ParamWidget::DragFloat:
            ImGui::DragFloat(param.Name.c_str(), ptr, 0.01f);
            break;
        case ParamWidget::DragFloat2:
            ImGui::DragFloat2(param.Name.c_str(), ptr, 0.01f);
            break;
        case ParamWidget::DragFloat3:
            ImGui::DragFloat3(param.Name.c_str(), ptr, 0.01f);
            break;
        case ParamWidget::DragFloat4:
            ImGui::DragFloat4(param.Name.c_str(), ptr, 0.01f);
            break;
        }
    }

    // Texture path helpers (albedo, normal, ORM)
    auto drawTexField = [&](const char* label, const char* id, std::string& path) {
        char buf[256] = {};
        strncpy_s(buf, sizeof(buf), path.c_str(), _TRUNCATE);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 80.0f);
        if (ImGui::InputText(id, buf, sizeof(buf)))
            path = buf;
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
                path = static_cast<const char*>(payload->Data);
            ImGui::EndDragDropTarget();
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(label);
    };

    drawTexField("Albedo Tex", "##albtex", mat->AlbedoTexturePath);
    drawTexField("Normal Tex", "##normtex", mat->NormalTexturePath);
    drawTexField("ORM Tex",    "##ormtex",  mat->OrmTexturePath);

    if (ImGui::Button("Save Material"))
        m_matMgr->Save(*mat);
}

void InspectorPanel::DrawAnimation(Actor* actor) {
    auto* a = actor->GetComponent<AnimationComponent>();
    if (!a) return;
    bool open = ImGui::CollapsingHeader("Animation Component", ImGuiTreeNodeFlags_DefaultOpen);
    if (ImGui::BeginPopupContextItem("##anim_ctx")) {
        if (ImGui::MenuItem("Remove Component")) actor->RemoveComponent<AnimationComponent>();
        ImGui::EndPopup();
    }
    if (!open || !actor->HasComponent<AnimationComponent>()) return;

    char clipBuf[128] = {};
    strncpy_s(clipBuf, sizeof(clipBuf), a->ClipName.c_str(), _TRUNCATE);
    if (ImGui::InputText("Clip", clipBuf, sizeof(clipBuf)))
        a->ClipName = clipBuf;

    ImGui::DragFloat("Speed",       &a->Speed,      0.01f,  0.0f, 10.0f);
    ImGui::DragFloat("Time Offset", &a->TimeOffset, 0.01f, -100.0f, 100.0f);
    ImGui::Checkbox("Loop",    &a->Loop);
    ImGui::SameLine();
    ImGui::Checkbox("Playing", &a->Playing);
    ImGui::TextDisabled("Time: %.3f s", a->Time);
}

// DrawLight / DrawCamera removed — Light/Camera now declare their fields in Reflect() and render
// through DrawReflectedComponents (Camera fully Reflect-driven; Light is a hybrid that keeps its
// string-keyed ToJson/FromJson but drives the Inspector via Reflect, like FootIK).

void InspectorPanel::DrawParticle(Actor* actor) {
    auto* pc = actor->GetComponent<ParticleComponent>();
    if (!pc) return;
    bool open = ImGui::CollapsingHeader("Particle Component", ImGuiTreeNodeFlags_DefaultOpen);
    if (ImGui::BeginPopupContextItem("##particle_ctx")) {
        if (ImGui::MenuItem("Remove Component")) actor->RemoveComponent<ParticleComponent>();
        ImGui::EndPopup();
    }
    if (!open || !actor->HasComponent<ParticleComponent>()) return;

    ImGui::Text("Emitters: %d", static_cast<int>(pc->GetEmitters().size()));
    ImGui::SameLine();
    if (ImGui::SmallButton("Play"))  pc->Play();
    ImGui::SameLine();
    if (ImGui::SmallButton("Stop"))  pc->Stop();
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset")) pc->Reset();

    int total = 0;
    for (auto& e : pc->GetEmitters()) total += e.GetActiveCount();
    ImGui::TextDisabled("Active particles: %d", total);

    if (m_onEditEffect) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32( 45, 80,165,255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  IM_COL32( 60,100,200,255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,   IM_COL32( 75,120,225,255));
        if (ImGui::Button("Edit Effect", ImVec2(-1.0f, 0.0f)))
            m_onEditEffect(actor);
        ImGui::PopStyleColor(3);
    }
}

// DrawRigidbody / DrawCollider removed — both now declare their fields in Reflect() (Collider uses
// the new Enum / EnumArray visitor methods for Shape, Channel and the per-channel response matrix)
// and render through DrawReflectedComponents.

// File-local property visitors backing the generic reflection Inspector.
namespace {

// Counts how many properties a component exposes (so we only draw a header when there are any).
struct CountVisitor : IPropertyVisitor {
    int count = 0;
    void Float(const char*, const char*, float*, float, float, float) override { ++count; }
    void Int  (const char*, const char*, int*, int, int)              override { ++count; }
    void Bool (const char*, const char*, bool*)                       override { ++count; }
    void Vec3 (const char*, const char*, Vector3*, float)             override { ++count; }
    void Color(const char*, const char*, Vector3*)                    override { ++count; }
    void Text (const char*, const char*, std::string*)                override { ++count; }
    void Enum (const char*, const char*, int*, const char* const*, int) override { ++count; }
    void EnumArray(const char*, const char*, int*, int, const char* const*,
                   const char* const*, int)                           override { ++count; }
};

// Renders each property with the matching ImGui widget, labelled by `label` (the `key` — the JSON
// serialization name — is unused here). min>=max ⇒ unbounded drag.
struct ImGuiVisitor : IPropertyVisitor {
    void Float(const char*, const char* n, float* v, float speed, float mn, float mx) override {
        ImGui::DragFloat(n, v, speed, mn, mx);
    }
    void Int(const char*, const char* n, int* v, int mn, int mx) override {
        if (mn < mx) ImGui::SliderInt(n, v, mn, mx); else ImGui::DragInt(n, v);
    }
    void Bool(const char*, const char* n, bool* v)            override { ImGui::Checkbox(n, v); }
    void Vec3(const char*, const char* n, Vector3* v, float s) override { ImGui::DragFloat3(n, &v->x, s); }
    void Color(const char*, const char* n, Vector3* v)        override { ImGui::ColorEdit3(n, &v->x); }
    void Text(const char*, const char* n, std::string* v) override {
        char buf[256] = {};
        strncpy_s(buf, sizeof(buf), v->c_str(), _TRUNCATE);
        if (ImGui::InputText(n, buf, sizeof(buf))) *v = buf;
    }
    void Enum(const char*, const char* n, int* v, const char* const* names, int count) override {
        ImGui::Combo(n, v, names, count);
    }
    void EnumArray(const char*, const char* n, int* base, int elemCount,
                   const char* const* elemLabels, const char* const* optionNames, int optionCount) override {
        if (!ImGui::TreeNode(n)) return;
        for (int i = 0; i < elemCount; ++i) {
            ImGui::PushID(i);
            ImGui::Combo(elemLabels[i], &base[i], optionNames, optionCount);
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
};

} // namespace

void InspectorPanel::DrawReflectedComponents(Actor* actor) {
    for (auto& c : actor->GetComponents()) {
        Component* comp = c.get();
        CountVisitor cv; comp->Reflect(cv);
        if (cv.count == 0) continue;   // no reflected props → a bespoke DrawXxx handles it (or none)

        ImGui::PushID(comp);
        bool open    = ImGui::CollapsingHeader(comp->GetTypeName(), ImGuiTreeNodeFlags_DefaultOpen);
        bool removed = false;
        if (ImGui::BeginPopupContextItem("##refl_ctx")) {
            if (ImGui::MenuItem("Remove Component")) removed = true;
            ImGui::EndPopup();
        }
        if (open && !removed) { ImGuiVisitor iv; comp->Reflect(iv); }
        ImGui::PopID();
        // Erase only after all ImGui calls + PopID; then stop — the removal invalidates the iterator.
        if (removed) { actor->RemoveComponent(comp); break; }
    }
}

} // namespace Fujin
