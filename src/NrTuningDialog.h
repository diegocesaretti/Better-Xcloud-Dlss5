#pragma once

#include <windows.h>
#include <filesystem>

class NrTuningDialog {
public:
    static void Show(HWND owner, const std::filesystem::path& iniPath);
};
