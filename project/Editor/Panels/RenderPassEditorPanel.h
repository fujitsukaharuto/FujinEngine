#pragma once
#include "imgui_node_editor.h"

namespace Fujin {

class RenderGraph;

class RenderPassEditorPanel {
public:
    void Initialize();
    void Shutdown();
    void Draw(const RenderGraph& rg);

private:
    ax::NodeEditor::EditorContext* m_context = nullptr;
};

} // namespace Fujin
