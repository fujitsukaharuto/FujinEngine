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
#include "imgui.h"

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

    void SetViewProj(const Matrix4x4& vp) { m_viewProj = vp; }

private:
    void ApplyUE5Theme();
    void SetupDockLayout(ImGuiID dockspaceId);
    void DrawToolbar();
    void DrawFPSOverlay();
    void DrawCameraGizmos();
    bool IsViewportHovered() const;

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

    char       m_scenePath[260] = "Resources/Scenes/test.scene.json";
    ImGuiID    m_dockspaceId   = 0;
    uint32_t   m_vpX = 0, m_vpY = 0, m_vpW = 0, m_vpH = 0;
    bool       m_isPlaying       = false;
    bool       m_isPaused        = false;
    bool       m_layoutBuilt     = false;  // rebuilt every startup
    bool       m_effectEditOpen  = false;  // large effect-editor window is visible
    Actor*     m_effectEditActor = nullptr;
    float      m_dt            = 0.0f;
    float      m_fpsSmoothed   = 0.0f;
};

} // namespace Fujin
