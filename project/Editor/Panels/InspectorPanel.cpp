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
    DrawLight(actor);
    DrawCamera(actor);
    DrawParticle(actor);
    DrawRigidbody(actor);
    DrawCollider(actor);
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

void InspectorPanel::DrawLight(Actor* actor) {
    auto* l = actor->GetComponent<LightComponent>();
    if (!l) return;
    bool open = ImGui::CollapsingHeader("Light Component", ImGuiTreeNodeFlags_DefaultOpen);
    if (ImGui::BeginPopupContextItem("##light_ctx")) {
        if (ImGui::MenuItem("Remove Component")) actor->RemoveComponent<LightComponent>();
        ImGui::EndPopup();
    }
    if (!open || !actor->HasComponent<LightComponent>()) return;

    const char* types[] = { "Directional", "Point", "Spot" };
    int typeIndex = static_cast<int>(l->Type);
    if (ImGui::Combo("Type", &typeIndex, types, 3))
        l->Type = static_cast<LightType>(typeIndex);

    ImGui::ColorEdit3("Color", &l->Color.x);
    ImGui::DragFloat("Intensity", &l->Intensity, 0.1f, 0.0f, 100.0f);
    if (l->Type != LightType::Directional)
        ImGui::DragFloat("Range", &l->Range, 0.5f, 0.0f, 1000.0f);
    if (l->Type == LightType::Spot)
        ImGui::DragFloat("Spot Angle", &l->SpotAngle, 0.5f, 1.0f, 179.0f);
    if (l->Type != LightType::Directional)
        ImGui::Checkbox("Cast Shadows", &l->CastShadows);
}

void InspectorPanel::DrawCamera(Actor* actor) {
    auto* c = actor->GetComponent<CameraComponent>();
    if (!c) return;
    bool open = ImGui::CollapsingHeader("Camera Component", ImGuiTreeNodeFlags_DefaultOpen);
    if (ImGui::BeginPopupContextItem("##camera_ctx")) {
        if (ImGui::MenuItem("Remove Component")) actor->RemoveComponent<CameraComponent>();
        ImGui::EndPopup();
    }
    if (!open || !actor->HasComponent<CameraComponent>()) return;

    ImGui::DragFloat("FOV",       &c->FOV,      0.5f,  1.0f, 179.0f);
    ImGui::DragFloat("Near Clip", &c->NearClip, 0.001f, 0.001f, 10.0f);
    ImGui::DragFloat("Far Clip",  &c->FarClip,  1.0f,  1.0f, 10000.0f);
    ImGui::Checkbox("Active", &c->IsActive);
}

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

void InspectorPanel::DrawRigidbody(Actor* actor) {
    auto* rb = actor->GetComponent<RigidbodyComponent>();
    if (!rb) return;
    bool open = ImGui::CollapsingHeader("Rigidbody Component", ImGuiTreeNodeFlags_DefaultOpen);
    if (ImGui::BeginPopupContextItem("##rb_ctx")) {
        if (ImGui::MenuItem("Remove Component")) actor->RemoveComponent<RigidbodyComponent>();
        ImGui::EndPopup();
    }
    if (!open || !actor->HasComponent<RigidbodyComponent>()) return;

    ImGui::DragFloat("Mass",           &rb->Mass,          0.01f, 0.001f, 10000.0f);
    ImGui::DragFloat("Restitution",    &rb->Restitution,   0.01f, 0.0f,   1.0f);
    ImGui::DragFloat("Friction",       &rb->Friction,      0.01f, 0.0f,   1.0f);
    ImGui::DragFloat("Linear Damping",  &rb->LinearDamping,  0.001f, 0.0f, 1.0f);
    ImGui::DragFloat("Angular Damping", &rb->AngularDamping, 0.001f, 0.0f, 1.0f);
    ImGui::Checkbox("Kinematic", &rb->IsKinematic);
    ImGui::SameLine();
    ImGui::Checkbox("Gravity",   &rb->UseGravity);
    ImGui::TextDisabled("Velocity: %.2f %.2f %.2f", rb->Velocity.x, rb->Velocity.y, rb->Velocity.z);
}

void InspectorPanel::DrawCollider(Actor* actor) {
    auto* col = actor->GetComponent<ColliderComponent>();
    if (!col) return;
    bool open = ImGui::CollapsingHeader("Collider Component", ImGuiTreeNodeFlags_DefaultOpen);
    if (ImGui::BeginPopupContextItem("##collider_ctx")) {
        if (ImGui::MenuItem("Remove Component")) actor->RemoveComponent<ColliderComponent>();
        ImGui::EndPopup();
    }
    if (!open || !actor->HasComponent<ColliderComponent>()) return;

    const char* shapes[] = { "Sphere", "AABB", "Capsule" };
    int shapeIdx = static_cast<int>(col->Shape);
    if (ImGui::Combo("Shape", &shapeIdx, shapes, 3))
        col->Shape = static_cast<ColliderShape>(shapeIdx);

    ImGui::DragFloat3("Offset", &col->Offset.x, 0.01f);
    ImGui::Checkbox("Trigger", &col->IsTrigger);

    switch (col->Shape) {
    case ColliderShape::Sphere:
        ImGui::DragFloat("Radius", &col->Radius, 0.01f, 0.001f, 1000.0f);
        break;
    case ColliderShape::AABB:
        ImGui::DragFloat3("Half Extents", &col->HalfExtents.x, 0.01f, 0.001f, 1000.0f);
        break;
    case ColliderShape::Capsule:
        ImGui::DragFloat("Radius",      &col->Radius,     0.01f, 0.001f, 1000.0f);
        ImGui::DragFloat("Half Height", &col->HalfHeight, 0.01f, 0.001f, 1000.0f);
        break;
    }

    // ── Collision channel & response ──────────────────────────────────────────
    ImGui::Separator();
    static const char* kChannelNames[] = {
        "WorldStatic", "WorldDynamic", "Pawn", "Projectile", "Custom1", "Custom2" };
    static const char* kRespNames[] = { "Ignore", "Overlap", "Block" };

    int ch = static_cast<int>(col->Channel);
    if (ImGui::Combo("Channel", &ch, kChannelNames, kChannelCount))
        col->Channel = static_cast<CollisionChannel>(ch);

    if (ImGui::TreeNode("Collision Responses")) {
        for (int i = 0; i < kChannelCount; ++i) {
            int r = static_cast<int>(col->Responses[static_cast<size_t>(i)]);
            ImGui::PushID(i);
            if (ImGui::Combo(kChannelNames[i], &r, kRespNames, 3))
                col->Responses[static_cast<size_t>(i)] = static_cast<CollisionResponse>(r);
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
}

// File-local property visitors backing the generic reflection Inspector.
namespace {

// Counts how many properties a component exposes (so we only draw a header when there are any).
struct CountVisitor : IPropertyVisitor {
    int count = 0;
    void Float(const char*, float*, float, float, float) override { ++count; }
    void Int  (const char*, int*, int, int)              override { ++count; }
    void Bool (const char*, bool*)                       override { ++count; }
    void Vec3 (const char*, Vector3*, float)             override { ++count; }
    void Color(const char*, Vector3*)                    override { ++count; }
    void Text (const char*, std::string*)                override { ++count; }
};

// Renders each property with the matching ImGui widget. min>=max ⇒ unbounded drag.
struct ImGuiVisitor : IPropertyVisitor {
    void Float(const char* n, float* v, float speed, float mn, float mx) override {
        ImGui::DragFloat(n, v, speed, mn, mx);
    }
    void Int(const char* n, int* v, int mn, int mx) override {
        if (mn < mx) ImGui::SliderInt(n, v, mn, mx); else ImGui::DragInt(n, v);
    }
    void Bool(const char* n, bool* v)            override { ImGui::Checkbox(n, v); }
    void Vec3(const char* n, Vector3* v, float s) override { ImGui::DragFloat3(n, &v->x, s); }
    void Color(const char* n, Vector3* v)        override { ImGui::ColorEdit3(n, &v->x); }
    void Text(const char* n, std::string* v) override {
        char buf[256] = {};
        strncpy_s(buf, sizeof(buf), v->c_str(), _TRUNCATE);
        if (ImGui::InputText(n, buf, sizeof(buf))) *v = buf;
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
