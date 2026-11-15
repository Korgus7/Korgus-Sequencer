// Stav klaves cez systém - samostatny subor kvoli windows.h
#include "KbdKeys.h"
#ifdef _WIN32
#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cwctype>
#include <string>

namespace KbdKeys
{
    bool isScanDown (int sc, char)
    {
        const UINT vk = MapVirtualKeyW ((UINT) sc, MAPVK_VSC_TO_VK);
        return vk != 0 && (GetAsyncKeyState ((int) vk) & 0x8000) != 0;
    }
    bool isForeground (void* h)
    {
        const HWND fg = GetForegroundWindow();
        return h != nullptr && fg != nullptr && (fg == (HWND) h || GetAncestor (fg, GA_ROOT) == (HWND) h
                                                  || GetAncestor ((HWND) h, GA_ROOT) == fg);
    }
    bool nativeTextFocus()
    {
        const HWND f = GetFocus();
        if (f == nullptr) return false;
        wchar_t cls[64] = {};
        GetClassNameW (f, cls, 63);
        std::wstring c (cls);
        for (auto& ch : c) ch = (wchar_t) std::towlower (ch);
        return c.find (L"edit") != std::wstring::npos || c.find (L"combobox") != std::wstring::npos;
    }
    bool commandModsDown()
    {
        return (GetAsyncKeyState (VK_CONTROL) & 0x8000) || (GetAsyncKeyState (VK_MENU) & 0x8000)
            || (GetAsyncKeyState (VK_LWIN) & 0x8000) || (GetAsyncKeyState (VK_RWIN) & 0x8000);
    }
    bool isVkDown (int vk) { return (GetAsyncKeyState (vk) & 0x8000) != 0; }
    int mods()
    {
        return ((GetAsyncKeyState (VK_CONTROL) & 0x8000) ? 1 : 0) | ((GetAsyncKeyState (VK_SHIFT) & 0x8000) ? 2 : 0) | ((GetAsyncKeyState (VK_MENU) & 0x8000) ? 4 : 0);
    }
}
#else
#include <juce_gui_basics/juce_gui_basics.h>
namespace KbdKeys
{
    bool isScanDown (int, char c) { return c != 0 && juce::KeyPress::isKeyCurrentlyDown (c); }
    bool isForeground (void*) { return true; }   // na ostatnych systemoch rozhoduje isActiveWindow()
    bool nativeTextFocus() { return false; }
    bool commandModsDown()
    {
        const auto m = juce::ModifierKeys::getCurrentModifiersRealtime();
        return m.isCtrlDown() || m.isAltDown() || m.isCommandDown();
    }
    bool isVkDown (int) { return false; }
    int mods() { return 0; }
}
#endif
