#include "RenderPassEditorPanel.h"
#include "Engine/Renderer/RenderGraph/RenderGraph.h"
#include "imgui.h"
#include <vector>
#include <algorithm>
#include <cmath>
#include <cfloat>

namespace Fujin {

// Per-pass header color based on name keywords.
static ImU32 PassColor(const std::string& name) {
    if (name.find("Shadow")   != std::string::npos) return IM_COL32( 96,  96, 104, 255);
    if (name.find("GBuffer")  != std::string::npos) return IM_COL32( 51, 102, 179, 255);
    if (name.find("Lighting") != std::string::npos) return IM_COL32(191, 115,  26, 255);
    if (name.find("Post")     != std::string::npos) return IM_COL32( 64, 140,  89, 255);
    return IM_COL32( 77,  77, 115, 255);
}

void RenderPassEditorPanel::Draw(const RenderGraph& rg) {
    ImGui::Begin("Render Graph");

    const uint32_t passCount = rg.GetPassCount();
    if (passCount == 0) {
        ImGui::TextDisabled("No passes recorded yet.");
        ImGui::End();
        return;
    }

    // ── Layout: column = dependency depth (longest producer chain) ────────────
    std::vector<int> depth(passCount, 0);
    for (uint32_t p = 0; p < passCount; ++p) {
        int d = 0;
        for (RGHandle res : rg.GetPassReads(p)) {
            uint32_t w = rg.GetResourceLastWritePass(res);
            if (w != UINT32_MAX && w < p)
                d = (std::max)(d, depth[w] + 1);
        }
        depth[p] = d;
    }

    const float rowH       = ImGui::GetTextLineHeightWithSpacing();
    const float headerH    = rowH + 6.0f;
    const float nodeW      = 200.0f;
    const float colSpacing = 270.0f;
    const float rowSpacing = 22.0f;

    struct NodeLayout { ImVec2 pos; float h; };
    std::vector<NodeLayout> nodes(passCount);
    std::vector<float> colY;  // next free y per column
    for (uint32_t p = 0; p < passCount; ++p) {
        int col = depth[p];
        if (static_cast<int>(colY.size()) <= col) colY.resize(col + 1, 0.0f);
        size_t rows = (std::max)(rg.GetPassReads(p).size(), rg.GetPassWrites(p).size());
        float h = headerH + static_cast<float>(rows) * rowH + 8.0f;
        nodes[p].pos = ImVec2(col * colSpacing, colY[col]);
        nodes[p].h   = h;
        colY[col] += h + rowSpacing;
    }

    // ── Canvas child (fills the tab; pan with drag, zoom with wheel) ──────────
    ImGui::BeginChild("##rgcanvas", ImVec2(0.0f, 0.0f), true,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImGuiIO&     io   = ImGui::GetIO();
    ImDrawList*  dl   = ImGui::GetWindowDrawList();
    const ImVec2 win0 = ImGui::GetCursorScreenPos();
    const ImVec2 size = ImGui::GetContentRegionAvail();
    const ImVec2 win1 = ImVec2(win0.x + size.x, win0.y + size.y);

    // Wheel zoom, anchored at the cursor (point under mouse stays put).
    if (ImGui::IsWindowHovered() && io.MouseWheel != 0.0f) {
        float prev = m_zoom;
        m_zoom = (std::min)(2.5f, (std::max)(0.2f, m_zoom * (1.0f + io.MouseWheel * 0.1f)));
        float cx = (io.MousePos.x - win0.x - m_scroll.x) / prev;
        float cy = (io.MousePos.y - win0.y - m_scroll.y) / prev;
        m_scroll.x = io.MousePos.x - win0.x - cx * m_zoom;
        m_scroll.y = io.MousePos.y - win0.y - cy * m_zoom;
    }
    const float Z = m_zoom;

    // Canvas-local → screen.
    auto S = [&](float cx, float cy) {
        return ImVec2(win0.x + m_scroll.x + cx * Z, win0.y + m_scroll.y + cy * Z);
    };

    // Background grid (scaled).
    const ImU32 gridCol = IM_COL32(255, 255, 255, 12);
    const float step    = 32.0f * Z;
    if (step > 4.0f) {
        for (float x = std::fmod(m_scroll.x, step); x < size.x; x += step)
            dl->AddLine(ImVec2(win0.x + x, win0.y), ImVec2(win0.x + x, win1.y), gridCol);
        for (float y = std::fmod(m_scroll.y, step); y < size.y; y += step)
            dl->AddLine(ImVec2(win0.x, win0.y + y), ImVec2(win1.x, win0.y + y), gridCol);
    }

    auto inPin = [&](uint32_t p, uint32_t r) {
        return S(nodes[p].pos.x,
                 nodes[p].pos.y + headerH + r * rowH + rowH * 0.5f);
    };
    auto outPin = [&](uint32_t p, uint32_t w) {
        return S(nodes[p].pos.x + nodeW,
                 nodes[p].pos.y + headerH + w * rowH + rowH * 0.5f);
    };

    // Links (producer write → consumer read), drawn under the nodes.
    for (uint32_t p = 0; p < passCount; ++p) {
        const auto& reads = rg.GetPassReads(p);
        for (uint32_t r = 0; r < static_cast<uint32_t>(reads.size()); ++r) {
            uint32_t wIdx = rg.GetResourceLastWritePass(reads[r]);
            if (wIdx == UINT32_MAX || wIdx >= passCount) continue;
            const auto& ww = rg.GetPassWrites(wIdx);
            for (uint32_t w = 0; w < static_cast<uint32_t>(ww.size()); ++w) {
                if (ww[w].handle == reads[r]) {
                    ImVec2 a = outPin(wIdx, w);
                    ImVec2 b = inPin(p, r);
                    dl->AddBezierCubic(a, ImVec2(a.x + 60.0f * Z, a.y),
                                       ImVec2(b.x - 60.0f * Z, b.y), b,
                                       IM_COL32(170, 170, 180, 200), 2.0f * Z);
                    break;
                }
            }
        }
    }

    ImFont* font = ImGui::GetFont();
    const float fsz = ImGui::GetFontSize() * Z;

    // Nodes.
    for (uint32_t p = 0; p < passCount; ++p) {
        ImVec2 nmin = S(nodes[p].pos.x, nodes[p].pos.y);
        ImVec2 nmax = S(nodes[p].pos.x + nodeW, nodes[p].pos.y + nodes[p].h);
        ImU32  col  = PassColor(rg.GetPassName(p));

        dl->AddRectFilled(nmin, nmax, IM_COL32(34, 34, 40, 235), 5.0f * Z);
        dl->AddRectFilled(nmin, ImVec2(nmax.x, nmin.y + headerH * Z), col, 5.0f * Z,
                          ImDrawFlags_RoundCornersTop);
        dl->AddRect(nmin, nmax, col, 5.0f * Z, 0, 2.0f * Z);
        dl->AddText(font, fsz, ImVec2(nmin.x + 8.0f * Z, nmin.y + 3.0f * Z),
                    IM_COL32(255, 255, 255, 255), rg.GetPassName(p).c_str());

        const auto& reads = rg.GetPassReads(p);
        for (uint32_t r = 0; r < static_cast<uint32_t>(reads.size()); ++r) {
            ImVec2 pin = inPin(p, r);
            dl->AddCircleFilled(pin, 4.0f * Z, IM_COL32(150, 200, 150, 255));
            dl->AddText(font, fsz, ImVec2(pin.x + 8.0f * Z, pin.y - fsz * 0.5f),
                        IM_COL32(205, 205, 205, 255), rg.GetResourceName(reads[r]).c_str());
        }

        const auto& writes = rg.GetPassWrites(p);
        for (uint32_t w = 0; w < static_cast<uint32_t>(writes.size()); ++w) {
            ImVec2 pin = outPin(p, w);
            dl->AddCircleFilled(pin, 4.0f * Z, IM_COL32(210, 180, 120, 255));
            const std::string& rn = rg.GetResourceName(writes[w].handle);
            ImVec2 ts = font->CalcTextSizeA(fsz, FLT_MAX, 0.0f, rn.c_str());
            dl->AddText(font, fsz, ImVec2(pin.x - 8.0f * Z - ts.x, pin.y - fsz * 0.5f),
                        IM_COL32(205, 205, 205, 255), rn.c_str());
        }
    }

    // Pan: drag empty canvas with the left or middle mouse button.
    ImGui::InvisibleButton("##pan", size,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    if (ImGui::IsItemActive()) {
        m_scroll.x += io.MouseDelta.x;
        m_scroll.y += io.MouseDelta.y;
    }

    // Hint + reset.
    dl->AddText(ImVec2(win0.x + 6.0f, win1.y - ImGui::GetTextLineHeight() - 6.0f),
                IM_COL32(150, 150, 150, 200),
                "drag: pan   wheel: zoom");
    if (ImGui::IsWindowHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        m_zoom = 1.0f;
        m_scroll = ImVec2(24.0f, 24.0f);
    }

    ImGui::EndChild();
    ImGui::End();
}

} // namespace Fujin
