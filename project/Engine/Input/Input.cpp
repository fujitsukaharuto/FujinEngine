#include "Input.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Xinput.h>
#include <algorithm>
#include <cmath>

#pragma comment(lib, "xinput.lib")

namespace Fujin {

Input& Input::Get() {
    static Input s_instance;
    return s_instance;
}

void Input::Initialize(void* hwnd) {
    m_hwnd = hwnd;
}

void Input::Shutdown() {
    SetVibration(0.0f, 0.0f);
    m_hwnd = nullptr;
}

// Normalize a raw XInput stick axis [-32768,32767] with a dead zone, into [-1,1].
static float NormalizeStick(SHORT v, SHORT deadZone) {
    float f  = (float)v;
    float dz = (float)deadZone;
    if (f >  dz) return std::min((f - dz) / (32767.0f - dz), 1.0f);
    if (f < -dz) return std::max((f + dz) / (32767.0f - dz), -1.0f);
    return 0.0f;
}

void Input::Update() {
    // ── Keyboard + mouse buttons (Win32 async state; mouse buttons share the VK space) ──
    std::copy(std::begin(m_keyCur), std::end(m_keyCur), std::begin(m_keyPrev));
    for (int vk = 1; vk < 256; ++vk)
        m_keyCur[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;

    // ── Mouse position + delta (client space) ──
    POINT p;
    if (m_hwnd && GetCursorPos(&p) && ScreenToClient((HWND)m_hwnd, &p)) {
        float x = (float)p.x, y = (float)p.y;
        if (m_haveMouse) { m_mouseDX = x - m_prevMouseX; m_mouseDY = y - m_prevMouseY; }
        else             { m_mouseDX = 0.0f; m_mouseDY = 0.0f; m_haveMouse = true; }
        m_mouseX = x; m_mouseY = y; m_prevMouseX = x; m_prevMouseY = y;
    } else {
        m_mouseDX = m_mouseDY = 0.0f;
    }
    m_wheel = m_wheelAccum; m_wheelAccum = 0.0f;

    // ── Gamepad (XInput player 0) ──
    m_padPrev = m_padCur;
    XINPUT_STATE st = {};
    if (XInputGetState(0, &st) == ERROR_SUCCESS) {
        m_padConnected = true;
        m_padCur = st.Gamepad.wButtons;
        m_padLX = NormalizeStick(st.Gamepad.sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
        m_padLY = NormalizeStick(st.Gamepad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
        m_padRX = NormalizeStick(st.Gamepad.sThumbRX, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
        m_padRY = NormalizeStick(st.Gamepad.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
        m_padLT = st.Gamepad.bLeftTrigger  / 255.0f;
        m_padRT = st.Gamepad.bRightTrigger / 255.0f;
    } else {
        m_padConnected = false;
        m_padCur = 0;
        m_padLX = m_padLY = m_padRX = m_padRY = m_padLT = m_padRT = 0.0f;
    }
}

// ── Raw keyboard / mouse ──────────────────────────────────────────────
bool Input::KeyHeld(Key k) const     { return m_enabled &&  m_keyCur[(int)k & 0xFF]; }
bool Input::KeyPressed(Key k) const   { return m_enabled &&  m_keyCur[(int)k & 0xFF] && !m_keyPrev[(int)k & 0xFF]; }
bool Input::KeyReleased(Key k) const  { return m_enabled && !m_keyCur[(int)k & 0xFF] &&  m_keyPrev[(int)k & 0xFF]; }

// ── Gamepad ───────────────────────────────────────────────────────────
bool Input::PadHeld(PadButton b) const {
    return m_enabled && (m_padCur & (uint16_t)b) != 0;
}
bool Input::PadPressed(PadButton b) const {
    return m_enabled && (m_padCur & (uint16_t)b) != 0 && (m_padPrev & (uint16_t)b) == 0;
}
bool Input::PadReleased(PadButton b) const {
    return m_enabled && (m_padCur & (uint16_t)b) == 0 && (m_padPrev & (uint16_t)b) != 0;
}
float Input::PadAxisValue(PadAxis a) const {
    if (!m_enabled) return 0.0f;
    switch (a) {
        case PadAxis::LeftX:        return m_padLX;
        case PadAxis::LeftY:        return m_padLY;
        case PadAxis::RightX:       return m_padRX;
        case PadAxis::RightY:       return m_padRY;
        case PadAxis::LeftTrigger:  return m_padLT;
        case PadAxis::RightTrigger: return m_padRT;
    }
    return 0.0f;
}
void Input::SetVibration(float left, float right) {
    XINPUT_VIBRATION v = {};
    v.wLeftMotorSpeed  = (WORD)(std::clamp(left,  0.0f, 1.0f) * 65535.0f);
    v.wRightMotorSpeed = (WORD)(std::clamp(right, 0.0f, 1.0f) * 65535.0f);
    XInputSetState(0, &v);
}

// ── Mapping layer ─────────────────────────────────────────────────────
void Input::BindAxisKey(const std::string& axis, Key key, float scale) {
    m_axes[axis].keys.push_back({ key, scale });
}
void Input::BindAxisPad(const std::string& axis, PadAxis padAxis, float scale) {
    m_axes[axis].pads.push_back({ padAxis, scale });
}
void Input::BindAction(const std::string& action, Key key) {
    m_actions[action].keys.push_back(key);
}
void Input::BindActionPad(const std::string& action, PadButton button) {
    m_actions[action].buttons.push_back(button);
}
void Input::ClearBindings() { m_axes.clear(); m_actions.clear(); }

float Input::GetAxis(const std::string& axis) const {
    if (!m_enabled) return 0.0f;
    auto it = m_axes.find(axis);
    if (it == m_axes.end()) return 0.0f;
    float v = 0.0f;
    for (const auto& b : it->second.keys) if (KeyHeld(b.key)) v += b.scale;
    for (const auto& b : it->second.pads) v += PadAxisValue(b.axis) * b.scale;
    return std::clamp(v, -1.0f, 1.0f);
}
bool Input::ActionHeld(const std::string& action) const {
    if (!m_enabled) return false;
    auto it = m_actions.find(action);
    if (it == m_actions.end()) return false;
    for (Key k : it->second.keys)        if (KeyHeld(k))  return true;
    for (PadButton b : it->second.buttons) if (PadHeld(b)) return true;
    return false;
}
bool Input::ActionPressed(const std::string& action) const {
    if (!m_enabled) return false;
    auto it = m_actions.find(action);
    if (it == m_actions.end()) return false;
    for (Key k : it->second.keys)          if (KeyPressed(k))  return true;
    for (PadButton b : it->second.buttons) if (PadPressed(b))  return true;
    return false;
}
bool Input::ActionReleased(const std::string& action) const {
    if (!m_enabled) return false;
    auto it = m_actions.find(action);
    if (it == m_actions.end()) return false;
    for (Key k : it->second.keys)          if (KeyReleased(k))  return true;
    for (PadButton b : it->second.buttons) if (PadReleased(b))  return true;
    return false;
}

} // namespace Fujin
