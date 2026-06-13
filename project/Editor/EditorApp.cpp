#include "EditorApp.h"
#include "Engine/Renderer/SceneRenderer.h"
#include "Engine/Renderer/Material/MaterialManager.h"
#include "Engine/Core/SceneManager.h"
#include "Engine/Core/Actor.h"
#include "Engine/Core/TransformComponent.h"
#include "Engine/Core/CameraComponent.h"
#include "Engine/Core/LightComponent.h"
#include "Engine/Core/ColliderComponent.h"
#include "Engine/Asset/SceneSerializer.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"
#include "ImGuizmo.h"
#include "Editor/Command/ICommand.h"
#include <cmath>
#include <cstdio>
#include <cfloat>
#include <vector>

namespace Fujin {

// ─── Theme ───────────────────────────────────────────────────────────────────

void EditorApp::ApplyUE5Theme() {
    ImGuiStyle& s = ImGui::GetStyle();

    s.WindowRounding     = 2.0f;
    s.ChildRounding      = 2.0f;
    s.FrameRounding      = 3.0f;
    s.PopupRounding      = 3.0f;
    s.GrabRounding       = 3.0f;
    s.TabRounding        = 3.0f;
    s.ScrollbarRounding  = 3.0f;
    s.FramePadding       = { 6.0f, 4.0f };
    s.WindowPadding      = { 8.0f, 8.0f };
    s.ItemSpacing        = { 8.0f, 4.0f };
    s.ItemInnerSpacing   = { 4.0f, 4.0f };
    s.IndentSpacing      = 16.0f;
    s.ScrollbarSize      = 12.0f;
    s.GrabMinSize        = 8.0f;
    s.WindowBorderSize   = 1.0f;
    s.FrameBorderSize    = 0.0f;
    s.TabBorderSize      = 0.0f;

    ImVec4* c = s.Colors;

    const ImVec4 orange    = { 0.878f, 0.482f, 0.224f, 1.00f };
    const ImVec4 orangeMid = { 0.878f, 0.482f, 0.224f, 0.70f };
    const ImVec4 orangeDim = { 0.878f, 0.482f, 0.224f, 0.40f };

    c[ImGuiCol_WindowBg]          = { 0.145f, 0.145f, 0.145f, 1.0f };
    c[ImGuiCol_ChildBg]           = { 0.118f, 0.118f, 0.118f, 1.0f };
    c[ImGuiCol_PopupBg]           = { 0.176f, 0.176f, 0.176f, 0.98f };
    c[ImGuiCol_Border]            = { 0.18f,  0.18f,  0.18f,  1.0f };
    c[ImGuiCol_BorderShadow]      = { 0.0f,   0.0f,   0.0f,   0.0f };
    c[ImGuiCol_Text]              = { 0.906f, 0.906f, 0.906f, 1.0f };
    c[ImGuiCol_TextDisabled]      = { 0.45f,  0.45f,  0.45f,  1.0f };
    c[ImGuiCol_FrameBg]           = { 0.22f,  0.22f,  0.22f,  1.0f };
    c[ImGuiCol_FrameBgHovered]    = { 0.30f,  0.30f,  0.30f,  1.0f };
    c[ImGuiCol_FrameBgActive]     = { 0.35f,  0.35f,  0.35f,  1.0f };
    c[ImGuiCol_TitleBg]           = { 0.094f, 0.094f, 0.094f, 1.0f };
    c[ImGuiCol_TitleBgActive]     = { 0.094f, 0.094f, 0.094f, 1.0f };
    c[ImGuiCol_TitleBgCollapsed]  = { 0.094f, 0.094f, 0.094f, 0.75f };
    c[ImGuiCol_MenuBarBg]         = { 0.12f,  0.12f,  0.12f,  1.0f };
    c[ImGuiCol_ScrollbarBg]       = { 0.12f,  0.12f,  0.12f,  1.0f };
    c[ImGuiCol_ScrollbarGrab]     = { 0.30f,  0.30f,  0.30f,  1.0f };
    c[ImGuiCol_ScrollbarGrabHovered] = { 0.40f, 0.40f, 0.40f, 1.0f };
    c[ImGuiCol_ScrollbarGrabActive]  = { 0.50f, 0.50f, 0.50f, 1.0f };
    c[ImGuiCol_CheckMark]         = orange;
    c[ImGuiCol_SliderGrab]        = orange;
    c[ImGuiCol_SliderGrabActive]  = { 1.0f,   0.60f,  0.30f,  1.0f };
    c[ImGuiCol_Button]            = { 0.26f,  0.26f,  0.26f,  1.0f };
    c[ImGuiCol_ButtonHovered]     = orangeMid;
    c[ImGuiCol_ButtonActive]      = orange;
    c[ImGuiCol_Header]            = orangeDim;
    c[ImGuiCol_HeaderHovered]     = orangeMid;
    c[ImGuiCol_HeaderActive]      = orange;
    c[ImGuiCol_Separator]         = { 0.25f,  0.25f,  0.25f,  1.0f };
    c[ImGuiCol_SeparatorHovered]  = orangeMid;
    c[ImGuiCol_SeparatorActive]   = orange;
    c[ImGuiCol_ResizeGrip]        = { 0.878f, 0.482f, 0.224f, 0.17f };
    c[ImGuiCol_ResizeGripHovered] = { 0.878f, 0.482f, 0.224f, 0.67f };
    c[ImGuiCol_ResizeGripActive]  = orange;
    c[ImGuiCol_Tab]               = { 0.14f,  0.14f,  0.14f,  1.0f };
    c[ImGuiCol_TabHovered]        = orangeMid;
    c[ImGuiCol_TabSelected]       = { 0.20f,  0.20f,  0.20f,  1.0f };
    c[ImGuiCol_TabSelectedOverline]       = orange;
    c[ImGuiCol_TabDimmed]         = { 0.10f,  0.10f,  0.10f,  1.0f };
    c[ImGuiCol_TabDimmedSelected] = { 0.145f, 0.145f, 0.145f, 1.0f };
    c[ImGuiCol_TabDimmedSelectedOverline] = { 0.878f, 0.482f, 0.224f, 0.50f };
    c[ImGuiCol_DockingPreview]    = orangeMid;
    c[ImGuiCol_DockingEmptyBg]    = { 0.118f, 0.118f, 0.118f, 1.0f };
    c[ImGuiCol_NavHighlight]      = orange;
    c[ImGuiCol_NavWindowingHighlight] = { 1.0f, 1.0f, 1.0f, 0.70f };
    c[ImGuiCol_NavWindowingDimBg]     = { 0.80f, 0.80f, 0.80f, 0.20f };
    c[ImGuiCol_ModalWindowDimBg]  = { 0.80f, 0.80f, 0.80f, 0.35f };
    c[ImGuiCol_TableHeaderBg]     = { 0.19f,  0.19f,  0.19f,  1.0f };
    c[ImGuiCol_TableBorderStrong] = { 0.25f,  0.25f,  0.25f,  1.0f };
    c[ImGuiCol_TableBorderLight]  = { 0.20f,  0.20f,  0.20f,  1.0f };
    c[ImGuiCol_TableRowBg]        = { 0.0f,   0.0f,   0.0f,   0.0f };
    c[ImGuiCol_TableRowBgAlt]     = { 1.0f,   1.0f,   1.0f,   0.03f };
}

// ─── Dock Layout ─────────────────────────────────────────────────────────────

void EditorApp::SetupDockLayout(ImGuiID dockspaceId) {
    ImGuiViewport* vp = ImGui::GetMainViewport();

    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId,
        static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_DockSpace) | ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::DockBuilderSetNodeSize(dockspaceId, vp->WorkSize);

    // Split off top toolbar (~4%)
    ImGuiID dockToolbar, dockRest;
    ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Up, 0.04f, &dockToolbar, &dockRest);

    // Split off bottom content browser + render graph (~22%)
    ImGuiID dockBottom, dockMid;
    ImGui::DockBuilderSplitNode(dockRest, ImGuiDir_Down, 0.22f, &dockBottom, &dockMid);

    // Split off left World Outliner (~18%)
    ImGuiID dockLeft, dockCR;
    ImGui::DockBuilderSplitNode(dockMid, ImGuiDir_Left, 0.18f, &dockLeft, &dockCR);

    // Split off right Details (~27% of remaining)
    ImGuiID dockRight, dockCenter;
    ImGui::DockBuilderSplitNode(dockCR, ImGuiDir_Right, 0.27f, &dockRight, &dockCenter);

    // Toolbar node: suppress tab bar and resize handle
    if (ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockToolbar))
        node->LocalFlags |= static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_NoTabBar)
                          | ImGuiDockNodeFlags_NoResize;

    ImGui::DockBuilderDockWindow("##Toolbar",       dockToolbar);
    ImGui::DockBuilderDockWindow("World Outliner",  dockLeft);
    ImGui::DockBuilderDockWindow("Details",         dockRight);
    ImGui::DockBuilderDockWindow("Post Process",    dockRight);
    ImGui::DockBuilderDockWindow("Content Browser", dockBottom);
    ImGui::DockBuilderDockWindow("Render Graph",    dockBottom);
    ImGui::DockBuilderDockWindow("GPU Profiler",    dockBottom);

    ImGui::DockBuilderFinish(dockspaceId);
}

// ─── Toolbar ─────────────────────────────────────────────────────────────────

void EditorApp::DrawToolbar() {
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar
                           | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::Begin("##Toolbar", nullptr, flags);

    const float btnW    = 72.0f;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float totalW  = btnW * 3.0f + spacing * 2.0f;
    const float startX  = (ImGui::GetContentRegionAvail().x - totalW) * 0.5f
                        + ImGui::GetCursorPosX();
    ImGui::SetCursorPosX(startX);

    // Play
    const bool wasPlaying = m_isPlaying;
    if (wasPlaying)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.55f, 0.18f, 1.0f));
    if (ImGui::Button("Play", ImVec2(btnW, 0.0f))) {
        m_isPlaying = true;
        m_isPaused  = false;
    }
    if (wasPlaying)
        ImGui::PopStyleColor();

    ImGui::SameLine();

    // Pause
    ImGui::BeginDisabled(!m_isPlaying);
    const bool wasPaused = m_isPaused;
    if (wasPaused)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.878f, 0.482f, 0.224f, 1.0f));
    if (ImGui::Button("Pause", ImVec2(btnW, 0.0f)))
        m_isPaused = !m_isPaused;
    if (wasPaused)
        ImGui::PopStyleColor();
    ImGui::EndDisabled();

    ImGui::SameLine();

    // Stop
    ImGui::BeginDisabled(!m_isPlaying);
    if (ImGui::Button("Stop", ImVec2(btnW, 0.0f))) {
        m_isPlaying = false;
        m_isPaused  = false;
    }
    ImGui::EndDisabled();

    // FPS counter — right-aligned in toolbar
    char fpsBuf[48];
    snprintf(fpsBuf, sizeof(fpsBuf), "%4.0f FPS   %.2f ms", m_fpsSmoothed, m_dt * 1000.0f);
    float fpsTextW = ImGui::CalcTextSize(fpsBuf).x + 16.0f;
    ImGui::SameLine(ImGui::GetContentRegionMax().x - fpsTextW);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.95f, 0.45f, 1.0f));
    ImGui::TextUnformatted(fpsBuf);
    ImGui::PopStyleColor();

    ImGui::End();
}

// ─── FPS Overlay ─────────────────────────────────────────────────────────────

void EditorApp::DrawFPSOverlay() {
    if (m_vpW == 0 || m_vpH == 0) return;

    // Anchor: top-right of the 3-D viewport with 8 px margin
    float px = static_cast<float>(m_vpX + m_vpW) - 8.0f;
    float py = static_cast<float>(m_vpY) + 8.0f;
    ImGui::SetNextWindowPos(ImVec2(px, py), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.52f);
    ImGui::SetNextWindowSize(ImVec2(0.0f, 0.0f)); // auto-size

    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoDecoration       |
        ImGuiWindowFlags_AlwaysAutoResize   |
        ImGuiWindowFlags_NoSavedSettings    |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav              |
        ImGuiWindowFlags_NoMove             |
        ImGuiWindowFlags_NoInputs           |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoDocking;

    if (ImGui::Begin("##fps_overlay", nullptr, kFlags)) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 1.0f, 0.35f, 1.0f));
        ImGui::Text("%4.0f FPS", m_fpsSmoothed);
        ImGui::PopStyleColor();
        ImGui::TextDisabled("%.2f ms", m_dt * 1000.0f);
    }
    ImGui::End();
}

// ─── GPU Profiler ────────────────────────────────────────────────────────────

void EditorApp::DrawProfiler() {
    if (!m_gfx) return;

    if (ImGui::Begin("GPU Profiler")) {
        GpuProfiler& profiler = m_gfx->GetProfiler();

        // VSync uncaps the frame rate for measurement; GPU timing can be paused too.
        bool vsync = m_gfx->GetVSync();
        if (ImGui::Checkbox("VSync", &vsync)) m_gfx->SetVSync(vsync);
        ImGui::SameLine();
        bool profOn = profiler.IsEnabled();
        if (ImGui::Checkbox("GPU timing", &profOn)) profiler.SetEnabled(profOn);

        ImGui::Text("%.0f FPS", m_fpsSmoothed);
        ImGui::SameLine();
        ImGui::TextDisabled("(CPU %.2f ms)", m_dt * 1000.0f);

        const std::vector<GpuProfiler::Result>& results = profiler.GetResults();

        // Total GPU time = sum of the top-level (depth 0) scopes.
        double totalGpu = 0.0;
        for (const auto& r : results)
            if (r.depth == 0) totalGpu += r.ms;
        ImGui::Text("GPU total: %.3f ms", totalGpu);

        // Rolling sparkline of total GPU ms.
        m_gpuMsHistory[m_gpuMsHistoryPos] = static_cast<float>(totalGpu);
        m_gpuMsHistoryPos = (m_gpuMsHistoryPos + 1) % IM_ARRAYSIZE(m_gpuMsHistory);
        ImGui::PlotLines("##gpuhist", m_gpuMsHistory, IM_ARRAYSIZE(m_gpuMsHistory),
                         m_gpuMsHistoryPos, "GPU ms", 0.0f, FLT_MAX, ImVec2(-1.0f, 48.0f));

        // Per-pass breakdown as a horizontal bar chart (first pass on top).
        if (results.empty()) {
            ImGui::TextDisabled("Enable GPU timing to see per-pass costs.");
        } else {
            std::vector<double>      vals;   vals.reserve(results.size());
            std::vector<double>      ypos;   ypos.reserve(results.size());
            std::vector<const char*> labels; labels.reserve(results.size());
            for (size_t i = 0; i < results.size(); ++i) {
                vals.push_back(results[i].ms);
                ypos.push_back(static_cast<double>(i));
                labels.push_back(results[i].name.c_str());
            }
            const float h = 22.0f * static_cast<float>(results.size()) + 36.0f;
            if (ImPlot::BeginPlot("##passms", ImVec2(-1.0f, h),
                                  ImPlotFlags_NoMouseText | ImPlotFlags_NoLegend)) {
                ImPlot::SetupAxis(ImAxis_X1, "ms",
                                  ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_NoGridLines);
                ImPlot::SetupAxis(ImAxis_Y1, nullptr,
                                  ImPlotAxisFlags_Invert | ImPlotAxisFlags_AutoFit |
                                  ImPlotAxisFlags_NoGridLines);
                ImPlot::SetupAxisTicks(ImAxis_Y1, ypos.data(),
                                       static_cast<int>(ypos.size()), labels.data());
                ImPlotSpec barSpec;
                barSpec.Flags = ImPlotBarsFlags_Horizontal;
                ImPlot::PlotBars("ms", vals.data(), static_cast<int>(vals.size()),
                                 0.6, 0.0, barSpec);
                ImPlot::EndPlot();
            }
        }
    }
    ImGui::End();
}

// ─── Lifecycle ───────────────────────────────────────────────────────────────

bool EditorApp::Initialize(HWND hwnd, GraphicsDevice& gfx, SceneManager& scene) {
    m_scene = &scene;
    m_gfx   = &gfx;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ApplyUE5Theme();

    ImFontConfig fontConfig;
    fontConfig.OversampleH = 1;
    fontConfig.OversampleV = 1;
    fontConfig.PixelSnapH  = true;
    ImFont* fontJapanese = io.Fonts->AddFontFromFileTTF(
        "Externals/imgui/MPLUS1p-Medium.ttf", 18.0f, &fontConfig,
        io.Fonts->GetGlyphRangesJapanese());
    if (fontJapanese)
        io.FontDefault = fontJapanese;

    ImGui_ImplWin32_Init(hwnd);

    ImGui_ImplDX12_InitInfo dx12Info = {};
    dx12Info.Device             = gfx.GetDevice();
    dx12Info.CommandQueue       = gfx.GetCommandQueue();
    dx12Info.NumFramesInFlight  = NUM_FRAMES_IN_FLIGHT;
    dx12Info.RTVFormat          = DXGI_FORMAT_R8G8B8A8_UNORM;
    dx12Info.SrvDescriptorHeap  = gfx.GetSRVHeap();
    dx12Info.LegacySingleSrvCpuDescriptor = gfx.GetSRVHeap()->GetCPUDescriptorHandleForHeapStart();
    dx12Info.LegacySingleSrvGpuDescriptor = gfx.GetSRVHeap()->GetGPUDescriptorHandleForHeapStart();
    ImGui_ImplDX12_Init(&dx12Info);

    m_renderPassPanel.Initialize();

    m_inspectorPanel.SetCommandHistory(&m_cmdHistory);

    // Wire up the "Edit Effect" callback so InspectorPanel can open the full editor
    m_inspectorPanel.SetEditEffectCallback([this](Actor* a) {
        m_effectEditActor = a;
        m_effectEditOpen  = true;
    });

    return true;
}

void EditorApp::SetMaterialManager(MaterialManager* mgr) {
    m_inspectorPanel.SetMaterialManager(mgr);
}

void EditorApp::SetSceneRenderer(SceneRenderer* renderer) {
    m_sceneRenderer = renderer;
}

void EditorApp::Shutdown() {
    m_renderPassPanel.Shutdown();
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
}

bool EditorApp::IsViewportHovered() const {
    ImVec2 mouse = ImGui::GetMousePos();
    return mouse.x >= m_vpX && mouse.x < m_vpX + m_vpW &&
           mouse.y >= m_vpY && mouse.y < m_vpY + m_vpH;
}

void EditorApp::BeginFrame(float dt) {
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();

    // FPS tracking (exponential moving average, τ ≈ 10 frames)
    m_dt = dt;
    if (dt > 0.0001f)
        m_fpsSmoothed = m_fpsSmoothed * 0.9f + (1.0f / dt) * 0.1f;

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    m_dockspaceId = ImGui::DockSpaceOverViewport(
        0, viewport, ImGuiDockNodeFlags_PassthruCentralNode);

    // Always rebuild layout once per process lifetime so every new panel
    // gets a dock slot even if imgui.ini was saved before it existed.
    if (!m_layoutBuilt) {
        SetupDockLayout(m_dockspaceId);
        m_layoutBuilt = true;
    }

    // Cache central node rect for SceneRenderer (used next frame)
    if (ImGuiDockNode* cn = ImGui::DockBuilderGetCentralNode(m_dockspaceId)) {
        m_vpX = static_cast<uint32_t>(cn->Pos.x);
        m_vpY = static_cast<uint32_t>(cn->Pos.y);
        m_vpW = static_cast<uint32_t>(cn->Size.x);
        m_vpH = static_cast<uint32_t>(cn->Size.y);
    }

    // Update debug camera — must happen after viewport rect is updated
    m_debugCamera.Update(dt, IsViewportHovered(),
        static_cast<float>(m_vpX), static_cast<float>(m_vpY),
        static_cast<float>(m_vpW), static_cast<float>(m_vpH));

    // Menu bar
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Save Scene"))
                SceneSerializer::Save(*m_scene, m_scenePath);
            if (ImGui::MenuItem("Load Scene")) {
                SceneSerializer::Load(*m_scene, m_scenePath);
                OnSceneReplaced();   // clear cached actor pointers (selection etc.) for the new scene
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit"))
                PostQuitMessage(0);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Reset Layout"))
                m_layoutBuilt = false;  // triggers SetupDockLayout next frame
            ImGui::Separator();
            ImGui::MenuItem("Debug Shapes", nullptr, &m_showDebugShapes);
            // Off by default: only the selected actor draws gizmos, so a scene with many
            // lights/colliders doesn't spend thousands of projected AddLine calls every frame.
            ImGui::MenuItem("Show All Gizmos", nullptr, &m_showAllGizmos);
            ImGui::EndMenu();
        }
        ImGui::SetNextItemWidth(200.0f);
        ImGui::InputText("##path", m_scenePath, sizeof(m_scenePath));
        ImGui::EndMainMenuBar();
    }

    // Undo / Redo
    ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false))
        m_cmdHistory.Undo();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false))
        m_cmdHistory.Redo();

    // The selected actor may have been destroyed since last frame — a gameplay actor despawned
    // during Play, or the GameMode's spawned PlayerController torn down on Stop. m_selected is a raw
    // pointer, so clear a stale selection (by scanning the live actor list, never dereferencing the
    // dangling pointer) before any panel reads it — the same guard m_effectEditActor uses below.
    if (Actor* sel = m_hierarchyPanel.GetSelectedActor()) {
        bool alive = false;
        for (auto& a : m_scene->GetActors())
            if (a.get() == sel) { alive = true; break; }
        if (!alive) m_hierarchyPanel.SetSelectedActor(nullptr);
    }

    DrawToolbar();
    DrawFPSOverlay();
    if (!m_effectEditOpen)
        m_hierarchyPanel.Draw(*m_scene);
    m_inspectorPanel.Draw(m_hierarchyPanel.GetSelectedActor());
    m_contentBrowserPanel.Draw();
    m_postProcessPanel.Draw(m_sceneRenderer ? &m_sceneRenderer->GetPostProcess() : nullptr);
    DrawProfiler();
    // Must be drawn in the normal UI flow (not in Render()) — the node-editor
    // canvas mispositions if drawn after ImGuizmo/gizmo foreground draw lists.
    if (m_sceneRenderer)
        m_renderPassPanel.Draw(m_sceneRenderer->GetRenderGraph());

    // Effect Editor is only shown when opened from the "Edit Effect" button in Details.
    if (m_effectEditOpen && m_effectEditActor) {
        bool still = false;
        for (auto& a : m_scene->GetActors())
            if (a.get() == m_effectEditActor) { still = true; break; }
        if (!still) { m_effectEditOpen = false; m_effectEditActor = nullptr; }
        else        { m_effectPanel.DrawFull(m_effectEditActor, m_effectEditOpen); }
    }
}

// ─── Gizmo Helpers ───────────────────────────────────────────────────────────

Vector3 EditorApp::ExtractScale(const Matrix4x4& m) {
    return Vector3(
        std::sqrt(m.m[0][0]*m.m[0][0] + m.m[0][1]*m.m[0][1] + m.m[0][2]*m.m[0][2]),
        std::sqrt(m.m[1][0]*m.m[1][0] + m.m[1][1]*m.m[1][1] + m.m[1][2]*m.m[1][2]),
        std::sqrt(m.m[2][0]*m.m[2][0] + m.m[2][1]*m.m[2][1] + m.m[2][2]*m.m[2][2])
    );
}

Quaternion EditorApp::ExtractRotation(const Matrix4x4& m, const Vector3& scale) {
    Matrix4x4 rot;
    if (scale.x > 1e-6f) { rot.m[0][0] = m.m[0][0]/scale.x; rot.m[0][1] = m.m[0][1]/scale.x; rot.m[0][2] = m.m[0][2]/scale.x; }
    if (scale.y > 1e-6f) { rot.m[1][0] = m.m[1][0]/scale.y; rot.m[1][1] = m.m[1][1]/scale.y; rot.m[1][2] = m.m[1][2]/scale.y; }
    if (scale.z > 1e-6f) { rot.m[2][0] = m.m[2][0]/scale.z; rot.m[2][1] = m.m[2][1]/scale.z; rot.m[2][2] = m.m[2][2]/scale.z; }
    return Quaternion::FromMatrix(rot);
}

// ─── Transform Gizmo ─────────────────────────────────────────────────────────

void EditorApp::DrawGizmo() {
    if (m_vpW == 0 || m_vpH == 0) return;

    // ── モード切り替えオーバーレイ（ビューポート左上） ──────────────────────
    {
        const float pad = 8.0f;
        ImGui::SetNextWindowPos(ImVec2(static_cast<float>(m_vpX) + pad,
                                       static_cast<float>(m_vpY) + pad),
                                ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.72f);
        constexpr ImGuiWindowFlags kOvFlags =
            ImGuiWindowFlags_NoDecoration      | ImGuiWindowFlags_AlwaysAutoResize  |
            ImGuiWindowFlags_NoSavedSettings   | ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoNav             | ImGuiWindowFlags_NoMove            |
            ImGuiWindowFlags_NoDocking         | ImGuiWindowFlags_NoBringToFrontOnFocus;

        ImGui::Begin("##gizmo_mode", nullptr, kOvFlags);

        auto ModeBtn = [&](const char* label, ImGuizmo::OPERATION op) {
            bool active = (m_gizmoOp == op);
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.878f, 0.482f, 0.224f, 1.0f));
            if (ImGui::Button(label, ImVec2(52.0f, 0.0f))) m_gizmoOp = op;
            if (active) ImGui::PopStyleColor();
            ImGui::SameLine(0.0f, 2.0f);
        };

        ModeBtn("Move",   ImGuizmo::TRANSLATE);
        ModeBtn("Rotate", ImGuizmo::ROTATE);
        ModeBtn("Scale",  ImGuizmo::SCALE);

        // Local / World トグル（Scaleはローカルのみ）
        if (m_gizmoOp != ImGuizmo::SCALE) {
            ImGui::SameLine(0.0f, 8.0f);
            bool isWorld = (m_gizmoMode == ImGuizmo::WORLD);
            if (isWorld) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.6f, 0.9f, 1.0f));
            if (ImGui::Button(isWorld ? "World" : "Local", ImVec2(46.0f, 0.0f)))
                m_gizmoMode = isWorld ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
            if (isWorld) ImGui::PopStyleColor();
        }

        // Debug shapes toggle
        ImGui::SameLine(0.0f, 12.0f);
        bool shapesWasOn = m_showDebugShapes;
        if (shapesWasOn) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.55f, 0.25f, 1.0f));
        if (ImGui::Button("Shapes", ImVec2(52.0f, 0.0f)))
            m_showDebugShapes = !m_showDebugShapes;
        if (shapesWasOn) ImGui::PopStyleColor();

        ImGui::End();
    }

    Actor* sel = m_hierarchyPanel.GetSelectedActor();
    if (!sel) { m_gizmoWasUsing = false; return; }
    auto* tc = sel->GetComponent<TransformComponent>();
    if (!tc) { m_gizmoWasUsing = false; return; }

    // Disable while right-click camera drag is active
    if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        m_gizmoWasUsing = false;
        return;
    }

    ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList());
    ImGuizmo::SetRect(
        static_cast<float>(m_vpX), static_cast<float>(m_vpY),
        static_cast<float>(m_vpW), static_cast<float>(m_vpH));
    ImGuizmo::AllowAxisFlip(false);

    // Our matrices are row-major; ImGuizmo expects column-major → transpose
    Matrix4x4 viewT = m_view.GetTransposed();
    Matrix4x4 projT = m_proj.GetTransposed();
    Matrix4x4 world  = tc->GetWorldMatrix();
    Matrix4x4 worldT = world.GetTransposed();

    bool wasUsing = ImGuizmo::IsUsing();

    // Capture state when the user starts dragging
    if (!wasUsing && ImGuizmo::IsOver()) {
        m_gizmoCaptured = { tc->Position, tc->Rotation, tc->Scale };
    }

    bool manipulated = ImGuizmo::Manipulate(
        viewT.v, projT.v,
        m_gizmoOp, m_gizmoMode,
        worldT.v);

    if (manipulated) {
        // Transpose back to row-major
        Matrix4x4 newWorld = worldT.GetTransposed();

        Vector3    newPos   = Vector3(newWorld.m[0][3], newWorld.m[1][3], newWorld.m[2][3]);
        Vector3    newScale = ExtractScale(newWorld);
        Quaternion newRot   = ExtractRotation(newWorld, newScale);

        // Convert world → local when actor has a parent
        if (sel->GetParent()) {
            auto* parentTc = sel->GetParent()->GetComponent<TransformComponent>();
            if (parentTc) {
                Matrix4x4 localMat = parentTc->GetWorldMatrix().GetInverse() * newWorld;
                newPos   = Vector3(localMat.m[0][3], localMat.m[1][3], localMat.m[2][3]);
                newScale = ExtractScale(localMat);
                newRot   = ExtractRotation(localMat, newScale);
            }
        }

        tc->Position = newPos;
        tc->Rotation = newRot;
        tc->Scale    = newScale;
        m_inspectorPanel.InvalidateEulerCache(sel->GetId());
    }

    // Push undo command when manipulation ends
    bool isUsing = ImGuizmo::IsUsing();
    if (wasUsing && !isUsing) {
        TransformCommand::State newState = { tc->Position, tc->Rotation, tc->Scale };
        // Push without re-Execute: wrap so Execute() is a no-op on first call
        struct GizmoCmd : public ICommand {
            TransformComponent*     tc;
            TransformCommand::State before, after;
            bool firstExec = true;
            GizmoCmd(TransformComponent* t, const TransformCommand::State& b, const TransformCommand::State& a)
                : tc(t), before(b), after(a) {}
            void Execute() override {
                if (firstExec) { firstExec = false; return; } // state already applied
                if (tc) { tc->Position = after.position; tc->Rotation = after.rotation; tc->Scale = after.scale; }
            }
            void Undo() override {
                if (tc) { tc->Position = before.position; tc->Rotation = before.rotation; tc->Scale = before.scale; }
            }
        };
        m_cmdHistory.Push(std::make_unique<GizmoCmd>(tc, m_gizmoCaptured, newState));
    }
    m_gizmoWasUsing = isUsing;
}

// ─── Debug Shape Overlay ──────────────────────────────────────────────────────

void EditorApp::DrawDebugShapes() {
    if (!m_scene || m_vpW == 0 || m_vpH == 0) return;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->PushClipRect(
        ImVec2(static_cast<float>(m_vpX),           static_cast<float>(m_vpY)),
        ImVec2(static_cast<float>(m_vpX + m_vpW),   static_cast<float>(m_vpY + m_vpH)),
        true);

    // Project world-space point → screen pixel. Returns false if behind camera.
    auto Project = [&](const Vector3& p, float& sx, float& sy) -> bool {
        Vector4 clip = m_viewProj * Vector4(p.x, p.y, p.z, 1.0f);
        if (clip.w <= 0.001f) return false;
        float ndcX =  clip.x / clip.w;
        float ndcY = -clip.y / clip.w;
        sx = (ndcX * 0.5f + 0.5f) * static_cast<float>(m_vpW) + static_cast<float>(m_vpX);
        sy = (ndcY * 0.5f + 0.5f) * static_cast<float>(m_vpH) + static_cast<float>(m_vpY);
        return true;
    };

    auto Line = [&](const Vector3& a, const Vector3& b, ImU32 col, float t = 1.0f) {
        float ax, ay, bx, by;
        if (Project(a, ax, ay) && Project(b, bx, by))
            dl->AddLine(ImVec2(ax, ay), ImVec2(bx, by), col, t);
    };

    // Oriented box wireframe (12 edges); rotation matches the physics OBB.
    auto BoxWire = [&](const Vector3& c, const Vector3& h, const Quaternion& rot, ImU32 col) {
        Vector3 v[8];
        for (int i = 0; i < 8; ++i)
            v[i] = c + rot * Vector3((i & 1) ? h.x : -h.x, (i & 2) ? h.y : -h.y, (i & 4) ? h.z : -h.z);
        constexpr int E[12][2] = {
            {0,1},{2,3},{4,5},{6,7}, {0,2},{1,3},{4,6},{5,7}, {0,4},{1,5},{2,6},{3,7}
        };
        for (auto& e : E) Line(v[e[0]], v[e[1]], col);
    };

    // Circle in world-space on plane defined by two orthogonal axis vectors
    auto Circle = [&](const Vector3& center, float r,
                      const Vector3& axA, const Vector3& axB,
                      ImU32 col, int segs = 24) {
        constexpr float TAU = 6.28318530f;
        Vector3 prev = center + axA * r;
        for (int i = 1; i <= segs; ++i) {
            float a = (float)i / segs * TAU;
            Vector3 cur = center + axA * (r * std::cosf(a)) + axB * (r * std::sinf(a));
            Line(prev, cur, col);
            prev = cur;
        }
    };

    // 180° arc: axA×axB plane, sweeping from +axA toward +axB
    auto Arc = [&](const Vector3& center, float r,
                   const Vector3& axA, const Vector3& axB,
                   ImU32 col, int segs = 12) {
        constexpr float PI = 3.14159265f;
        Vector3 prev = center + axA * r;
        for (int i = 1; i <= segs; ++i) {
            float a = (float)i / segs * PI;
            Vector3 cur = center + axA * (r * std::cosf(a)) + axB * (r * std::sinf(a));
            Line(prev, cur, col);
            prev = cur;
        }
    };

    // By default only the selected actor draws its gizmos; "Show All Gizmos" (View menu) opts into
    // drawing every actor's, which is the expensive path (each line is two world→screen projects).
    Actor* selected = m_hierarchyPanel.GetSelectedActor();

    for (auto& actorPtr : m_scene->GetActors()) {
        if (!m_showAllGizmos && actorPtr.get() != selected) continue;

        auto* tc = actorPtr->GetComponent<TransformComponent>();
        if (!tc) continue;

        // ── Collider (drawn in world space with scale baked in — matches the physics shape) ──
        if (auto* col = actorPtr->GetComponent<ColliderComponent>()) {
            ImU32 cc = col->IsTrigger ? IM_COL32(0, 220, 220, 200) : IM_COL32(90, 220, 90, 200);
            const Transform& w = tc->CachedWorld;
            Vector3 as(std::fabsf(w.Scale.x), std::fabsf(w.Scale.y), std::fabsf(w.Scale.z));
            Vector3 center = w.TransformPoint(col->Offset);
            // std::max is shadowed by the Windows 'max' macro in this translation unit.
            float sMax  = as.x > as.y ? as.x : as.y; sMax = sMax > as.z ? sMax : as.z;
            float sMaxXZ = as.x > as.z ? as.x : as.z;

            switch (col->Shape) {
            case ColliderShape::AABB: {
                Vector3 he(col->HalfExtents.x * as.x, col->HalfExtents.y * as.y, col->HalfExtents.z * as.z);
                BoxWire(center, he, w.Rotation, cc);
                break;
            }
            case ColliderShape::Sphere: {
                float rad = col->Radius * sMax;
                Circle(center, rad, {1,0,0}, {0,1,0}, cc);
                Circle(center, rad, {1,0,0}, {0,0,1}, cc);
                Circle(center, rad, {0,1,0}, {0,0,1}, cc);
                break;
            }
            case ColliderShape::Capsule: {
                float rad = col->Radius     * sMaxXZ;
                float hh  = col->HalfHeight  * as.y;
                Vector3 up    = w.Rotation * Vector3(0, 1, 0);
                Vector3 right = w.Rotation * Vector3(1, 0, 0);
                Vector3 fwd   = w.Rotation * Vector3(0, 0, 1);
                Vector3 base = center - up * hh;
                Vector3 tip  = center + up * hh;
                // cylinder rings at the cap boundaries
                Circle(base, rad, right, fwd, cc);
                Circle(tip,  rad, right, fwd, cc);
                // cylinder side lines
                Line(base + right * rad, tip + right * rad, cc);
                Line(base - right * rad, tip - right * rad, cc);
                Line(base + fwd   * rad, tip + fwd   * rad, cc);
                Line(base - fwd   * rad, tip - fwd   * rad, cc);
                // hemispherical caps (physics extends Radius beyond base/tip)
                Arc(tip,  rad, right,  up, cc);
                Arc(tip,  rad, fwd,    up, cc);
                Arc(base, rad, right, -up, cc);
                Arc(base, rad, fwd,   -up, cc);
                break;
            }
            }
        }

        // ── Light ─────────────────────────────────────────────────────────
        if (auto* lc = actorPtr->GetComponent<LightComponent>()) {
            Vector3 pos = tc->Position;
            ImU32 lc32  = IM_COL32(255, 220, 60, 210);

            // Use the same row-based extraction as LightingPass.cpp:243
            // so the arrow matches the actual rendered light direction.
            Matrix4x4 rot = tc->Rotation.ToMatrix();
            Vector3 fwd  (rot.m[2][0], rot.m[2][1], rot.m[2][2]);
            Vector3 right(rot.m[0][0], rot.m[0][1], rot.m[0][2]);
            Vector3 up   (rot.m[1][0], rot.m[1][1], rot.m[1][2]);

            switch (lc->Type) {
            case LightType::Point:
                Circle(pos, lc->Range, {1,0,0}, {0,1,0}, lc32, 32);
                Circle(pos, lc->Range, {1,0,0}, {0,0,1}, lc32, 32);
                Circle(pos, lc->Range, {0,1,0}, {0,0,1}, lc32, 32);
                break;

            case LightType::Directional: {
                // Arrow + 4-point arrowhead
                Vector3 tip = pos + fwd * 1.5f;
                Line(pos, tip, lc32, 2.0f);
                float hs = 0.2f;
                Line(tip, tip - fwd * 0.4f + right * hs, lc32, 2.0f);
                Line(tip, tip - fwd * 0.4f - right * hs, lc32, 2.0f);
                Line(tip, tip - fwd * 0.4f + up    * hs, lc32, 2.0f);
                Line(tip, tip - fwd * 0.4f - up    * hs, lc32, 2.0f);
                break;
            }

            case LightType::Spot: {
                float halfRad = lc->SpotAngle * 0.5f * (3.14159265f / 180.0f);
                float cr      = lc->Range * std::tanf(halfRad);
                Vector3 cEnd  = pos + fwd * lc->Range;
                Circle(cEnd, cr, right, up, lc32, 24);
                Line(pos, cEnd + right * cr, lc32);
                Line(pos, cEnd - right * cr, lc32);
                Line(pos, cEnd + up    * cr, lc32);
                Line(pos, cEnd - up    * cr, lc32);
                break;
            }
            }
        }
    }

    dl->PopClipRect();
}

// ─── Camera Gizmos ───────────────────────────────────────────────────────────

void EditorApp::DrawCameraGizmos() {
    if (!m_scene || m_vpW == 0 || m_vpH == 0) return;

    // Project a world-space point to viewport-relative screen pixels.
    // Returns false if the point is behind the camera.
    auto Project = [&](const Vector3& worldPos, float& sx, float& sy) -> bool {
        Vector4 clip = m_viewProj * Vector4(worldPos.x, worldPos.y, worldPos.z, 1.0f);
        if (clip.w <= 0.001f) return false;
        float ndcX =  clip.x / clip.w;
        float ndcY = -clip.y / clip.w; // DX NDC: +Y up, screen: +Y down
        sx = (ndcX * 0.5f + 0.5f) * static_cast<float>(m_vpW) + static_cast<float>(m_vpX);
        sy = (ndcY * 0.5f + 0.5f) * static_cast<float>(m_vpH) + static_cast<float>(m_vpY);
        return true;
    };

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImU32 colBody  = IM_COL32(255, 220,  60, 220);
    const ImU32 colFrustum = IM_COL32(255, 220,  60, 100);
    const ImU32 colText  = IM_COL32(255, 240, 120, 255);

    for (auto& actorPtr : m_scene->GetActors()) {
        auto* cam = actorPtr->GetComponent<CameraComponent>();
        if (!cam) continue;

        auto* tc = actorPtr->GetComponent<TransformComponent>();
        Vector3 camWorldPos = tc ? tc->GetWorldMatrix().GetTranslation() : Vector3(0,0,0);

        float sx, sy;
        if (!Project(camWorldPos, sx, sy)) continue;

        // Camera body: small filled square
        const float halfW = 10.0f, halfH = 7.0f;
        dl->AddRectFilled(ImVec2(sx - halfW, sy - halfH),
                          ImVec2(sx + halfW, sy + halfH), colBody, 2.0f);

        // Lens triangle (pointing in screen-right approximation of forward)
        // Build frustum near-plane corners in world space using camera rotation
        if (tc) {
            Matrix4x4 rotMat = tc->Rotation.ToMatrix();
            // Forward (+Z column of rotation matrix in this left-hand convention)
            Vector3 fwd(rotMat.m[0][2], rotMat.m[1][2], rotMat.m[2][2]);
            Vector3 up (rotMat.m[0][1], rotMat.m[1][1], rotMat.m[2][1]);
            Vector3 right(rotMat.m[0][0], rotMat.m[1][0], rotMat.m[2][0]);

            const float nearDist = cam->NearClip * 30.0f; // scaled for visibility
            const float fovRad   = cam->FOV * 3.14159f / 180.0f;
            const float aspect   = (m_vpH > 0) ? static_cast<float>(m_vpW) / static_cast<float>(m_vpH) : 1.0f;
            const float nh = std::tan(fovRad * 0.5f) * nearDist;
            const float nw = nh * aspect;

            Vector3 center = { camWorldPos.x + fwd.x * nearDist,
                                camWorldPos.y + fwd.y * nearDist,
                                camWorldPos.z + fwd.z * nearDist };
            Vector3 corners[4] = {
                { center.x + right.x*nw + up.x*nh, center.y + right.y*nw + up.y*nh, center.z + right.z*nw + up.z*nh },
                { center.x - right.x*nw + up.x*nh, center.y - right.y*nw + up.y*nh, center.z - right.z*nw + up.z*nh },
                { center.x - right.x*nw - up.x*nh, center.y - right.y*nw - up.y*nh, center.z - right.z*nw - up.z*nh },
                { center.x + right.x*nw - up.x*nh, center.y + right.y*nw - up.y*nh, center.z + right.z*nw - up.z*nh },
            };

            float cx[4], cy[4];
            bool visible[4];
            for (int i = 0; i < 4; ++i)
                visible[i] = Project(corners[i], cx[i], cy[i]);

            // Near-plane quad
            for (int i = 0; i < 4; ++i) {
                int j = (i + 1) % 4;
                if (visible[i] && visible[j])
                    dl->AddLine(ImVec2(cx[i], cy[i]), ImVec2(cx[j], cy[j]), colFrustum, 1.5f);
            }
            // Lines from camera body to near-plane corners
            for (int i = 0; i < 4; ++i) {
                if (visible[i])
                    dl->AddLine(ImVec2(sx, sy), ImVec2(cx[i], cy[i]), colFrustum, 1.0f);
            }
        }

        // Label
        std::string label = actorPtr->GetName() + " [Camera]";
        dl->AddText(ImVec2(sx + 14.0f, sy - 8.0f), colText, label.c_str());
    }
}

void EditorApp::Render(ID3D12GraphicsCommandList* cmdList) {
    DrawGizmo();
    if (m_showDebugShapes) DrawDebugShapes();
    DrawCameraGizmos();
    ImGui::Render();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmdList);
}

} // namespace Fujin
