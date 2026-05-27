#include "DebugCamera.h"
#include "imgui.h"
#include <cmath>

namespace Fujin {

static constexpr float PITCH_LIMIT = 1.55f; // ~89 degrees

Vector3 DebugCamera::Forward() const {
    return Vector3(
        std::sin(m_yaw) * std::cos(m_pitch),
        std::sin(-m_pitch),
        std::cos(m_yaw) * std::cos(m_pitch));
}

Vector3 DebugCamera::Right() const {
    // right = forward × worldUp
    Vector3 f = Forward();
    Vector3 up(0.0f, 1.0f, 0.0f);
    return Vector3(
        f.z * up.y - f.y * up.z,   // cross product (f × up)... actually (up × f) for right-hand
        f.x * up.z - f.z * up.x,
        f.y * up.x - f.x * up.y).GetSafeNormal();
}

Vector3 DebugCamera::GetTarget() const {
    Vector3 f = Forward();
    return Vector3(m_pos.x + f.x, m_pos.y + f.y, m_pos.z + f.z);
}

void DebugCamera::Update(float dt, bool viewportHovered,
                          float /*vpX*/, float /*vpY*/, float /*vpW*/, float /*vpH*/) {
    ImGuiIO& io = ImGui::GetIO();
    (void)io; // used for MouseDelta and MouseWheel below

    const bool rmbDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    const bool mmbDown = ImGui::IsMouseDown(ImGuiMouseButton_Middle);

    // Only capture input when viewport is hovered or we're already dragging
    if (!viewportHovered && !rmbDown && !mmbDown) return;

    // RMB: look + fly
    if (rmbDown) {
        // Capture mouse (Unreal: RMB drag rotates the view)
        const ImVec2 delta = io.MouseDelta;
        m_yaw   += delta.x * RotateSpeed;
        m_pitch += delta.y * RotateSpeed;
        m_pitch = (std::max)(-PITCH_LIMIT, (std::min)(PITCH_LIMIT, m_pitch));

        // WASD fly
        const bool sprint = ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift);
        const float speed = MoveSpeed * (sprint ? 4.0f : 1.0f) * dt;
        Vector3 fwd   = Forward();
        Vector3 right = Right();

        if (ImGui::IsKeyDown(ImGuiKey_W)) {
            m_pos.x += fwd.x * speed; m_pos.y += fwd.y * speed; m_pos.z += fwd.z * speed;
        }
        if (ImGui::IsKeyDown(ImGuiKey_S)) {
            m_pos.x -= fwd.x * speed; m_pos.y -= fwd.y * speed; m_pos.z -= fwd.z * speed;
        }
        if (ImGui::IsKeyDown(ImGuiKey_A)) {
            m_pos.x -= right.x * speed; m_pos.y -= right.y * speed; m_pos.z -= right.z * speed;
        }
        if (ImGui::IsKeyDown(ImGuiKey_D)) {
            m_pos.x += right.x * speed; m_pos.y += right.y * speed; m_pos.z += right.z * speed;
        }
        if (ImGui::IsKeyDown(ImGuiKey_E)) {
            m_pos.y += speed;
        }
        if (ImGui::IsKeyDown(ImGuiKey_Q)) {
            m_pos.y -= speed;
        }
    }

    // MMB: pan
    if (mmbDown && !rmbDown) {
        const ImVec2 delta = io.MouseDelta;
        Vector3 right = Right();
        Vector3 up(0.0f, 1.0f, 0.0f);
        const float panSpeed = MoveSpeed * 0.01f;
        m_pos.x -= (right.x * delta.x - up.x * delta.y) * panSpeed;
        m_pos.y -= (right.y * delta.x - up.y * delta.y) * panSpeed;
        m_pos.z -= (right.z * delta.x - up.z * delta.y) * panSpeed;
    }

    // Scroll wheel: dolly forward/backward (only when hovered, no button required)
    if (viewportHovered && io.MouseWheel != 0.0f) {
        Vector3 fwd = Forward();
        const float dolly = io.MouseWheel * MoveSpeed * 0.5f;
        m_pos.x += fwd.x * dolly; m_pos.y += fwd.y * dolly; m_pos.z += fwd.z * dolly;
    }
}

} // namespace Fujin
