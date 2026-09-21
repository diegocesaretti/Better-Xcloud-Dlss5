#include "WindowCapture.h"
#include "WindowFinder.h"

#include "D3D12Renderer.h"
#include "DLSSBackend.h"
#include "TemporalGuides.h"

#include <windows.h>
#include <winver.h>
#include <dwmapi.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <string>
#include <thread>

#include <winrt/base.h>

namespace {

constexpr wchar_t kOverlayClass[] = L"BetterXcloudDLSS5Overlay";
constexpr int kHotkeyToggle = 1;
constexpr int kHotkeyExit = 2;
bool g_overlayInteractive = false;

LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
        case WM_NCHITTEST:
            return g_overlayInteractive ? HTCLIENT : HTTRANSPARENT;
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

void SetOverlayInteractive(HWND overlay, bool interactive)
{
    if (!overlay) return;

    LONG_PTR exStyle = GetWindowLongPtrW(overlay, GWL_EXSTYLE);
    if (interactive) {
        exStyle &= ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
    } else {
        exStyle |= static_cast<LONG_PTR>(WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
    }
    SetWindowLongPtrW(overlay, GWL_EXSTYLE, exStyle);
    g_overlayInteractive = interactive;

    SetWindowPos(
        overlay,
        HWND_TOPMOST,
        0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOOWNERZORDER |
            SWP_FRAMECHANGED | (interactive ? 0 : SWP_NOACTIVATE));
}

bool ActivateWindow(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd)) return false;

    const DWORD currentThread = GetCurrentThreadId();
    DWORD targetProcess = 0;
    const DWORD targetThread = GetWindowThreadProcessId(hwnd, &targetProcess);

    HWND foreground = GetForegroundWindow();
    DWORD foregroundProcess = 0;
    const DWORD foregroundThread =
        foreground ? GetWindowThreadProcessId(foreground, &foregroundProcess) : 0;

    bool attachedTarget = false;
    bool attachedForeground = false;

    if (targetThread && targetThread != currentThread) {
        attachedTarget = AttachThreadInput(currentThread, targetThread, TRUE) != FALSE;
    }
    if (foregroundThread && foregroundThread != currentThread &&
        foregroundThread != targetThread) {
        attachedForeground =
            AttachThreadInput(currentThread, foregroundThread, TRUE) != FALSE;
    }

    ShowWindow(hwnd, SW_SHOW);
    BringWindowToTop(hwnd);
    const bool foregroundOk = SetForegroundWindow(hwnd) != FALSE;
    SetFocus(hwnd);

    if (attachedForeground) {
        AttachThreadInput(currentThread, foregroundThread, FALSE);
    }
    if (attachedTarget) {
        AttachThreadInput(currentThread, targetThread, FALSE);
    }

    return foregroundOk || GetForegroundWindow() == hwnd ||
           GetAncestor(GetForegroundWindow(), GA_ROOT) == hwnd;
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

std::wstring ModulePath(HMODULE module)
{
    if (!module) return {};
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(
        module, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size()) return {};
    path.resize(length);
    return path;
}

bool PathEqualsInsensitive(const std::filesystem::path& left,
                           const std::filesystem::path& right)
{
    const std::wstring a = left.lexically_normal().wstring();
    const std::wstring b = right.lexically_normal().wstring();
    return CompareStringOrdinal(
               a.c_str(), static_cast<int>(a.size()),
               b.c_str(), static_cast<int>(b.size()),
               TRUE) == CSTR_EQUAL;
}

void PrimeVersionProxyImport()
{
    // version.lib is linked deliberately. When a compatibility pack places a
    // proxy named version.dll beside the host, the Windows loader resolves that
    // import before wWinMain, matching the injection method used by the known-
    // good GTX/Turing pack. This harmless query keeps the import live.
    std::wstring exePath(32768, L'\0');
    const DWORD length = GetModuleFileNameW(
        nullptr, exePath.data(), static_cast<DWORD>(exePath.size()));
    if (!length || length >= exePath.size()) return;
    exePath.resize(length);
    DWORD ignored = 0;
    (void)GetFileVersionInfoSizeW(exePath.c_str(), &ignored);
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
    PrimeVersionProxyImport();

    const std::filesystem::path moduleDir = ModuleDirectory();
    const bool versionProxyPresent = std::filesystem::exists(moduleDir / L"version.dll");
    const bool streamlineInterposerPresent =
        std::filesystem::exists(moduleDir / L"sl.interposer.dll");
    const bool streamlineNrPresent =
        std::filesystem::exists(moduleDir / L"sl.dlss_nr.dll");
    const bool streamlineCompatPresent =
        versionProxyPresent && streamlineInterposerPresent && streamlineNrPresent &&
        std::filesystem::exists(moduleDir / L"nvngx_dlss.dll") &&
        std::filesystem::exists(moduleDir / L"nvngx_dlssnr.dll");

    const std::wstring loadedVersionPath =
        ModulePath(GetModuleHandleW(L"version.dll"));
    const bool localVersionProxyLoaded =
        !loadedVersionPath.empty() &&
        PathEqualsInsensitive(
            std::filesystem::path(loadedVersionPath),
            moduleDir / L"version.dll");

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

    // F8 is deliberately left free for compatibility tools / user bindings.
    // F7 only hides the processed overlay; Insert enters OptiScaler setup mode.
    RegisterHotKey(nullptr, kHotkeyToggle, 0, VK_F7);
    RegisterHotKey(nullptr, kHotkeyExit, 0, VK_F9);

    BrowserWindowInfo browser = WaitForBrowserWindow();
    if (!browser.hwnd) {
        ErrorBox(
            L"No Edge/Chrome/Brave xCloud window was found within 2 minutes.\n\n"
            L"Start Xbox Cloud Gaming, then launch Better Xcloud DLSS5 again.");
        return 11;
    }

    std::ofstream liveLog(
        ModuleDirectory() / L"XCloudDLSS5-live.log",
        std::ios::out | std::ios::trunc);
    liveLog << "Better Xcloud DLSS5 live session\n";
    liveLog << "browserProcess=";
    for (wchar_t ch : browser.processName) liveLog << (ch <= 0x7f ? char(ch) : '?');
    liveLog << " title=";
    for (wchar_t ch : browser.title) liveLog << (ch <= 0x7f ? char(ch) : '?');
    liveLog << "\n";
    liveLog << "compatVersionProxyPresent=" << (versionProxyPresent ? 1 : 0)
            << " compatVersionProxyLoaded=" << (localVersionProxyLoaded ? 1 : 0)
            << " streamlineInterposer=" << (streamlineInterposerPresent ? 1 : 0)
            << " streamlineNr=" << (streamlineNrPresent ? 1 : 0)
            << " streamlineCompat=" << (streamlineCompatPresent ? 1 : 0)
            << "\n";
    liveLog << "versionModule=";
    for (wchar_t ch : loadedVersionPath) liveLog << (ch <= 0x7f ? char(ch) : '?');
    liveLog << "\n";
    liveLog.flush();

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

    liveLog << "capture=" << frame.width << "x" << frame.height << "\n";
    const bool localNgxUnderscore = std::filesystem::exists(ModuleDirectory() / L"_nvngx.dll");
    const bool localNgxPlain = std::filesystem::exists(ModuleDirectory() / L"nvngx.dll");
    liveLog << "localNgxUnderscore=" << (localNgxUnderscore ? 1 : 0)
            << " localNgxPlain=" << (localNgxPlain ? 1 : 0) << "\n";
    liveLog.flush();

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

        std::wstring carrierError =
            L"The DLSS carrier could not initialize.";
        if (streamlineCompatPresent && !localVersionProxyLoaded) {
            carrierError +=
                L"\n\nThe GTX Streamline compatibility files are present, but "
                L"version.dll was not loaded as the process-local startup proxy. "
                L"Check XCloudDLSS5-live.log for the loaded version.dll path.";
        } else if (streamlineCompatPresent && localVersionProxyLoaded) {
            carrierError +=
                L"\n\nThe GTX Streamline/version.dll compatibility route is "
                L"loaded. The remaining failure is inside the carrier/NGX path; "
                L"inspect DLSSVideoPlayer.log and ReShade.log for the next gate.";
        } else if (!localNgxUnderscore && !localNgxPlain) {
            carrierError +=
                L"\n\nNo complete GTX compatibility route was staged. "
                L"Install with the known-good pack that contains version.dll, "
                L"Streamline plugins, nvngx_dlss.dll and nvngx_dlssnr.dll.";
        } else {
            carrierError +=
                L"\n\nA local NGX core override is present, so inspect the NGX "
                L"and ReShade logs for the next compatibility gate.";
        }
        carrierError += RuntimeLogHint();
        ErrorBox(carrierError);
        if (overlay) DestroyWindow(overlay);
        return 15;
    }

    liveLog << "renderer=initialized dlssAvailable=1\n";

    // The compatibility host can become foreground while its proxy/overlay
    // initializes. xCloud's Gamepad API expects the browser to be the active
    // application, so explicitly hand focus back before normal play starts.
    const bool initialBrowserFocus = ActivateWindow(browser.hwnd);
    liveLog << "browserFocusAfterInit=" << (initialBrowserFocus ? 1 : 0) << "\n";
    liveLog.flush();

    TemporalGuideGenerator guides;
    bool overlayEnabled = true;
    bool running = true;
    bool forceReset = true;
    bool setupMode = false;
    bool insertWasDown = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
    bool leaveSetupOnInsertRelease = false;
    auto returnToGameAt = std::chrono::steady_clock::time_point::max();
    std::uint64_t lastSequence = 0;

    const auto ptsOrigin = std::chrono::steady_clock::now();
    auto previousFrameTime = ptsOrigin;
    auto nextPositionSync = ptsOrigin;
    auto statsWindowStart = ptsOrigin;
    std::uint64_t statsInputFrames = 0;
    std::uint64_t statsRenderedFrames = 0;
    std::uint64_t statsDroppedFrames = 0;
    double statsProcessingMs = 0.0;

    while (running && IsWindow(browser.hwnd)) {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_HOTKEY) {
                if (msg.wParam == kHotkeyToggle) {
                    overlayEnabled = !overlayEnabled;
                    if (!overlayEnabled) ShowWindow(overlay, SW_HIDE);
                } else if (msg.wParam == kHotkeyExit) {
                    liveLog << "exitReason=hotkey-F9\n";
                    liveLog.flush();
                    running = false;
                }
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!running) break;

        const auto now = std::chrono::steady_clock::now();

        // OptiScaler's menu input is tied to the render target HWND being
        // foreground. Our normal overlay is intentionally click-through and
        // non-activating, so use Insert as a two-way setup handshake:
        // first press focuses/makes the overlay interactive; OptiScaler sees
        // that same key release and opens its menu. The second press closes
        // OptiScaler, then we return focus/input to the xCloud browser.
        const bool insertDown = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
        if (insertDown && !insertWasDown) {
            if (!setupMode) {
                setupMode = true;
                returnToGameAt = std::chrono::steady_clock::time_point::max();
                SetOverlayInteractive(overlay, true);
                PlaceOverlay(overlay, browser.hwnd);
                ShowWindow(overlay, SW_SHOW);
                const bool focused = ActivateWindow(overlay);
                liveLog << "setupMode=entered trigger=Insert focus="
                        << (focused ? 1 : 0) << "\n";
                liveLog.flush();
            } else {
                // Keep the host focused until OptiScaler observes the matching
                // key release and closes its own menu.
                leaveSetupOnInsertRelease = true;
            }
        }

        if (!insertDown && insertWasDown && setupMode &&
            leaveSetupOnInsertRelease) {
            leaveSetupOnInsertRelease = false;
            returnToGameAt = now + std::chrono::milliseconds(250);
        }

        if (setupMode &&
            returnToGameAt != std::chrono::steady_clock::time_point::max() &&
            now >= returnToGameAt) {
            setupMode = false;
            returnToGameAt = std::chrono::steady_clock::time_point::max();
            SetOverlayInteractive(overlay, false);
            const bool focused = ActivateWindow(browser.hwnd);
            liveLog << "setupMode=exited trigger=Insert browserFocus="
                    << (focused ? 1 : 0) << "\n";
            liveLog.flush();
        }
        insertWasDown = insertDown;

        if (now >= nextPositionSync) {
            PlaceOverlay(overlay, browser.hwnd);
            nextPositionSync = now + std::chrono::milliseconds(250);

            const bool shouldShow =
                overlayEnabled && (setupMode || TargetHasFocus(browser.hwnd));
            if (shouldShow) {
                ShowWindow(
                    overlay,
                    setupMode ? SW_SHOW : SW_SHOWNOACTIVATE);
            } else {
                ShowWindow(overlay, SW_HIDE);
            }
        }

        CapturedFrame latest;
        if (!capture.TryGetLatest(latest)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        // xCloud/Chromium can resize the captured surface when a stream starts,
        // browser chrome changes or fullscreen is entered. Recreate only the
        // D3D12 carrier and temporal history; keep WGC and the browser session alive.
        if (latest.width != frame.width || latest.height != frame.height) {
            liveLog << "resize=" << frame.width << "x" << frame.height
                    << "->" << latest.width << "x" << latest.height << "\n";
            liveLog.flush();

            ShowWindow(overlay, SW_HIDE);
            renderer.reset();
            guides.Reset();

            const auto [resizeGridW, resizeGridH] =
                TemporalGuideGenerator::AnalysisGrid(
                    latest.width, latest.height, 60.0);

            renderer = MakeD3D12Renderer();
            if (!renderer ||
                !renderer->Initialize(
                    overlay,
                    latest.width,
                    latest.height,
                    latest.width,
                    latest.height,
                    resizeGridW,
                    resizeGridH,
                    DefaultNeuralCarrierQuality(),
                    false) ||
                !renderer->DLSSAvailable()) {
                liveLog << "exitReason=renderer-reinit-after-resize-failed\n";
                liveLog.flush();
                ErrorBox(
                    L"The xCloud video surface changed size and the DLSS carrier "
                    L"could not be recreated." + RuntimeLogHint());
                break;
            }

            PlaceOverlay(overlay, browser.hwnd);
            if (setupMode) {
                SetOverlayInteractive(overlay, true);
                ActivateWindow(overlay);
            } else {
                SetOverlayInteractive(overlay, false);
                ActivateWindow(browser.hwnd);
            }
            frame = std::move(latest);
            previousFrameTime = std::chrono::steady_clock::now();
            forceReset = true;
            lastSequence = 0;
            liveLog << "resizeRenderer=initialized dlssAvailable=1\n";
            liveLog.flush();
            continue;
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

        ++statsInputFrames;
        if (latest.drainedFrames > 1) {
            statsDroppedFrames += static_cast<std::uint64_t>(latest.drainedFrames - 1);
        }

        GuideFrame guide;
        const float frameTimeMs = static_cast<float>(std::clamp(
            std::chrono::duration<double, std::milli>(
                latest.capturedAt - previousFrameTime).count(),
            1.0,
            100.0));

        const auto processingStarted = std::chrono::steady_clock::now();

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

        const auto processingEnded = std::chrono::steady_clock::now();
        statsProcessingMs += std::chrono::duration<double, std::milli>(
            processingEnded - processingStarted).count();
        if (renderOk) ++statsRenderedFrames;

        if (!renderOk) {
            if (renderer->GpuUnusable()) {
                liveLog << "exitReason=gpu-unusable\n";
                liveLog.flush();
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

        const auto statsNow = std::chrono::steady_clock::now();
        const double statsSeconds =
            std::chrono::duration<double>(statsNow - statsWindowStart).count();
        if (statsSeconds >= 2.0) {
            const double fps = statsSeconds > 0.0
                ? static_cast<double>(statsRenderedFrames) / statsSeconds
                : 0.0;
            const double avgMs = statsInputFrames
                ? statsProcessingMs / static_cast<double>(statsInputFrames)
                : 0.0;

            liveLog << std::fixed << std::setprecision(2)
                    << "fps=" << fps
                    << " avgProcessingMs=" << avgMs
                    << " neuralGpuMs=" << renderer->LastNeuralGpuMs()
                    << " droppedCaptureFrames=" << statsDroppedFrames
                    << " peakVramMiB=" << renderer->PeakLocalVideoMemoryMiB()
                    << " evaluations=" << renderer->DLSSEvaluations()
                    << "\n";
            liveLog.flush();

            statsWindowStart = statsNow;
            statsInputFrames = 0;
            statsRenderedFrames = 0;
            statsDroppedFrames = 0;
            statsProcessingMs = 0.0;
        }
    }

    if (!IsWindow(browser.hwnd)) {
        liveLog << "exitReason=browser-window-closed\n";
    }
    liveLog << "session=ended\n";
    liveLog.flush();

    ShowWindow(overlay, SW_HIDE);
    renderer.reset();
    capture.Stop();
    DestroyWindow(overlay);

    UnregisterHotKey(nullptr, kHotkeyToggle);
    UnregisterHotKey(nullptr, kHotkeyExit);
    return 0;
}
