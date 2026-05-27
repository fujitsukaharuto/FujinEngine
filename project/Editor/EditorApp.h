#pragma once
#include <Windows.h>
#include <d3d12.h>
#include "Engine/Graphics/GraphicsDevice.h"
#include "Engine/Camera/DebugCamera.h"
#include "Engine/Math/Math.h"
#include "Editor/Panels/SceneHierarchyPanel.h"
#include "Editor/Panels/InspectorPanel.h"
#include "Editor/Panels/ContentBrowserPanel.h"
#include "Editor/Panels/RenderPassEditorPanel.h"
#include "Editor/Panels/EffectEditorPanel.h"
#include "Editor/Panels/PostProcessPanel.h"
#include "Editor/Command/CommandHistory.h"
#include "Editor/Command/TransformCommand.h"
#include "imgui.h"
#include "ImGuizmo.h"

namespace Fujin {

class SceneManager;
class MaterialManager;
class SceneRenderer;

class EditorApp {
public:
    bool Initialize(HWND hwnd, GraphicsDevice& gfx, SceneManager& scene);
    void SetMaterialManager(MaterialManager* mgr);
    void SetSceneRenderer(SceneRenderer* renderer);
    void Shutdown();
    void BeginFrame(float dt);
    void Render(ID3D12GraphicsCommandList* cmdList);
    void GetViewportRect(uint32_t& x, uint32_t& y, uint32_t& w, uint32_t& h) const {
        x = m_vpX; y = m_vpY; w = m_vpW; h = m_vpH;
    }

    Vector3 GetDebugCameraPos()    const { return m_debugCamera.GetPosition(); }
    Vector3 GetDebugCameraTarget() const { return m_debugCamera.GetTarget(); }
    bool    IsPlaying()            const { return m_isPlaying; }

    void SetViewProj(const Matrix4x4& vp) { m_viewProj = vp; }
    void SetViewAndProj(const Matrix4x4& view, const Matrix4x4& proj) {
        m_view     = view;
        m_proj     = proj;
        m_viewProj = proj * view;
    }

private:
    void ApplyUE5Theme();
    void SetupDockLayout(ImGuiID dockspaceId);
    void DrawToolbar();
    void DrawFPSOverlay();
    void DrawCameraGizmos();
    void DrawGizmo();
    void DrawDebugShapes();
    bool IsViewportHovered() const;

    static Quaternion ExtractRotation(const Matrix4x4& m, const Vector3& scale);
    static Vector3    ExtractScale(const Matrix4x4& m);

    SceneManager*        m_scene          = nullptr;
    SceneRenderer*       m_sceneRenderer  = nullptr;
    SceneHierarchyPanel  m_hierarchyPanel;
    InspectorPanel       m_inspectorPanel;
    ContentBrowserPanel  m_contentBrowserPanel;
    RenderPassEditorPanel m_renderPassPanel;
    EffectEditorPanel     m_effectPanel;
    PostProcessPanel      m_postProcessPanel;

    DebugCamera  m_debugCamera;
    Matrix4x4    m_viewProj;
    Matrix4x4    m_view;
    Matrix4x4    m_proj;

    ImGuizmo::OPERATION     m_gizmoOp       = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE          m_gizmoMode     = ImGuizmo::WORLD;
    bool                    m_gizmoWasUsing = false;
    TransformCommand::State m_gizmoCaptured;

    CommandHistory m_cmdHistory;

    char       m_scenePath[260] = "Resources/Scenes/test.scene.json";
    ImGuiID    m_dockspaceId   = 0;
    uint32_t   m_vpX = 0, m_vpY = 0, m_vpW = 0, m_vpH = 0;
    bool       m_isPlaying        = false;
    bool       m_isPaused         = false;
    bool       m_layoutBuilt      = false;
    bool       m_showDebugShapes  = true;
    bool       m_effectEditOpen  = false;  // large effect-editor window is visible
    Actor*     m_effectEditActor = nullptr;
    float      m_dt            = 0.0f;
    float      m_fpsSmoothed   = 0.0f;
};

} // namespace Fujin
