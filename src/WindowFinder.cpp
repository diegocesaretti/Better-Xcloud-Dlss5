#include "WindowFinder.h"

#include <dwmapi.h>
#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>

namespace {

std::wstring Lower(std::wstring value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return value;
}

bool ContainsI(std::wstring_view text, std::wstring_view needle)
{
    if (needle.empty()) return true;
    std::wstring lowerText(text);
    std::wstring lowerNeedle(needle);
    lowerText = Lower(std::move(lowerText));
    lowerNeedle = Lower(std::move(lowerNeedle));
    return lowerText.find(lowerNeedle) != std::wstring::npos;
}

std::wstring ProcessNameForWindow(HWND hwnd)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) return {};

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return {};

    std::wstring path(32768, L'\0');
    DWORD chars = static_cast<DWORD>(path.size());
    if (!QueryFullProcessImageNameW(process, 0, path.data(), &chars)) {
        CloseHandle(process);
        return {};
    }
    CloseHandle(process);
    path.resize(chars);
    return Lower(std::filesystem::path(path).filename().wstring());
}

bool SupportedBrowser(std::wstring_view process)
{
    return process == L"msedge.exe" ||
           process == L"chrome.exe" ||
           process == L"brave.exe";
}

std::wstring WindowTitle(HWND hwnd)
{
    const int length = GetWindowTextLengthW(hwnd);
    if (length <= 0) return {};

    std::wstring title(static_cast<size_t>(length) + 1u, L'\0');
    const int written = GetWindowTextW(hwnd, title.data(), length + 1);
    if (written <= 0) return {};
    title.resize(static_cast<size_t>(written));
    return title;
}

struct SearchState {
    BrowserWindowInfo best;
    unsigned long long bestScore{};
};

BOOL CALLBACK EnumProc(HWND hwnd, LPARAM param)
{
    auto& state = *reinterpret_cast<SearchState*>(param);

    if (!IsWindowVisible(hwnd) || IsIconic(hwnd) || GetWindow(hwnd, GW_OWNER) != nullptr) {
        return TRUE;
    }

    const std::wstring process = ProcessNameForWindow(hwnd);
    if (!SupportedBrowser(process)) return TRUE;

    RECT bounds{};
    if (!GetVisualWindowBounds(hwnd, bounds)) return TRUE;
    const long width = bounds.right - bounds.left;
    const long height = bounds.bottom - bounds.top;
    if (width < 640 || height < 360) return TRUE;

    const std::wstring title = WindowTitle(hwnd);
    const unsigned long long area =
        static_cast<unsigned long long>(width) * static_cast<unsigned long long>(height);

    // Prefer a window whose title explicitly identifies Xbox/xCloud. The large
    // bonus makes a smaller xCloud app window beat an unrelated browser window.
    unsigned long long score = area;
    if (ContainsI(title, L"xbox")) score += (1ull << 62);
    if (ContainsI(title, L"cloud gaming")) score += (1ull << 61);
    if (ContainsI(title, L"xcloud")) score += (1ull << 60);

    if (!state.best.hwnd || score > state.bestScore) {
        state.bestScore = score;
        state.best.hwnd = hwnd;
        state.best.processName = process;
        state.best.title = title;
        state.best.visualBounds = bounds;
    }
    return TRUE;
}

} // namespace

bool GetVisualWindowBounds(HWND hwnd, RECT& bounds)
{
    if (!hwnd || !IsWindow(hwnd)) return false;

    if (SUCCEEDED(DwmGetWindowAttribute(
            hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &bounds, sizeof(bounds)))) {
        return bounds.right > bounds.left && bounds.bottom > bounds.top;
    }

    return GetWindowRect(hwnd, &bounds) != FALSE &&
           bounds.right > bounds.left && bounds.bottom > bounds.top;
}

BrowserWindowInfo FindBestXCloudWindow()
{
    SearchState state;
    EnumWindows(EnumProc, reinterpret_cast<LPARAM>(&state));
    return state.best;
}
