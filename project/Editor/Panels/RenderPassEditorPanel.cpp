#include "RenderPassEditorPanel.h"
#include "Engine/Renderer/RenderGraph/RenderGraph.h"
#include "imgui.h"

namespace ed = ax::NodeEditor;

// Pin ID layout (avoids collisions for up to 64 passes with up to 16 slots each):
//   Node  :  passIdx + 1
//   Input :  1000 + passIdx * 16 + readSlot
//   Output:  2000 + passIdx * 16 + writeSlot

static inline ed::NodeId  NodeId (uint32_t p)               { return ed::NodeId (p + 1); }
static inline ed::PinId   InPin  (uint32_t p, uint32_t r)   { return ed::PinId  (1000 + p * 16 + r); }
static inline ed::PinId   OutPin (uint32_t p, uint32_t w)   { return ed::PinId  (2000 + p * 16 + w); }
static inline ed::LinkId  LinkId_(uint32_t p, uint32_t r)   { return ed::LinkId (1000 + p * 16 + r); }

// Simple per-pass color based on name keywords
static ImVec4 PassColor(const std::string& name) {
    if (name.find("Shadow")   != std::string::npos) return { 0.35f, 0.35f, 0.35f, 1.0f };
    if (name.find("GBuffer")  != std::string::npos) return { 0.20f, 0.40f, 0.70f, 1.0f };
    if (name.find("Lighting") != std::string::npos) return { 0.75f, 0.45f, 0.10f, 1.0f };
    if (name.find("Post")     != std::string::npos) return { 0.25f, 0.55f, 0.35f, 1.0f };
    return { 0.30f, 0.30f, 0.45f, 1.0f };
}

namespace Fujin {

void RenderPassEditorPanel::Initialize() {
    ed::Config cfg;
    cfg.SettingsFile = nullptr; // no persistence file
    m_context = ed::CreateEditor(&cfg);
}

void RenderPassEditorPanel::Shutdown() {
    if (m_context) {
        ed::DestroyEditor(m_context);
        m_context = nullptr;
    }
}

void RenderPassEditorPanel::Draw(const RenderGraph& rg) {
    ImGui::Begin("Render Graph");

    if (!m_context || rg.GetPassCount() == 0) {
        ImGui::TextDisabled("No passes recorded yet.");
        ImGui::End();
        return;
    }

    const uint32_t passCount = rg.GetPassCount();

    ed::SetCurrentEditor(m_context);
    ed::Begin("##RGCanvas", ImGui::GetContentRegionAvail());

    // Draw nodes
    for (uint32_t passIdx = 0; passIdx < passCount; ++passIdx) {
        const std::string& passName = rg.GetPassName(passIdx);
        const auto& reads  = rg.GetPassReads(passIdx);
        const auto& writes = rg.GetPassWrites(passIdx);

        ImVec4 col = PassColor(passName);
        ed::PushStyleColor(ed::StyleColor_NodeBorder, col);
        ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, 2.0f);

        ed::BeginNode(NodeId(passIdx));
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::TextUnformatted(passName.c_str());
        ImGui::PopStyleColor();

        ImGui::Dummy(ImVec2(0.0f, 2.0f));

        // Input pins (reads) on the left
        for (uint32_t r = 0; r < static_cast<uint32_t>(reads.size()); ++r) {
            ed::BeginPin(InPin(passIdx, r), ed::PinKind::Input);
            ImGui::Text("  > %s", rg.GetResourceName(reads[r]).c_str());
            ed::EndPin();
        }

        // Output pins (writes)
        for (uint32_t w = 0; w < static_cast<uint32_t>(writes.size()); ++w) {
            ed::BeginPin(OutPin(passIdx, w), ed::PinKind::Output);
            ImGui::Text("  %s >", rg.GetResourceName(writes[w].handle).c_str());
            ed::EndPin();
        }

        ed::EndNode();
        ed::PopStyleVar();
        ed::PopStyleColor();
    }

    // Draw links: for each read, find the pass that last wrote the resource
    for (uint32_t passIdx = 0; passIdx < passCount; ++passIdx) {
        const auto& reads = rg.GetPassReads(passIdx);
        for (uint32_t r = 0; r < static_cast<uint32_t>(reads.size()); ++r) {
            Fujin::RGHandle res = reads[r];
            uint32_t writerIdx = rg.GetResourceLastWritePass(res);
            if (writerIdx == UINT32_MAX) continue;

            const auto& writerWrites = rg.GetPassWrites(writerIdx);
            for (uint32_t w = 0; w < static_cast<uint32_t>(writerWrites.size()); ++w) {
                if (writerWrites[w].handle == res) {
                    ed::Link(LinkId_(passIdx, r), OutPin(writerIdx, w), InPin(passIdx, r));
                    break;
                }
            }
        }
    }

    ed::End();
    ed::SetCurrentEditor(nullptr);

    ImGui::End();
}

} // namespace Fujin
