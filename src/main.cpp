#include "WindowCapture.h"
#include "WindowFinder.h"
#include "ControlPanel.h"

#include "D3D12Renderer.h"
#include "DLSSBackend.h"
#include "TemporalGuides.h"

#include <windows.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <winver.h>
#include <Xinput.h>
#include <dwmapi.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>

#include <winrt/base.h>

namespace {

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

constexpr wchar_t kOverlayClass[] = L"BetterXcloudDLSS5Overlay";
LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_ERASEBKGND:
            return 1;
        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wparam, lparam);
    }
}

HWND CreateOverlay(const RECT& bounds, HWND owner)
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
        WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        kOverlayClass,
        L"Better Xcloud DLSS5",
        WS_POPUP,
        bounds.left,
        bounds.top,
        width,
        height,
        owner,
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
        HWND_TOP,
        bounds.left,
        bounds.top,
        bounds.right - bounds.left,
        bounds.bottom - bounds.top,
        SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

int ConnectedXInputControllers()
{
    int count = 0;
    for (DWORD index = 0; index < XUSER_MAX_COUNT; ++index) {
        XINPUT_STATE state{};
        if (XInputGetState(index, &state) == ERROR_SUCCESS) {
            ++count;
        }
    }
    return count;
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

void PumpMessages()
{
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void OpenPath(const std::filesystem::path& path)
{
    ShellExecuteW(
        nullptr,
        L"open",
        path.c_str(),
        nullptr,
        path.has_extension() ? path.parent_path().c_str() : nullptr,
        SW_SHOWNORMAL);
}

bool CopyTextToClipboard(HWND owner, const std::wstring& text)
{
    if (!OpenClipboard(owner)) return false;
    EmptyClipboard();

    const SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory) {
        CloseClipboard();
        return false;
    }

    void* destination = GlobalLock(memory);
    if (!destination) {
        GlobalFree(memory);
        CloseClipboard();
        return false;
    }

    memcpy(destination, text.c_str(), bytes);
    GlobalUnlock(memory);

    if (!SetClipboardData(CF_UNICODETEXT, memory)) {
        GlobalFree(memory);
        CloseClipboard();
        return false;
    }

    CloseClipboard();
    return true;
}

std::wstring TargetKindName(CaptureTargetPreference kind)
{
    switch (kind) {
        case CaptureTargetPreference::XboxApp: return L"Xbox App";
        case CaptureTargetPreference::Browser: return L"Browser";
        case CaptureTargetPreference::Auto: return L"Auto";
    }
    return L"Unknown";
}

bool SaveRendererSettings(
    const std::filesystem::path& configPath,
    bool optiScalerDirect,
    bool firstOption,
    bool secondOption)
{
    if (optiScalerDirect) {
        const bool a = WritePrivateProfileStringW(
            L"DlssNr",
            L"Enabled",
            firstOption ? L"true" : L"false",
            configPath.c_str()) != FALSE;
        const bool b = WritePrivateProfileStringW(
            L"DlssNr",
            L"RunBeforeSR",
            secondOption ? L"true" : L"false",
            configPath.c_str()) != FALSE;
        return a && b;
    }

    const bool a = WritePrivateProfileStringW(
        L"RenoDX.DLSS5",
        L"NeuralUplift",
        firstOption ? L"1" : L"0",
        configPath.c_str()) != FALSE;
    const bool b = WritePrivateProfileStringW(
        L"RenoDX.DLSS5",
        L"NREnableUpscaling",
        secondOption ? L"1" : L"0",
        configPath.c_str()) != FALSE;
    return a && b;
}

void LoadRendererSettings(
    ControlPanel& panel,
    const std::filesystem::path& configPath,
    bool optiScalerDirect)
{
    if (optiScalerDirect) {
        const bool enabled =
            GetPrivateProfileIntW(
                L"DlssNr", L"Enabled", 1, configPath.c_str()) != 0;
        const bool runBefore =
            GetPrivateProfileIntW(
                L"DlssNr", L"RunBeforeSR", 0, configPath.c_str()) != 0;
        panel.SetRendererSettings(enabled, runBefore);
        return;
    }

    const bool neuralUplift =
        GetPrivateProfileIntW(
            L"RenoDX.DLSS5", L"NeuralUplift", 1, configPath.c_str()) != 0;
    const bool nrUpscaling =
        GetPrivateProfileIntW(
            L"RenoDX.DLSS5", L"NREnableUpscaling", 0, configPath.c_str()) != 0;
    panel.SetRendererSettings(neuralUplift, nrUpscaling);
}

bool WaitForFirstFrame(WindowCapture& capture, CapturedFrame& frame)
{
    using namespace std::chrono_literals;
    const auto deadline = std::chrono::steady_clock::now() + 15s;

    while (std::chrono::steady_clock::now() < deadline) {
        if (capture.TryGetLatest(frame)) return true;

        PumpMessages();
        std::this_thread::sleep_for(2ms);
    }
    return false;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    // Keep an explicit WinMM import so a local OptiScaler proxy named winmm.dll
    // is resolved by the Windows loader before wWinMain.
    (void)timeGetTime();
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

    auto panel = ControlPanel::Create(GetModuleHandleW(nullptr));
    if (!panel) {
        ErrorBox(L"Unable to create the Better Xcloud DLSS5 control panel.");
        return 11;
    }

    BrowserWindowInfo browser;
    std::wstring diagnosticsText =
        L"Better Xcloud DLSS5\r\n"
        L"Status: waiting for target selection.\r\n";

    const auto moduleDirForUi = ModuleDirectory();
    const auto reshadeIniPath = moduleDirForUi / L"ReShade.ini";
    const auto optiIniPath = moduleDirForUi / L"OptiScaler.ini";
    const bool optiScalerDirect =
        std::filesystem::exists(moduleDirForUi / L"BACKEND_OPTISCALER_DIRECT_NR.txt");
    const auto activeConfigPath =
        optiScalerDirect ? optiIniPath : reshadeIniPath;
    panel->SetBackendMode(optiScalerDirect);
    LoadRendererSettings(*panel, activeConfigPath, optiScalerDirect);

    while (panel->Alive() && !browser.hwnd) {
        PumpMessages();

        switch (panel->TakeCommand()) {
            case ControlPanelCommand::Start: {
                const auto preference = panel->SelectedTarget();
                browser = FindBestTargetWindow(preference);
                if (!browser.hwnd) {
                    panel->SetStatus(
                        preference == CaptureTargetPreference::XboxApp
                            ? L"Xbox App window not found. Open the Xbox App and Cloud Gaming, then click Start mirror again."
                            : preference == CaptureTargetPreference::Browser
                                ? L"Supported xCloud browser window not found. Open Xbox Cloud Gaming in Chrome/Edge/Brave, then retry."
                                : L"No Xbox App or supported xCloud browser window was found. Open one and retry.");
                } else {
                    std::wstring status = L"Target found: " + browser.processName;
                    if (!browser.title.empty()) status += L" — " + browser.title;
                    panel->SetStatus(status + L"\r\nInitializing capture and DLSS carrier...");
                    panel->SetRunning(true);
                }
                break;
            }
            case ControlPanelCommand::OpenLogs:
                OpenPath(moduleDirForUi);
                break;
            case ControlPanelCommand::OpenReShadeConfig:
                OpenPath(activeConfigPath);
                break;
            case ControlPanelCommand::OpenOptiScalerConfig:
                OpenPath(optiIniPath);
                break;
            case ControlPanelCommand::CopyDiagnostics:
                CopyTextToClipboard(panel->Hwnd(), diagnosticsText);
                panel->SetStatus(L"Diagnostics copied to clipboard.");
                break;
            case ControlPanelCommand::SaveSettings:
                if (SaveRendererSettings(
                        activeConfigPath,
                        optiScalerDirect,
                        panel->NeuralUpliftEnabled(),
                        panel->NrUpscalingEnabled())) {
                    panel->SetStatus(
                        L"Renderer settings saved. They will apply the next time the mirror starts.");
                } else {
                    panel->SetStatus(
                        optiScalerDirect
                            ? L"Could not write OptiScaler.ini."
                            : L"Could not write ReShade.ini.");
                }
                break;
            case ControlPanelCommand::Close:
            case ControlPanelCommand::Stop:
                return 0;
            default:
                break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    if (!panel->Alive() || !browser.hwnd) return 0;

    std::ofstream liveLog(
        ModuleDirectory() / L"XCloudDLSS5-live.log",
        std::ios::out | std::ios::trunc);
    liveLog << "Better Xcloud DLSS5 live session\n";
    liveLog << "mode=full-window-mirror inputPolicy=target-owned controlPanel=separate\n";
    liveLog << "targetProcess=";
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
    liveLog << "targetKind=";
    {
        const std::wstring kind = TargetKindName(browser.kind);
        for (wchar_t ch : kind) liveLog << (ch <= 0x7f ? char(ch) : '?');
    }
    liveLog << "\n";
    liveLog.flush();

    WindowCapture capture;
    const bool preferMonitorCrop =
        browser.kind == CaptureTargetPreference::XboxApp;

    bool captureStarted = preferMonitorCrop
        ? capture.StartMonitorCrop(browser.hwnd)
        : capture.Start(browser.hwnd);

    std::wstring captureMode =
        preferMonitorCrop ? L"monitor-crop" : L"window";

    if (!captureStarted && preferMonitorCrop) {
        const std::wstring monitorError = capture.LastError();
        captureStarted = capture.Start(browser.hwnd);
        captureMode = L"window-fallback";
        panel->SetStatus(
            L"Monitor-rate Xbox capture failed; using window capture fallback.\r\n" +
            monitorError);
    }

    if (!captureStarted) {
        ErrorBox(capture.LastError());
        return 12;
    }

    liveLog << "captureMode=";
    for (wchar_t ch : captureMode) liveLog << (ch <= 0x7f ? char(ch) : '?');
    liveLog << "\n";
    liveLog.flush();

    CapturedFrame frame;
    if (!WaitForFirstFrame(capture, frame)) {
        std::wstring reason = capture.LastError();
        if (reason.empty()) {
            reason = L"No frame arrived from the target window. Make sure Xbox Cloud Gaming is visible.";
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

    HWND overlay = CreateOverlay(browser.visualBounds, browser.hwnd);
    if (!overlay) {
        ErrorBox(L"Unable to create the processed-video overlay.");
        return 14;
    }

    const BOOL overlayExcludedFromCapture =
        SetWindowDisplayAffinity(overlay, WDA_EXCLUDEFROMCAPTURE);
    liveLog << "overlayExcludedFromCapture="
            << (overlayExcludedFromCapture ? 1 : 0) << "\n";
    liveLog.flush();

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

    panel->SetStatus(
        L"Mirror running on " + TargetKindName(browser.kind) +
        L". Minimize this panel while playing. The target application keeps all input.");
    panel->SetMirrorVisible(true);


    // Mirror mode is strictly observational: never activate, focus, subclass or
    // synthesize input into the target. Xbox App/Chrome owns keyboard/mouse/gamepad.
    const int xinputControllers = ConnectedXInputControllers();
    liveLog << "xinputControllersVisibleToHost=" << xinputControllers << "\n";
    liveLog << "targetForegroundAfterInit="
            << (TargetHasFocus(browser.hwnd) ? 1 : 0)
            << " foregroundHwnd=0x" << std::hex
            << reinterpret_cast<std::uintptr_t>(GetForegroundWindow())
            << std::dec << "\n";
    liveLog.flush();

    TemporalGuideGenerator guides;
    bool overlayEnabled = true;
    bool running = true;
    bool forceReset = true;
    std::uint64_t lastSequence = 0;

    const auto ptsOrigin = std::chrono::steady_clock::now();
    auto previousFrameTime = ptsOrigin;
    auto nextPositionSync = ptsOrigin;
    auto statsWindowStart = ptsOrigin;
    std::uint64_t statsInputFrames = 0;
    std::uint64_t statsRenderedFrames = 0;
    std::uint64_t statsDroppedFrames = 0;
    std::uint64_t statsArrivalStart = capture.FrameArrivals();
    double statsProcessingMs = 0.0;

    while (running && IsWindow(browser.hwnd)) {
        PumpMessages();

        switch (panel->TakeCommand()) {
            case ControlPanelCommand::Stop:
            case ControlPanelCommand::Close:
                liveLog << "exitReason=control-panel-stop\n";
                liveLog.flush();
                running = false;
                break;
            case ControlPanelCommand::ToggleMirror:
                overlayEnabled = !overlayEnabled;
                panel->SetMirrorVisible(overlayEnabled);
                if (!overlayEnabled) ShowWindow(overlay, SW_HIDE);
                break;
            case ControlPanelCommand::OpenLogs:
                OpenPath(ModuleDirectory());
                break;
            case ControlPanelCommand::OpenReShadeConfig:
                OpenPath(
                    optiScalerDirect
                        ? ModuleDirectory() / L"OptiScaler.ini"
                        : ModuleDirectory() / L"ReShade.ini");
                break;
            case ControlPanelCommand::OpenOptiScalerConfig:
                OpenPath(ModuleDirectory() / L"OptiScaler.ini");
                break;
            case ControlPanelCommand::CopyDiagnostics:
                if (CopyTextToClipboard(panel->Hwnd(), diagnosticsText)) {
                    panel->SetStatus(L"Diagnostics copied to clipboard. Mirror is still running.");
                }
                break;
            case ControlPanelCommand::SaveSettings:
                if (SaveRendererSettings(
                        optiScalerDirect
                            ? ModuleDirectory() / L"OptiScaler.ini"
                            : ModuleDirectory() / L"ReShade.ini",
                        optiScalerDirect,
                        panel->NeuralUpliftEnabled(),
                        panel->NrUpscalingEnabled())) {
                    panel->SetStatus(
                        L"Renderer settings saved. Stop and relaunch the mirror to apply them.");
                } else {
                    panel->SetStatus(
                        optiScalerDirect
                            ? L"Could not write OptiScaler.ini."
                            : L"Could not write ReShade.ini.");
                }
                break;
            default:
                break;
        }
        if (!panel->Alive()) running = false;
        if (!running) break;

        const auto now = std::chrono::steady_clock::now();

        if (now >= nextPositionSync) {
            PlaceOverlay(overlay, browser.hwnd);

            if (capture.UsingMonitorCrop() &&
                !capture.UpdateMonitorCrop(browser.hwnd)) {
                liveLog << "exitReason=monitor-crop-update-failed\n";
                liveLog.flush();
                panel->SetStatus(
                    L"Xbox App moved to another monitor or its capture rectangle became invalid. Stop and restart the mirror.");
                running = false;
                break;
            }

            nextPositionSync = now + std::chrono::milliseconds(250);

            const bool shouldShow =
                overlayEnabled && TargetHasFocus(browser.hwnd);
            if (shouldShow) {
                ShowWindow(overlay, SW_SHOWNOACTIVATE);
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
            frame = std::move(latest);
            previousFrameTime = std::chrono::steady_clock::now();
            forceReset = true;
            lastSequence = 0;
            liveLog << "resizeRenderer=initialized dlssAvailable=1\n";
            liveLog.flush();
            continue;
        }

        // Latest-frame capture intentionally drops source frames whenever
        // processing is slower than the monitor cadence. A skipped source frame
        // is NOT a temporal discontinuity: resetting DLSS-NR on every skip made
        // a 60 Hz source at ~30 rendered FPS reset neural history every frame.
        const bool dropped =
            latest.drainedFrames > 1 ||
            (lastSequence != 0 && latest.sequence != lastSequence + 1);

        const auto captureGap = latest.capturedAt - previousFrameTime;
        const bool temporalDiscontinuity =
            lastSequence != 0 &&
            (captureGap > std::chrono::milliseconds(250) ||
             latest.sequence > lastSequence + 30);

        const HistoryReset reset =
            forceReset ? HistoryReset::FirstFrame :
            (temporalDiscontinuity ? HistoryReset::Drop : HistoryReset::None);

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
            const std::uint64_t arrivalNow = capture.FrameArrivals();
            const std::uint64_t arrivals =
                arrivalNow >= statsArrivalStart ? arrivalNow - statsArrivalStart : 0;
            const double captureArrivalFps = statsSeconds > 0.0
                ? static_cast<double>(arrivals) / statsSeconds
                : 0.0;

            liveLog << std::fixed << std::setprecision(2)
                    << "fps=" << fps
                    << " captureArrivalFps=" << captureArrivalFps
                    << " avgProcessingMs=" << avgMs
                    << " neuralGpuMs=" << renderer->LastNeuralGpuMs()
                    << " droppedCaptureFrames=" << statsDroppedFrames
                    << " peakVramMiB=" << renderer->PeakLocalVideoMemoryMiB()
                    << " evaluations=" << renderer->DLSSEvaluations()
                    << "\n";
            liveLog.flush();

            std::wostringstream stats;
            stats << std::fixed << std::setprecision(2)
                  << L"Target: " << TargetKindName(browser.kind)
                  << L"  [" << browser.processName << L"]\r\n"
                  << L"Capture: " << frame.width << L"x" << frame.height << L"\r\n"
                  << L"DLSS carrier: active\r\n"
                  << L"Capture mode: " << captureMode << L"\r\n"
                  << L"Capture arrivals: " << captureArrivalFps << L" fps\r\n"
                  << L"Rendered mirror: " << fps << L" fps\r\n"
                  << L"Average processing: " << avgMs << L" ms\r\n"
                  << L"Neural GPU: " << renderer->LastNeuralGpuMs() << L" ms\r\n"
                  << L"Dropped capture frames: " << statsDroppedFrames << L"\r\n"
                  << L"Peak VRAM: " << renderer->PeakLocalVideoMemoryMiB() << L" MiB\r\n"
                  << L"DLSS evaluations: " << renderer->DLSSEvaluations() << L"\r\n"
                  << L"XInput controllers visible to host: " << xinputControllers << L"\r\n"
                  << L"Target foreground: " << (TargetHasFocus(browser.hwnd) ? L"yes" : L"no");
            panel->SetStats(stats.str());

            diagnosticsText =
                L"Better Xcloud DLSS5 diagnostics\r\n"
                L"Mode: full-window mirror / target-owned input\r\n"
                L"Neural backend: " +
                std::wstring(optiScalerDirect ? L"OptiScaler built-in direct NR" : L"Legacy RenoDX") +
                L"\r\n" +
                stats.str() +
                L"\r\nRuntime: " + ModuleDirectory().wstring() +
                L"\r\nLive log: " + (ModuleDirectory() / L"XCloudDLSS5-live.log").wstring() +
                L"\r\nBackend log: " +
                (optiScalerDirect
                    ? (ModuleDirectory() / L"OptiScaler.log").wstring()
                    : (ModuleDirectory() / L"ReShade.log").wstring()) +
                L"\r\nDLSS log: " + (ModuleDirectory() / L"DLSSVideoPlayer.log").wstring();

            statsWindowStart = statsNow;
            statsInputFrames = 0;
            statsRenderedFrames = 0;
            statsDroppedFrames = 0;
            statsArrivalStart = arrivalNow;
            statsProcessingMs = 0.0;
        }
    }

    if (!IsWindow(browser.hwnd)) {
        liveLog << "exitReason=target-window-closed\n";
    }
    liveLog << "session=ended\n";
    liveLog.flush();

    ShowWindow(overlay, SW_HIDE);
    renderer.reset();
    capture.Stop();
    DestroyWindow(overlay);
    panel->SetRunning(false);
    panel->SetStatus(L"Mirror stopped.");

    return 0;
}
