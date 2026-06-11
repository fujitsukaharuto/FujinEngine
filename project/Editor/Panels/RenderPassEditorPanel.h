#pragma once
#include "imgui.h"

namespace Fujin {

class RenderGraph;

// Read-only visualizer for the frame's RenderGraph. Drawn with plain ImGui
// (ImDrawList) rather than imgui-node-editor, whose canvas mispositions under
// ImGui 1.92.x inside docked tabs.
class RenderPassEditorPanel {
public:
    void Initialize() {}   // kept for call-site compatibility (no state to set up)
    void Shutdown()   {}
    void Draw(const RenderGraph& rg);

private:
    ImVec2 m_scroll{ 24.0f, 24.0f }; // canvas pan offset (top-left padding by default)
    float  m_zoom = 1.0f;            // mouse-wheel zoom factor
};

} // namespace Fujin
