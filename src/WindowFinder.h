#pragma once

#include <windows.h>
#include <string>

enum class CaptureTargetPreference {
    XboxApp,
    Browser,
    Auto
};

struct BrowserWindowInfo {
    HWND hwnd{};
    std::wstring processName;
    std::wstring title;
    RECT visualBounds{};
    CaptureTargetPreference kind{CaptureTargetPreference::Auto};
};

BrowserWindowInfo FindBestTargetWindow(CaptureTargetPreference preference);
BrowserWindowInfo FindBestXCloudWindow();
BrowserWindowInfo FindBestXboxAppWindow();
bool GetVisualWindowBounds(HWND hwnd, RECT& bounds);
