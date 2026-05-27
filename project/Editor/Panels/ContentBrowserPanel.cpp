#include "ContentBrowserPanel.h"
#include "imgui.h"
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace Fujin {

// Extensions that are internal auxiliary files and should not be shown.
static bool IsHiddenExtension(const fs::path& p) {
    std::string ext = p.extension().string();
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".mtl"   // OBJ material sidecar
        || ext == ".bin";  // glTF binary chunk
}

void ContentBrowserPanel::Draw() {
    ImGui::Begin("Content Browser");

    // Back button
    if (m_currentPath != "Resource") {
        if (ImGui::Button("<")) {
            fs::path p(m_currentPath);
            if (p.has_parent_path())
                m_currentPath = p.parent_path().string();
        }
        ImGui::SameLine();
    }
    ImGui::Text("%s", m_currentPath.c_str());
    ImGui::SameLine(ImGui::GetContentRegionMax().x - 204.0f);
    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputText("##search", m_searchBuf, sizeof(m_searchBuf));
    ImGui::Separator();

    std::string filter(m_searchBuf);

    fs::path dir(m_currentPath);
    if (!fs::exists(dir)) {
        ImGui::TextDisabled("Directory not found: %s", m_currentPath.c_str());
        ImGui::End();
        return;
    }

    // Directories first
    for (auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_directory()) continue;
        std::string name = entry.path().filename().string();
        if (!filter.empty() && name.find(filter) == std::string::npos) continue;
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.878f, 0.750f, 0.400f, 1.0f));
        if (ImGui::Selectable(("[D] " + name).c_str(), false, ImGuiSelectableFlags_AllowDoubleClick)) {
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                m_currentPath = entry.path().string();
        }
        ImGui::PopStyleColor();
    }

    // Files
    for (auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_directory()) continue;
        if (IsHiddenExtension(entry.path())) continue;
        std::string name = entry.path().filename().string();
        if (!filter.empty() && name.find(filter) == std::string::npos) continue;
        ImGui::Selectable(name.c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", entry.path().string().c_str());
        if (ImGui::BeginDragDropSource()) {
            std::string payloadPath = entry.path().generic_string();
            ImGui::SetDragDropPayload("ASSET_PATH", payloadPath.c_str(), payloadPath.size() + 1);
            ImGui::Text("%s", name.c_str());
            ImGui::EndDragDropSource();
        }
    }

    ImGui::End();
}

} // namespace Fujin
