#pragma once
#include <Windows.h>
#include <cstdint>

namespace Fujin {

class Win32Window {
public:
    bool Initialize(const wchar_t* title, uint32_t width, uint32_t height);
    void Shutdown();
    bool ProcessMessages();

    HWND     GetHWND()     const { return m_hwnd; }
    uint32_t GetWidth()    const { return m_width; }
    uint32_t GetHeight()   const { return m_height; }
    bool     IsMinimized() const { return m_minimized; }
    bool     WasResized()  const { return m_resized; }
    void     ClearResize()       { m_resized = false; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    HWND     m_hwnd      = nullptr;
    uint32_t m_width     = 0;
    uint32_t m_height    = 0;
    bool     m_minimized = false;
    bool     m_resized   = false;
};

} // namespace Fujin
