#include "WindowCapture.h"
#include "WindowFinder.h"

#include "D3D12Renderer.h"
#include "DLSSBackend.h"
#include "TemporalGuides.h"

#include <windows.h>
#include <dwmapi.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>

#include <winrt/base.h>

namespace {

constexpr wchar_t kOverlayClass[] = L"BetterXcloudDLSS5Overlay";
constexpr int kHotkeyToggle = 1;
constexpr int kHotkeyExit = 2;

LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        case WM_ERASEBKGND:
            return 1;
        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wparam, lparam);
    }
}

HWND CreateOverlay(const RECT& bounds)
{
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = OverlayWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kOverlayClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;

    return CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        kOverlayClass,
        L"Better Xcloud DLSS5",
        WS_POPUP,
        bounds.left,
        bounds.top,
        width,
        height,
        nullptr,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);
}

void PlaceOverlay(HWND overlay, HWND target)
{
    RECT bounds{};
    if (!GetVisualWindowBounds(target, bounds)) return;

    SetWindowPos(
        overlay,
        HWND_TOPMOST,
        bounds.left,
        bounds.top,
        bounds.right - bounds.left,
        bounds.bottom - bounds.top,
        SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

bool TargetHasFocus(HWND target)
{
    HWND foreground = GetForegroundWindow();
    if (!foreground) return false;
    return foreground == target || GetAncestor(foreground, GA_ROOT) == target;
}

std::filesystem::path ModuleDirectory()
{
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(
        nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size()) return std::filesystem::current_path();
    path.resize(length);
    return std::filesystem::path(path).parent_path();
}

void ErrorBox(const std::wstring& text)
{
    MessageBoxW(
        nullptr,
        text.c_str(),
        L"Better Xcloud DLSS5",
        MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
}

std::wstring RuntimeLogHint()
{
    return L"\n\nLog: " + (ModuleDirectory() / L"DLSSVideoPlayer.log").wstring();
}

BrowserWindowInfo WaitForBrowserWindow()
{
    using namespace std::chrono_literals;
    const auto deadline = std::chrono::steady_clock::now() + 120s;

    while (std::chrono::steady_clock::now() < deadline) {
        BrowserWindowInfo info = FindBestXCloudWindow();
        if (info.hwnd) return info;

        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        std::this_thread::sleep_for(250ms);
    }
    return {};
}

bool WaitForFirstFrame(WindowCapture& capture, CapturedFrame& frame)
{
    using namespace std::chrono_literals;
    const auto deadline = std::chrono::steady_clock::now() + 15s;

    while (std::chrono::steady_clock::now() < deadline) {
        if (capture.TryGetLatest(frame)) return true;

        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        std::this_thread::sleep_for(2ms);
    }
    return false;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    // WGC reports physical pixels. Match that coordinate system when placing
    // the overlay on mixed-DPI desktops.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (const winrt::hresult_error& e) {
        ErrorBox(L"Unable to initialize the Windows Runtime.\n\nHRESULT: " +
                 std::to_wstring(static_cast<unsigned long>(e.code())));
        return 10;
    }

    RegisterHotKey(nullptr, kHotkeyToggle, 0, VK_F8);
    RegisterHotKey(nullptr, kHotkeyExit, 0, VK_F9);

    BrowserWindowInfo browser = WaitForBrowserWindow();
    if (!browser.hwnd) {
        ErrorBox(
            L"No Edge/Chrome/Brave xCloud window was found within 2 minutes.\n\n"
            L"Start Xbox Cloud Gaming, then launch Better Xcloud DLSS5 again.");
        return 11;
    }

    WindowCapture capture;
    if (!capture.Start(browser.hwnd)) {
        ErrorBox(capture.LastError());
        return 12;
    }

    CapturedFrame frame;
    if (!WaitForFirstFrame(capture, frame)) {
        std::wstring reason = capture.LastError();
        if (reason.empty()) {
            reason = L"No frame arrived from the browser. Make sure the xCloud window is visible.";
        }
        ErrorBox(reason);
        return 13;
    }

    HWND overlay = CreateOverlay(browser.visualBounds);
    if (!overlay) {
        ErrorBox(L"Unable to create the processed-video overlay.");
        return 14;
    }

    // Keep the carrier 1:1. DLAA is the hook-visible NGX contract that the
    // upstream project uses when neural rendering should run without spatial
    // upscaling first.
    const auto [gridW, gridH] =
        TemporalGuideGenerator::AnalysisGrid(frame.width, frame.height, 60.0);

    auto renderer = MakeD3D12Renderer();
    if (!renderer ||
        !renderer->Initialize(
            overlay,
            frame.width,
            frame.height,
            frame.width,
            frame.height,
            gridW,
            gridH,
            DefaultNeuralCarrierQuality(),
            false) ||
        !renderer->DLSSAvailable()) {

        ErrorBox(
            L"The DLSS carrier could not initialize. This can mean an unsupported "
            L"GPU/driver, a missing runtime file, or a runtime compatibility failure." +
            RuntimeLogHint());
        if (overlay) DestroyWindow(overlay);
        return 15;
    }

    TemporalGuideGenerator guides;
    bool overlayEnabled = true;
    bool running = true;
    bool forceReset = true;
    std::uint64_t lastSequence = 0;

    const auto ptsOrigin = std::chrono::steady_clock::now();
    auto previousFrameTime = ptsOrigin;
    auto nextPositionSync = ptsOrigin;

    while (running && IsWindow(browser.hwnd)) {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_HOTKEY) {
                if (msg.wParam == kHotkeyToggle) {
                    overlayEnabled = !overlayEnabled;
                    if (!overlayEnabled) ShowWindow(overlay, SW_HIDE);
                } else if (msg.wParam == kHotkeyExit) {
                    running = false;
                }
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!running) break;

        const auto now = std::chrono::steady_clock::now();
        if (now >= nextPositionSync) {
            PlaceOverlay(overlay, browser.hwnd);
            nextPositionSync = now + std::chrono::milliseconds(250);

            const bool shouldShow = overlayEnabled && TargetHasFocus(browser.hwnd);
            ShowWindow(overlay, shouldShow ? SW_SHOWNOACTIVATE : SW_HIDE);
        }

        CapturedFrame latest;
        if (!capture.TryGetLatest(latest)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        // Resizing requires recreation of both WGC buffers and the D3D12
        // renderer. The launcher starts a maximized app window, so for the
        // alpha we fail visibly instead of silently stretching/misaligning.
        if (latest.width != frame.width || latest.height != frame.height) {
            ErrorBox(
                L"The xCloud window changed size. Restart Better Xcloud DLSS5 "
                L"after resizing/fullscreening the browser. Automatic live resize "
                L"is planned for the next milestone.");
            break;
        }

        const bool dropped =
            latest.drainedFrames > 1 ||
            (lastSequence != 0 && latest.sequence != lastSequence + 1);

        const HistoryReset reset =
            forceReset ? HistoryReset::FirstFrame :
            (dropped ? HistoryReset::Drop : HistoryReset::None);

        FrameIdentity id{};
        id.frameNumber = latest.sequence ? latest.sequence - 1 : 0;
        id.pts100ns = static_cast<std::int64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                latest.capturedAt - ptsOrigin).count() / 100);
        id.sourceGeneration = 1;
        id.historyGeneration = guides.HistoryGeneration();
        id.jobId = 0;
        id.reset = reset;

        if (reset != HistoryReset::None && reset != HistoryReset::FirstFrame) {
            // Generate() also sees the reset in the identity. Resetting here
            // releases stale CPU-side history immediately after an intentional
            // latest-frame drop.
            guides.Reset();
            id.historyGeneration = guides.HistoryGeneration();
        }

        GuideFrame guide;
        const float frameTimeMs = static_cast<float>(std::clamp(
            std::chrono::duration<double, std::milli>(
                latest.capturedAt - previousFrameTime).count(),
            1.0,
            100.0));

        const bool guideOk = guides.Generate(
            latest.bgra.data(),
            latest.width,
            latest.height,
            latest.width,
            latest.height,
            60.0,
            id,
            guide,
            SourcePixelLayout::Bgra);

        const bool renderOk =
            guideOk &&
            renderer->RenderFrame(
                latest.bgra.data(),
                latest.bgra.size(),
                id,
                guide,
                frameTimeMs);

        if (!renderOk) {
            if (renderer->GpuUnusable()) {
                ErrorBox(
                    L"The GPU renderer stopped responding or the D3D12 device was removed." +
                    RuntimeLogHint());
                break;
            }
            // Do not build a queue or retry the same stale cloud frame. Discard
            // history and let the next frame make a clean attempt.
            forceReset = true;
        } else {
            forceReset = false;
        }

        previousFrameTime = latest.capturedAt;
        lastSequence = latest.sequence;
        frame = std::move(latest);
    }

    ShowWindow(overlay, SW_HIDE);
    renderer.reset();
    capture.Stop();
    DestroyWindow(overlay);

    UnregisterHotKey(nullptr, kHotkeyToggle);
    UnregisterHotKey(nullptr, kHotkeyExit);
    return 0;
}
