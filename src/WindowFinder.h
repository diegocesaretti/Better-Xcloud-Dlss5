#pragma once

#include <windows.h>
#include <string>

struct BrowserWindowInfo {
    HWND hwnd{};
    std::wstring processName;
    std::wstring title;
    RECT visualBounds{};
};

BrowserWindowInfo FindBestXCloudWindow();
bool GetVisualWindowBounds(HWND hwnd, RECT& bounds);
