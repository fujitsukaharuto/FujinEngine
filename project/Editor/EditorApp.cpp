#include "EditorApp.h"
#include "Engine/Renderer/SceneRenderer.h"
#include "Engine/Renderer/Material/MaterialManager.h"
#include "Engine/Core/SceneManager.h"
#include "Engine/Core/Actor.h"
#include "Engine/Core/TransformComponent.h"
#include "Engine/Core/CameraComponent.h"
#include "Engine/Asset/SceneSerializer.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"
#include <cmath>
#include <cstdio>

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

// ─── Lifecycle ───────────────────────────────────────────────────────────────

bool EditorApp::Initialize(HWND hwnd, GraphicsDevice& gfx, SceneManager& scene) {
    m_scene = &scene;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

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
            if (ImGui::MenuItem("Load Scene"))
                SceneSerializer::Load(*m_scene, m_scenePath);
            ImGui::Separator();
            if (ImGui::MenuItem("Exit"))
                PostQuitMessage(0);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Reset Layout"))
                m_layoutBuilt = false;  // triggers SetupDockLayout next frame
            ImGui::EndMenu();
        }
        ImGui::SetNextItemWidth(200.0f);
        ImGui::InputText("##path", m_scenePath, sizeof(m_scenePath));
        ImGui::EndMainMenuBar();
    }

    DrawToolbar();
    DrawFPSOverlay();
    if (!m_effectEditOpen)
        m_hierarchyPanel.Draw(*m_scene);
    m_inspectorPanel.Draw(m_hierarchyPanel.GetSelectedActor());
    m_contentBrowserPanel.Draw();
    m_postProcessPanel.Draw(m_sceneRenderer ? &m_sceneRenderer->GetPostProcess() : nullptr);

    // Effect Editor is only shown when opened from the "Edit Effect" button in Details.
    if (m_effectEditOpen && m_effectEditActor) {
        bool still = false;
        for (auto& a : m_scene->GetActors())
            if (a.get() == m_effectEditActor) { still = true; break; }
        if (!still) { m_effectEditOpen = false; m_effectEditActor = nullptr; }
        else        { m_effectPanel.DrawFull(m_effectEditActor, m_effectEditOpen); }
    }
}

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
    DrawCameraGizmos();
    if (m_sceneRenderer)
        m_renderPassPanel.Draw(m_sceneRenderer->GetRenderGraph());
    ImGui::Render();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmdList);
}

} // namespace Fujin
