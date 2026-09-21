#include "WindowFinder.h"

#include <dwmapi.h>
#include <algorithm>
#include <cwctype>
#include <filesystem>
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

bool SupportedXboxApp(std::wstring_view process)
{
    // Current Xbox app builds normally expose XboxPcApp.exe. Keep a couple of
    // historical/package-host fallbacks so capture keeps working across app
    // updates without coupling the renderer to one executable name.
    return process == L"xboxpcapp.exe" ||
           process == L"xbox.exe" ||
           process == L"gamingapp.exe" ||
           process == L"applicationframehost.exe";
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
    CaptureTargetPreference preference{CaptureTargetPreference::Auto};
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
    const std::wstring title = WindowTitle(hwnd);
    const bool browser = SupportedBrowser(process);
    const bool xboxProcess = SupportedXboxApp(process);
    const bool xboxTitle =
        ContainsI(title, L"xbox") || ContainsI(title, L"cloud gaming");

    bool candidate = false;
    CaptureTargetPreference kind = CaptureTargetPreference::Auto;

    switch (state.preference) {
        case CaptureTargetPreference::XboxApp:
            candidate = xboxProcess && (process != L"applicationframehost.exe" || xboxTitle);
            kind = CaptureTargetPreference::XboxApp;
            break;
        case CaptureTargetPreference::Browser:
            candidate = browser;
            kind = CaptureTargetPreference::Browser;
            break;
        case CaptureTargetPreference::Auto:
            if (xboxProcess && (process != L"applicationframehost.exe" || xboxTitle)) {
                candidate = true;
                kind = CaptureTargetPreference::XboxApp;
            } else if (browser) {
                candidate = true;
                kind = CaptureTargetPreference::Browser;
            }
            break;
    }

    if (!candidate) return TRUE;

    RECT bounds{};
    if (!GetVisualWindowBounds(hwnd, bounds)) return TRUE;
    const long width = bounds.right - bounds.left;
    const long height = bounds.bottom - bounds.top;
    if (width < 640 || height < 360) return TRUE;

    const unsigned long long area =
        static_cast<unsigned long long>(width) * static_cast<unsigned long long>(height);

    unsigned long long score = area;

    if (kind == CaptureTargetPreference::XboxApp) {
        // In Auto mode, prefer Xbox App decisively over a browser because the
        // native app has the cleanest controller path.
        score += (1ull << 62);
        if (process == L"xboxpcapp.exe") score += (1ull << 61);
        if (xboxTitle) score += (1ull << 60);
    } else {
        if (ContainsI(title, L"xbox")) score += (1ull << 59);
        if (ContainsI(title, L"cloud gaming")) score += (1ull << 58);
        if (ContainsI(title, L"xcloud")) score += (1ull << 57);
    }

    if (!state.best.hwnd || score > state.bestScore) {
        state.bestScore = score;
        state.best.hwnd = hwnd;
        state.best.processName = process;
        state.best.title = title;
        state.best.visualBounds = bounds;
        state.best.kind = kind;
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

BrowserWindowInfo FindBestTargetWindow(CaptureTargetPreference preference)
{
    SearchState state;
    state.preference = preference;
    EnumWindows(EnumProc, reinterpret_cast<LPARAM>(&state));
    return state.best;
}

BrowserWindowInfo FindBestXCloudWindow()
{
    return FindBestTargetWindow(CaptureTargetPreference::Browser);
}

BrowserWindowInfo FindBestXboxAppWindow()
{
    return FindBestTargetWindow(CaptureTargetPreference::XboxApp);
}
