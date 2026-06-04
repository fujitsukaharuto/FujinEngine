#pragma once
// Fujin input system — a standalone, game-usable input layer (not an ImGui wrapper).
//   • Raw devices: keyboard + mouse (Win32 async state) and one XInput gamepad, with held / pressed
//     (this-frame edge) / released semantics, mouse position+delta, sticks/triggers, and rumble.
//   • UE5-style mapping layer on top: bind named Axes (e.g. "MoveForward") and Actions (e.g. "Jump")
//     to keys / buttons / stick axes, then gameplay queries by name — `Input::Get().GetAxis("MoveForward")`
//     — instead of hard-coding key codes. Rebindable at runtime.
//   • This header is Windows.h-free (Key values ARE Win32 virtual-key codes, so no translation table),
//     so it can be included widely without leaking the min/max macros the rest of the engine avoids.
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Fujin {

// Key codes == Win32 virtual-key codes (so the .cpp needs no lookup table). Mouse buttons included.
enum class Key : uint16_t {
    MouseLeft = 0x01, MouseRight = 0x02, MouseMiddle = 0x04, MouseX1 = 0x05, MouseX2 = 0x06,
    Backspace = 0x08, Tab = 0x09, Enter = 0x0D,
    Shift = 0x10, Ctrl = 0x11, Alt = 0x12, Pause = 0x13, CapsLock = 0x14, Escape = 0x1B,
    Space = 0x20, PageUp = 0x21, PageDown = 0x22, End = 0x23, Home = 0x24,
    Left = 0x25, Up = 0x26, Right = 0x27, Down = 0x28,
    Delete = 0x2E,
    Num0 = 0x30, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    A = 0x41, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    F1 = 0x70, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    LeftShift = 0xA0, RightShift = 0xA1, LeftCtrl = 0xA2, RightCtrl = 0xA3,
};

// Digital gamepad buttons. Values == XINPUT_GAMEPAD_* bit masks (stable, so no translation needed).
enum class PadButton : uint16_t {
    Up = 0x0001, Down = 0x0002, Left = 0x0004, Right = 0x0008,
    Start = 0x0010, Back = 0x0020,
    LeftStick = 0x0040, RightStick = 0x0080,
    LeftShoulder = 0x0100, RightShoulder = 0x0200,
    A = 0x1000, B = 0x2000, X = 0x4000, Y = 0x8000,
};

// Analog gamepad axes (for axis bindings). Left/right stick X/Y in [-1,1], triggers in [0,1].
enum class PadAxis : uint8_t { LeftX, LeftY, RightX, RightY, LeftTrigger, RightTrigger };

class Input {
public:
    static Input& Get();

    void Initialize(void* hwnd);   // hwnd used to map the cursor into client space
    void Update();                 // poll all devices + advance edge state; call once per frame
    void Shutdown();

    // When disabled (e.g. editor not in Play, or window unfocused) all queries return neutral, so
    // gameplay code never reacts to input it shouldn't. Raw polling still runs to keep edges coherent.
    void SetEnabled(bool e) { m_enabled = e; }
    bool IsEnabled() const  { return m_enabled; }

    // ── Raw keyboard / mouse ────────────────────────────────────────────
    bool KeyHeld(Key k) const;
    bool KeyPressed(Key k) const;   // transitioned up→down this frame
    bool KeyReleased(Key k) const;  // transitioned down→up this frame

    float MouseX()  const { return m_enabled ? m_mouseX : 0.0f; }   // client-space pixels
    float MouseY()  const { return m_enabled ? m_mouseY : 0.0f; }
    float MouseDX() const { return m_enabled ? m_mouseDX : 0.0f; }  // delta since last frame
    float MouseDY() const { return m_enabled ? m_mouseDY : 0.0f; }
    float MouseWheel() const { return m_enabled ? m_wheel : 0.0f; }
    void  FeedMouseWheel(float delta) { m_wheelAccum += delta; }    // call from WndProc on WM_MOUSEWHEEL

    // ── Gamepad (XInput player 0) ───────────────────────────────────────
    bool PadConnected() const { return m_enabled && m_padConnected; }
    bool PadHeld(PadButton b) const;
    bool PadPressed(PadButton b) const;
    bool PadReleased(PadButton b) const;
    float PadAxisValue(PadAxis a) const;             // sticks [-1,1] (dead-zoned), triggers [0,1]
    void  SetVibration(float left, float right);     // 0..1 motor strengths

    // ── UE5-style named mappings ────────────────────────────────────────
    void BindAxisKey(const std::string& axis, Key key, float scale);     // key held → += scale
    void BindAxisPad(const std::string& axis, PadAxis padAxis, float scale);
    void BindAction(const std::string& action, Key key);
    void BindActionPad(const std::string& action, PadButton button);
    void ClearBindings();

    float GetAxis(const std::string& axis) const;        // sum of bound contributions, clamped [-1,1]
    bool  ActionHeld(const std::string& action) const;
    bool  ActionPressed(const std::string& action) const;
    bool  ActionReleased(const std::string& action) const;

private:
    Input() = default;
    Input(const Input&) = delete;
    Input& operator=(const Input&) = delete;

    bool RawKeyHeld(int vk) const     { return m_keyCur[vk & 0xFF]; }
    bool RawKeyHeldPrev(int vk) const { return m_keyPrev[vk & 0xFF]; }

    void*  m_hwnd    = nullptr;
    bool   m_enabled = false;

    bool m_keyCur[256]  = {};
    bool m_keyPrev[256] = {};

    float m_mouseX = 0, m_mouseY = 0, m_mouseDX = 0, m_mouseDY = 0;
    float m_prevMouseX = 0, m_prevMouseY = 0;
    float m_wheel = 0, m_wheelAccum = 0;
    bool  m_haveMouse = false;

    bool     m_padConnected = false;
    uint16_t m_padCur = 0, m_padPrev = 0;
    float    m_padLX = 0, m_padLY = 0, m_padRX = 0, m_padRY = 0, m_padLT = 0, m_padRT = 0;

    struct AxisKeyBind { Key key; float scale; };
    struct AxisPadBind { PadAxis axis; float scale; };
    struct AxisBinding { std::vector<AxisKeyBind> keys; std::vector<AxisPadBind> pads; };
    struct ActionBinding { std::vector<Key> keys; std::vector<PadButton> buttons; };

    std::unordered_map<std::string, AxisBinding>   m_axes;
    std::unordered_map<std::string, ActionBinding> m_actions;
};

} // namespace Fujin
