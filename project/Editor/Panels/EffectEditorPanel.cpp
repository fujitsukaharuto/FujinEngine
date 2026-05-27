#include "EffectEditorPanel.h"
#include "Engine/Core/ParticleComponent.h"
#include "Engine/Renderer/Effect/ParticleTypes.h"
#include "imgui.h"
#include <algorithm>
#include <cstring>
#include <cstdio>

namespace Fujin {

// ─── Layout constants ─────────────────────────────────────────────────────────
static constexpr float CARD_W    = 138.0f;
static constexpr float CARD_H    = 92.0f;
static constexpr float STACK_W   = 230.0f;
static constexpr float OV_H      = CARD_H + 24.0f;   // overview child height
static constexpr float SAVEBAR_H = 32.0f;

// ─── Group definitions (Niagara colour scheme) ───────────────────────────────
struct GroupDef {
    int         id;
    const char* label;
    ImU32       bgNormal;
    ImU32       bgHover;
    ImU32       textCol;
    ImU32       dotCol;
};

static const GroupDef k_groups[] = {
    { 0, "EMITTER SETTINGS", IM_COL32( 55, 55, 60,255), IM_COL32( 72, 72, 78,255), IM_COL32(185,185,195,255), IM_COL32(160,160,170,255) },
    { 1, "SPAWN",            IM_COL32( 20, 85, 42,255), IM_COL32( 28,105, 52,255), IM_COL32(105,225,135,255), IM_COL32( 80,210,110,255) },
    { 2, "INITIALIZE",       IM_COL32( 20, 50,105,255), IM_COL32( 25, 65,130,255), IM_COL32( 95,160,235,255), IM_COL32( 70,140,215,255) },
    { 3, "UPDATE",           IM_COL32(100, 68, 10,255), IM_COL32(120, 82, 12,255), IM_COL32(235,178, 55,255), IM_COL32(215,158, 35,255) },
    { 4, "RENDER",           IM_COL32( 75, 20, 95,255), IM_COL32( 92, 25,115,255), IM_COL32(188,115,235,255), IM_COL32(168, 95,215,255) },
};

static const char* s_renderModes[] = { "Sprite", "Beam", "Ribbon" };
static const char* s_simModes[]    = { "CPU", "GPU" };
static const char* s_blendModes[]  = { "Alpha Blend", "Additive" };
static const char* s_shapes[]      = { "Point", "Sphere", "Cone", "Box" };

// ─── Helpers ──────────────────────────────────────────────────────────────────

static ImU32 RenderModeStripe(EmitterRenderMode m) {
    switch (m) {
    case EmitterRenderMode::Sprite: return IM_COL32(210,140, 40,255);
    case EmitterRenderMode::Beam:   return IM_COL32( 55,160,230,255);
    case EmitterRenderMode::Ribbon: return IM_COL32(180, 55,220,255);
    }
    return IM_COL32(120,120,120,255);
}

// Draws a coloured group header bar; returns true when the group is selected.
static bool DrawGroupBar(int gid, int& sel, const GroupDef& g) {
    float  w = ImGui::GetContentRegionAvail().x;
    float  h = 26.0f;
    ImVec2 p = ImGui::GetCursorScreenPos();

    char id[16]; sprintf_s(id, "##gb%d", gid);
    ImGui::InvisibleButton(id, ImVec2(w, h));
    bool hov  = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked()) sel = (sel == gid) ? -1 : gid;

    bool selected = (sel == gid);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x+w, p.y+h), hov ? g.bgHover : g.bgNormal, 3.0f);

    // Left selection bar
    if (selected)
        dl->AddRectFilled(p, ImVec2(p.x+4, p.y+h), g.textCol);
    // Outer border when selected
    if (selected)
        dl->AddRect(p, ImVec2(p.x+w, p.y+h), IM_COL32(255,255,255,50), 3.0f, 0, 1.2f);

    // Arrow + label
    const char* arrow = selected ? "v " : "> ";
    dl->AddText(ImVec2(p.x+8,  p.y+5), g.textCol, arrow);
    dl->AddText(ImVec2(p.x+24, p.y+5), g.textCol, g.label);
    return selected;
}

// Draws a module row inside a group (clicking selects the parent group).
static void DrawModuleRow(int gid, int& sel, const char* label, ImU32 dotCol) {
    float  w = ImGui::GetContentRegionAvail().x;
    float  h = ImGui::GetTextLineHeight() + 6.0f;
    ImVec2 p = ImGui::GetCursorScreenPos();

    char id[32]; sprintf_s(id, "##mr%d_%s", gid, label);
    ImGui::InvisibleButton(id, ImVec2(w, h));
    bool hov = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked()) sel = gid;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hov) dl->AddRectFilled(p, ImVec2(p.x+w, p.y+h), IM_COL32(60,62,68,255), 2.0f);
    // coloured dot
    dl->AddCircleFilled(ImVec2(p.x+14, p.y+h*0.5f), 3.5f, dotCol);
    dl->AddText(ImVec2(p.x+24, p.y+3), IM_COL32(185,185,190,255), label);
}

// ─── Top bar ──────────────────────────────────────────────────────────────────

void EffectEditorPanel::DrawTopBar(Actor* actor, ParticleComponent* pc,
                                    bool showClose, bool& open) {
    // Actor name label
    ImGui::TextColored(ImVec4(0.7f,0.8f,1.0f,1.0f), "Actor:");
    ImGui::SameLine();
    ImGui::Text("%s", actor->GetName().c_str());

    // Right-side controls
    float rightW = showClose ? 288.0f : 216.0f;
    float cx = ImGui::GetContentRegionMax().x - rightW;
    if (cx < ImGui::GetCursorPosX()) cx = ImGui::GetCursorPosX();
    ImGui::SameLine(cx);

    ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32( 30,110, 50,255));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  IM_COL32( 40,135, 65,255));
    if (ImGui::Button("  Play  ")) pc->Play();
    ImGui::PopStyleColor(2);

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32(120, 35, 35,255));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  IM_COL32(150, 45, 45,255));
    if (ImGui::Button("  Stop  ")) pc->Stop();
    ImGui::PopStyleColor(2);

    ImGui::SameLine();
    if (ImGui::Button(" Reset ")) pc->Reset();

    if (showClose) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32( 80, 25, 25,255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  IM_COL32(120, 35, 35,255));
        if (ImGui::Button("  X Close  ")) open = false;
        ImGui::PopStyleColor(2);
    }
}

// ─── Emitter card ────────────────────────────────────────────────────────────

void EffectEditorPanel::DrawEmitterCard(int idx, Emitter& em, bool selected, int& outRemove) {
    const auto& desc = em.GetDesc();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 p1 = { p0.x + CARD_W, p0.y + CARD_H };

    char id[16]; sprintf_s(id, "##c%d", idx);
    ImGui::InvisibleButton(id, ImVec2(CARD_W, CARD_H));
    bool hov = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked()) m_selectedEmitter = idx;

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Background
    ImU32 bg = selected ? IM_COL32(48,65,108,255)
                        : (hov ? IM_COL32(55,57,63,255) : IM_COL32(42,43,47,255));
    dl->AddRectFilled(p0, p1, bg, 5.0f);

    // Coloured top stripe (render mode)
    ImU32 stripe = RenderModeStripe(desc.RenderMode);
    dl->AddRectFilled(p0, ImVec2(p1.x, p0.y + 4), stripe, 5.0f);

    // Border
    ImU32 bdr = selected ? IM_COL32(95,148,245,255) : IM_COL32(68,70,76,255);
    dl->AddRect(p0, p1, bdr, 5.0f, 0, selected ? 2.0f : 1.0f);

    // Playing dot
    ImU32 dot = em.IsPlaying() ? IM_COL32(72,220,95,255) : IM_COL32(215,75,75,255);
    dl->AddCircleFilled(ImVec2(p0.x+11, p0.y+17), 5.0f, dot);

    // Emitter name (truncated)
    char name[24]; sprintf_s(name, "%.20s", desc.Name.c_str());
    dl->AddText(ImVec2(p0.x+22, p0.y+10), IM_COL32(210,212,218,255), name);

    // Render mode label (stripe is already IM_COL32 with alpha=255)
    dl->AddText(ImVec2(p0.x+8, p0.y+30), stripe, s_renderModes[(int)desc.RenderMode]);

    // GPU badge
    if (desc.Simulation == SimMode::GPU) {
        ImVec2 b0 = { p1.x-38, p0.y+27 }, b1 = { p1.x-5, p0.y+39 };
        dl->AddRectFilled(b0, b1, IM_COL32(50,90,200,220), 3.0f);
        dl->AddText(ImVec2(b0.x+4, b0.y+1), IM_COL32(190,215,255,255), "GPU");
    }

    // Particle count info
    char cnt[32];
    if (desc.Simulation == SimMode::CPU)
        sprintf_s(cnt, "Active : %d / %d", em.GetActiveCount(), desc.MaxParticles);
    else
        sprintf_s(cnt, "Pool   : %d", desc.MaxParticles);
    dl->AddText(ImVec2(p0.x+8, p0.y+48), IM_COL32(115,118,128,255), cnt);

    // Loop indicator
    dl->AddText(ImVec2(p0.x+8, p0.y+65), IM_COL32(90,93,102,255),
                desc.Loop ? "Loop: ON" : "Loop: OFF");

    // Context menu
    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Remove")) outRemove = idx;
        ImGui::EndPopup();
    }
}

// ─── Overview row ─────────────────────────────────────────────────────────────

void EffectEditorPanel::DrawOverview(ParticleComponent* pc) {
    ImGui::BeginChild("##ov", ImVec2(0, OV_H), false, ImGuiWindowFlags_HorizontalScrollbar);

    auto& emitters = pc->GetEmitters();
    int removeIdx = -1;

    for (int i = 0; i < (int)emitters.size(); ++i) {
        if (i > 0) ImGui::SameLine(0, 8);
        ImGui::PushID(i);
        DrawEmitterCard(i, emitters[i], i == m_selectedEmitter, removeIdx);
        ImGui::PopID();
    }

    // "+ Add" button
    ImGui::SameLine(0, 8);
    ImGui::PushStyleColor(ImGuiCol_Button,       IM_COL32(48,50,56,255));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(62,65,72,255));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
    if (ImGui::Button("  +\nAdd", ImVec2(64, CARD_H)))
        ImGui::OpenPopup("##addpop");
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);

    if (ImGui::BeginPopup("##addpop")) {
        ImGui::TextDisabled("Add Emitter Preset");
        ImGui::Separator();
        if (ImGui::MenuItem("Steam"))             { pc->AddEmitter(MakeSteamPreset()); }
        if (ImGui::MenuItem("Sparks"))            { pc->AddEmitter(MakeSparkPreset()); }
        if (ImGui::MenuItem("Beam"))              { pc->AddEmitter(MakeBeamPreset()); }
        if (ImGui::MenuItem("Ribbon"))            { pc->AddEmitter(MakeRibbonPreset()); }
        if (ImGui::MenuItem("Fire"))              { pc->AddEmitter(MakeFirePreset()); }
        ImGui::Separator();
        if (ImGui::MenuItem("Fire (GPU)")) {
            EmitterDesc gpuFire = MakeFirePreset();
            gpuFire.Name       = "GPU Fire";
            gpuFire.Simulation = SimMode::GPU;
            pc->AddEmitter(gpuFire);
        }
        if (ImGui::MenuItem("Empty Sprite (CPU)")) { EmitterDesc d; d.Name = "New Sprite"; pc->AddEmitter(d); }
        if (ImGui::MenuItem("Empty Sprite (GPU)")) {
            EmitterDesc d; d.Name = "New GPU Sprite"; d.Simulation = SimMode::GPU;
            pc->AddEmitter(d);
        }
        ImGui::EndPopup();
    }

    // Handle removal
    if (removeIdx >= 0) {
        emitters.erase(emitters.begin() + removeIdx);
        if (m_selectedEmitter >= (int)emitters.size())
            m_selectedEmitter = (int)emitters.size() - 1;
    }

    ImGui::EndChild();
}

// ─── Stack ────────────────────────────────────────────────────────────────────

void EffectEditorPanel::DrawStack(Emitter& em) {
    const auto& desc = em.GetDesc();

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 2));

    for (const auto& g : k_groups) {
        DrawGroupBar(g.id, m_selectedGroup, g);

        // Module items (always visible below each header)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4);
        ImGui::BeginGroup();
        switch (g.id) {
        case 0: // Settings
            DrawModuleRow(0, m_selectedGroup, "General Settings", g.dotCol);
            break;
        case 1: // Spawn
            DrawModuleRow(1, m_selectedGroup, "Spawn Rate",       g.dotCol);
            if (desc.Spawn.BurstCount > 0)
                DrawModuleRow(1, m_selectedGroup, "Burst",        g.dotCol);
            break;
        case 2: // Initialize
            DrawModuleRow(2, m_selectedGroup, "Init Particle",    g.dotCol);
            if (desc.Init.UseColorMid)
                DrawModuleRow(2, m_selectedGroup, "Color Mid Stop",   g.dotCol);
            break;
        case 3: // Update
            DrawModuleRow(3, m_selectedGroup, "Gravity Force",    g.dotCol);
            DrawModuleRow(3, m_selectedGroup, "Drag Force",       g.dotCol);
            if (desc.Update.FadeColor)
                DrawModuleRow(3, m_selectedGroup, "Color Over Life",  g.dotCol);
            if (desc.Update.ShrinkSize)
                DrawModuleRow(3, m_selectedGroup, "Size Over Life",   g.dotCol);
            if (desc.Update.Turbulence)
                DrawModuleRow(3, m_selectedGroup, "Turbulence",        g.dotCol);
            if (desc.Update.UseAttractor)
                DrawModuleRow(3, m_selectedGroup, "Attractor",         g.dotCol);
            break;
        case 4: { // Render
            const char* rn[] = { "Sprite Renderer", "Beam Renderer", "Ribbon Renderer" };
            DrawModuleRow(4, m_selectedGroup, rn[(int)desc.RenderMode], g.dotCol);
            break;
        }
        }
        ImGui::EndGroup();
        ImGui::Dummy(ImVec2(0, 4));
    }

    ImGui::PopStyleVar();
}

// ─── Parameters panel ────────────────────────────────────────────────────────

void EffectEditorPanel::DrawParameters(Emitter& em) {
    auto& d = em.GetDesc();

    if (m_selectedGroup < 0) {
        ImGui::Spacing();
        ImGui::TextDisabled("  Select a module group on the left.");
        return;
    }

    const GroupDef& g = k_groups[m_selectedGroup];

    // Panel header
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x;
        ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x+w, p.y+28), g.bgNormal, 0);
        ImGui::GetWindowDrawList()->AddText(ImVec2(p.x+10, p.y+6), g.textCol, g.label);
        ImGui::Dummy(ImVec2(w, 28));
    }
    ImGui::Separator();
    ImGui::Spacing();

    // ── 2-column property table helpers ──────────────────────────────────────
    // ラベル列(固定幅) / ウィジェット列(残り全幅) で独立管理するためラベルが切れない
    const float labelW = std::max(80.0f, ImGui::GetContentRegionAvail().x * 0.36f);

    auto BeginProps = [&]() {
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4, 3));
        ImGui::BeginTable("##pt", 2,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoBordersInBody);
        ImGui::TableSetupColumn("##l", ImGuiTableColumnFlags_WidthFixed, labelW);
        ImGui::TableSetupColumn("##w", ImGuiTableColumnFlags_WidthStretch);
    };
    auto EndProps = [&]() {
        ImGui::EndTable();
        ImGui::PopStyleVar();
    };
    // ラベルをセルに表示してから右セルへ移動し、ウィジェット幅を列幅いっぱいにセット
    auto Row = [&](const char* label) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
    };

    switch (m_selectedGroup) {

    // ── EMITTER SETTINGS ─────────────────────────────────────────────────────
    case 0: {
        char nameBuf[64] = {};
        strncpy_s(nameBuf, d.Name.c_str(), _TRUNCATE);

        BeginProps();
        Row("Name");
        if (ImGui::InputText("##n", nameBuf, sizeof(nameBuf)))
            d.Name = nameBuf;

        int mode = (int)d.RenderMode;
        Row("Render Mode");
        if (ImGui::Combo("##rm", &mode, s_renderModes, 3)) {
            d.RenderMode = (EmitterRenderMode)mode;
            em.Reset();
        }

        int sim = (int)d.Simulation;
        Row("Simulation");
        if (ImGui::Combo("##sim", &sim, s_simModes, 2))
            d.Simulation = (SimMode)sim;

        int blend = (int)d.Blend;
        Row("Blend Mode");
        if (ImGui::Combo("##bm", &blend, s_blendModes, 2))
            d.Blend = (BlendMode)blend;

        Row("Emissive");
        ImGui::DragFloat("##ei", &d.EmissiveIntensity, 0.1f, 0.0f, 20.0f, "x%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("HDR color multiplier. >1.0 で Bloom が光って見える\n(Additive + 2〜5 が目安)");

        Row("Max Particles");
        if (ImGui::DragInt("##mp", &d.MaxParticles, 1.0f, 1, 10000))
            em.Initialize(d);

        Row("Loop");
        ImGui::Checkbox("##loop", &d.Loop);
        if (!d.Loop) {
            Row("Duration (s)");
            ImGui::DragFloat("##dur", &d.Duration, 0.1f, 0.0f, 999.0f, "%.1f");
        }
        EndProps();

        ImGui::Spacing();
        ImGui::TextDisabled("--- Live ---");
        if (d.Simulation == SimMode::CPU)
            ImGui::Text("Active: %d / %d", em.GetActiveCount(), d.MaxParticles);
        else
            ImGui::Text("GPU pool: %d particles", d.MaxParticles);
        ImGui::Text("Playing: %s", em.IsPlaying() ? "Yes" : "No");
        break;
    }

    // ── SPAWN ─────────────────────────────────────────────────────────────────
    case 1: {
        BeginProps();
        Row("Rate / sec");
        ImGui::DragFloat("##rs", &d.Spawn.RatePerSecond, 0.5f, 0.0f, 5000.0f, "%.1f");
        Row("Burst Count");
        ImGui::DragInt("##bc", &d.Spawn.BurstCount, 1.0f, 0, 1000);
        EndProps();

        ImGui::Spacing();
        ImGui::TextDisabled("Spawn Shape");
        ImGui::Separator();

        BeginProps();
        int sh = (int)d.Spawn.Shape;
        Row("Shape");
        if (ImGui::Combo("##sh", &sh, s_shapes, 4))
            d.Spawn.Shape = (EmitterShape)sh;

        switch (d.Spawn.Shape) {
        case EmitterShape::Sphere:
            Row("Radius");
            ImGui::DragFloat("##srad", &d.Spawn.ShapeRadius, 0.01f, 0.0f, 50.0f);
            break;
        case EmitterShape::Box:
            Row("Extent");
            ImGui::DragFloat3("##ext", &d.Spawn.ShapeExtent.x, 0.01f, 0.0f, 50.0f);
            break;
        case EmitterShape::Cone:
            Row("Cone Angle");
            ImGui::DragFloat("##ca", &d.Spawn.ConeAngleDeg, 0.5f, 0.0f, 90.0f);
            Row("Emit Dir");
            ImGui::DragFloat3("##ed", &d.Spawn.EmitDir.x, 0.01f);
            break;
        default: break;
        }
        EndProps();
        break;
    }

    // ── INITIALIZE ────────────────────────────────────────────────────────────
    case 2: {
        ImGui::TextDisabled("Velocity (Min / Max)");
        ImGui::Separator();
        BeginProps();
        Row("Vel Min");
        ImGui::DragFloat3("##v0", &d.Init.VelMin.x, 0.05f);
        Row("Vel Max");
        ImGui::DragFloat3("##v1", &d.Init.VelMax.x, 0.05f);
        EndProps();

        ImGui::Spacing();
        ImGui::TextDisabled("Lifetime");
        ImGui::Separator();
        BeginProps();
        float life[2] = { d.Init.LifeMin, d.Init.LifeMax };
        Row("Life Min / Max");
        if (ImGui::DragFloat2("##lf", life, 0.05f, 0.01f, 60.0f))
            { d.Init.LifeMin = life[0]; d.Init.LifeMax = life[1]; }
        EndProps();

        ImGui::Spacing();
        ImGui::TextDisabled("Size");
        ImGui::Separator();
        BeginProps();
        float sz[2] = { d.Init.SizeMin, d.Init.SizeMax };
        Row("Size Min / Max");
        if (ImGui::DragFloat2("##sz", sz, 0.005f, 0.001f, 10.0f))
            { d.Init.SizeMin = sz[0]; d.Init.SizeMax = sz[1]; }
        EndProps();

        ImGui::Spacing();
        ImGui::TextDisabled("Colour");
        ImGui::Separator();
        BeginProps();
        Row("Start");
        ImGui::ColorEdit4("##cs", &d.Init.ColorStart.x);
        Row("Use Mid Stop");
        ImGui::Checkbox("##ucm", &d.Init.UseColorMid);
        if (d.Init.UseColorMid) {
            Row("Mid");
            ImGui::ColorEdit4("##cm", &d.Init.ColorMid.x);
        }
        Row("End");
        ImGui::ColorEdit4("##ce", &d.Init.ColorEnd.x);
        EndProps();

        ImGui::Spacing();
        ImGui::TextDisabled("Rotation Rate");
        ImGui::Separator();
        BeginProps();
        float rr[2] = { d.Init.RotRateMin, d.Init.RotRateMax };
        Row("RotRate Min/Max");
        if (ImGui::DragFloat2("##rr", rr, 1.0f))
            { d.Init.RotRateMin = rr[0]; d.Init.RotRateMax = rr[1]; }
        EndProps();
        break;
    }

    // ── UPDATE ────────────────────────────────────────────────────────────────
    case 3: {
        ImGui::TextDisabled("Forces");
        ImGui::Separator();
        BeginProps();
        Row("Gravity");
        ImGui::DragFloat3("##gv", &d.Update.Gravity.x, 0.1f);
        Row("Drag");
        ImGui::DragFloat("##dr", &d.Update.Drag, 0.005f, 0.0f, 1.0f, "%.3f");
        EndProps();

        ImGui::Spacing();
        ImGui::TextDisabled("Over Lifetime");
        ImGui::Separator();
        BeginProps();
        Row("Fade Colour");
        ImGui::Checkbox("##fc", &d.Update.FadeColor);
        Row("Shrink Size");
        ImGui::Checkbox("##ss", &d.Update.ShrinkSize);
        if (d.Update.ShrinkSize) {
            Row("Size End Mult");
            ImGui::DragFloat("##se", &d.Update.SizeEndMult, 0.01f, 0.0f, 5.0f, "%.2f");
        }
        EndProps();

        ImGui::Spacing();
        ImGui::TextDisabled("Turbulence");
        ImGui::Separator();
        BeginProps();
        Row("Enable");
        ImGui::Checkbox("##tu", &d.Update.Turbulence);
        if (d.Update.Turbulence) {
            Row("Strength");
            ImGui::DragFloat("##ts", &d.Update.TurbStrength, 0.05f, 0.0f, 20.0f, "%.2f");
            Row("Frequency");
            ImGui::DragFloat("##tf", &d.Update.TurbFrequency, 0.01f, 0.01f, 5.0f, "%.2f");
        }
        EndProps();

        ImGui::Spacing();
        ImGui::TextDisabled("Attractor");
        ImGui::Separator();
        BeginProps();
        Row("Enable");
        ImGui::Checkbox("##ate", &d.Update.UseAttractor);
        if (d.Update.UseAttractor) {
            Row("Position");
            ImGui::DragFloat3("##atp", &d.Update.AttractorPos.x, 0.1f);
            Row("Strength");
            ImGui::DragFloat("##ats", &d.Update.AttractorStrength, 0.1f, -50.0f, 50.0f, "%.1f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Positive: attract   Negative: repel");
            Row("Radius");
            ImGui::DragFloat("##atr", &d.Update.AttractorRadius, 0.1f, 0.1f, 100.0f, "%.1f");
        }
        EndProps();
        break;
    }

    // ── RENDER ────────────────────────────────────────────────────────────────
    case 4: {
        if (d.RenderMode == EmitterRenderMode::Sprite) {
            ImGui::TextColored(ImVec4(0.75f,0.45f,0.92f,1.0f), "Sprite Renderer");
            ImGui::Separator();
            ImGui::TextDisabled("(Shape from procedural soft circle)");
            ImGui::Spacing();
            ImGui::TextDisabled("Size and colour are driven by");
            ImGui::TextDisabled("Initialize and Update modules.");
        }
        else if (d.RenderMode == EmitterRenderMode::Beam) {
            ImGui::TextColored(ImVec4(0.75f,0.45f,0.92f,1.0f), "Beam Renderer");
            ImGui::Separator();
            BeginProps();
            Row("Start");
            ImGui::DragFloat3("##bs", &d.Beam.Start.x, 0.05f);
            Row("End");
            ImGui::DragFloat3("##be", &d.Beam.End.x, 0.05f);
            Row("Width");
            ImGui::DragFloat("##bw", &d.Beam.Width, 0.002f, 0.001f, 2.0f, "%.3f");
            Row("Segments");
            ImGui::DragInt("##bseg", &d.Beam.Segments, 1.0f, 2, 64);
            Row("Noise Amp");
            ImGui::DragFloat("##bna", &d.Beam.NoiseAmp, 0.01f, 0.0f, 5.0f);
            Row("Noise Speed");
            ImGui::DragFloat("##bns", &d.Beam.NoiseSpeed, 0.1f, 0.0f, 20.0f);
            EndProps();
            ImGui::Spacing();
            BeginProps();
            Row("Colour");
            ImGui::ColorEdit4("##bc", &d.Beam.Color.x);
            EndProps();
        }
        else {
            ImGui::TextColored(ImVec4(0.75f,0.45f,0.92f,1.0f), "Ribbon Renderer");
            ImGui::Separator();
            ImGui::TextDisabled("Width driven by particle Size.");
            ImGui::TextDisabled("Colour driven by Initialize/Update.");
        }
        break;
    }
    } // switch
}

// ─── Save / Load bar ─────────────────────────────────────────────────────────

void EffectEditorPanel::DrawSaveLoadBar(ParticleComponent* pc) {
    static char pathBuf[256] = "Resource/Effects/effect.fx.json";
    if (!pc->EffectPath.empty())
        strncpy_s(pathBuf, pc->EffectPath.c_str(), _TRUNCATE);

    ImGui::Separator();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 120.0f);
    ImGui::InputText("##effpath", pathBuf, sizeof(pathBuf));
    ImGui::SameLine();
    if (ImGui::Button("Save##eff", ImVec2(52, 0))) pc->SaveEffect(pathBuf);
    ImGui::SameLine();
    if (ImGui::Button("Load##eff", ImVec2(52, 0))) pc->LoadEffect(pathBuf);
}

// ─── Shared contents (used by both Draw and DrawFull) ────────────────────────

void EffectEditorPanel::DrawContents(Actor* actor, ParticleComponent* pc,
                                      bool showClose, bool& open) {
    DrawTopBar(actor, pc, showClose, open);
    ImGui::Separator();

    DrawOverview(pc);

    auto& emitters = pc->GetEmitters();
    if (m_selectedEmitter < 0 || m_selectedEmitter >= (int)emitters.size()) {
        if (!emitters.empty()) m_selectedEmitter = 0;
        else {
            ImGui::TextDisabled("No emitters. Use '+Add' to create one.");
            DrawSaveLoadBar(pc);
            return;
        }
    }

    Emitter& selEm = emitters[m_selectedEmitter];

    // Adaptive stack width: never wider than 40% of the available window width
    // so the parameters column always has at least 60% of the space.
    float availW  = ImGui::GetContentRegionAvail().x;
    float stackW  = (std::min)(STACK_W, availW * 0.40f);
    if (stackW < 175.0f) stackW = 175.0f;

    float avail_h = ImGui::GetContentRegionAvail().y - SAVEBAR_H
                    - ImGui::GetStyle().ItemSpacing.y * 2 - 4.0f;
    if (avail_h < 40.0f) avail_h = 40.0f;

    ImGui::BeginChild("##stack", ImVec2(stackW, avail_h), true);
    DrawStack(selEm);
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##params", ImVec2(0.0f, avail_h), true);
    DrawParameters(selEm);
    ImGui::EndChild();

    DrawSaveLoadBar(pc);
}

// ─── Normal docked panel ─────────────────────────────────────────────────────

void EffectEditorPanel::Draw(Actor* actor) {
    ImGui::Begin("Effect Editor");

    if (!actor) {
        ImGui::TextDisabled("No actor selected.");
        ImGui::End(); return;
    }
    auto* pc = actor->GetComponent<ParticleComponent>();
    if (!pc) {
        ImGui::TextDisabled("Selected actor has no ParticleComponent.");
        ImGui::End(); return;
    }

    bool open = true; // unused in docked mode — Close button is hidden
    DrawContents(actor, pc, false, open);

    ImGui::End();
}

// ─── Large floating editor window ────────────────────────────────────────────

void EffectEditorPanel::DrawFull(Actor* actor, bool& open) {
    if (!open) return;

    ImGuiIO& io = ImGui::GetIO();
    const float kTopOffset = 40.0f;   // menu bar + toolbar height
    const float kPanelW    = io.DisplaySize.x * 0.40f;
    const float kPanelH    = io.DisplaySize.y - kTopOffset;

    // 初回表示時のみ右端に配置。その後はユーザーが移動・リサイズ可能
    ImGui::SetNextWindowPos(
        ImVec2(io.DisplaySize.x - kPanelW, kTopOffset),
        ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(
        ImVec2(kPanelW, kPanelH),
        ImGuiCond_Appearing);
    ImGui::SetNextWindowBgAlpha(0.97f);

    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoCollapse     |
        ImGuiWindowFlags_NoDocking      |
        ImGuiWindowFlags_NoSavedSettings;

    ImGui::PushStyleColor(ImGuiCol_TitleBg,       IM_COL32(18, 28, 55, 255));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive,  IM_COL32(22, 36, 72, 255));
    ImGui::Begin("Effect Editor##full", &open, kFlags);
    ImGui::PopStyleColor(2);

    if (!actor) {
        ImGui::TextDisabled("No actor.");
        ImGui::End(); return;
    }
    auto* pc = actor->GetComponent<ParticleComponent>();
    if (!pc) {
        ImGui::TextDisabled("Selected actor has no ParticleComponent.");
        ImGui::End(); return;
    }

    DrawContents(actor, pc, true, open);

    ImGui::End();
}

} // namespace Fujin
