#include "ControlPanel.h"

#include <algorithm>
#include <array>

namespace {

constexpr wchar_t kControlPanelClass[] = L"BetterXcloudDLSS5ControlPanel";

constexpr int kIdTarget = 1001;
constexpr int kIdStart = 1002;
constexpr int kIdStop = 1003;
constexpr int kIdToggle = 1004;
constexpr int kIdOpenLogs = 1005;
constexpr int kIdOpenReShade = 1006;
constexpr int kIdOpenOptiScaler = 1007;
constexpr int kIdCopyDiagnostics = 1008;
constexpr int kIdNeuralUplift = 1009;
constexpr int kIdNrUpscaling = 1010;
constexpr int kIdSaveSettings = 1011;
constexpr int kIdRawConfig = 1012;
constexpr int kIdFrameGen = 1013;
constexpr int kIdFrameGenMultiplier = 1014;

HWND MakeControl(
    DWORD exStyle,
    const wchar_t* cls,
    const wchar_t* text,
    DWORD style,
    int x,
    int y,
    int width,
    int height,
    HWND parent,
    int id)
{
    return CreateWindowExW(
        exStyle,
        cls,
        text,
        style,
        x,
        y,
        width,
        height,
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr),
        nullptr);
}

} // namespace

std::unique_ptr<ControlPanel> ControlPanel::Create(HINSTANCE instance)
{
    auto panel = std::unique_ptr<ControlPanel>(new ControlPanel(instance));
    if (!panel->Initialize()) return {};
    return panel;
}

ControlPanel::~ControlPanel()
{
    if (font_) DeleteObject(font_);
}

bool ControlPanel::Initialize()
{
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance_;
    wc.lpszClassName = kControlPanelClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassExW(&wc);

    font_ = CreateFontW(
        -16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    hwnd_ = CreateWindowExW(
        WS_EX_APPWINDOW,
        kControlPanelClass,
        L"Better Xcloud DLSS5 - Control & Debug",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 620, 700,
        nullptr, nullptr, instance_, this);
    if (!hwnd_) return false;

    MakeControl(0, L"STATIC", L"Capture target:", WS_CHILD | WS_VISIBLE,
                20, 20, 120, 24, hwnd_, 0);

    targetCombo_ = MakeControl(
        0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        145, 17, 300, 200, hwnd_, kIdTarget);
    SendMessageW(targetCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Xbox App (recommended)"));
    SendMessageW(targetCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Chrome / Edge / Brave"));
    SendMessageW(targetCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Auto (prefer Xbox App)"));
    SendMessageW(targetCombo_, CB_SETCURSEL, 0, 0);

    startButton_ = MakeControl(
        0, L"BUTTON", L"Start mirror",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        465, 16, 125, 30, hwnd_, kIdStart);

    MakeControl(0, L"STATIC", L"Status", WS_CHILD | WS_VISIBLE,
                20, 62, 100, 22, hwnd_, 0);

    statusText_ = MakeControl(
        WS_EX_CLIENTEDGE, L"STATIC",
        L"Open Xbox Cloud Gaming in the Xbox App or browser, verify input, then click Start mirror.",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        20, 86, 570, 58, hwnd_, 0);

    MakeControl(0, L"STATIC", L"Live diagnostics", WS_CHILD | WS_VISIBLE,
                20, 156, 140, 22, hwnd_, 0);

    statsText_ = MakeControl(
        WS_EX_CLIENTEDGE, L"EDIT", L"Idle",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL |
            ES_READONLY | WS_VSCROLL,
        20, 180, 570, 180, hwnd_, 0);

    stopButton_ = MakeControl(
        0, L"BUTTON", L"Stop / Exit",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        20, 380, 120, 32, hwnd_, kIdStop);
    EnableWindow(stopButton_, FALSE);

    toggleButton_ = MakeControl(
        0, L"BUTTON", L"Hide mirror",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        150, 380, 120, 32, hwnd_, kIdToggle);
    EnableWindow(toggleButton_, FALSE);

    MakeControl(0, L"BUTTON", L"Open logs",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                280, 380, 95, 32, hwnd_, kIdOpenLogs);
    MakeControl(0, L"BUTTON", L"Copy diagnostics",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                385, 380, 130, 32, hwnd_, kIdCopyDiagnostics);

    backendConfigButton_ = MakeControl(
        0, L"BUTTON", L"Backend config",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        20, 425, 120, 32, hwnd_, kIdOpenReShade);
    secondaryConfigButton_ = MakeControl(
        0, L"BUTTON", L"OptiScaler.ini",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        150, 425, 120, 32, hwnd_, kIdOpenOptiScaler);
    rawConfigButton_ = MakeControl(
        0, L"BUTTON", L"Raw INI",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        280, 425, 95, 32, hwnd_, kIdRawConfig);

    MakeControl(
        0, L"STATIC",
        L"Mirror mode never injects input into Xbox App/Chrome. Use this window only when you want to configure or debug; minimize it while playing.",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        390, 423, 200, 52, hwnd_, 0);

    MakeControl(0, L"STATIC", L"Renderer settings (next launch)",
                WS_CHILD | WS_VISIBLE,
                20, 480, 220, 24, hwnd_, 0);

    neuralUpliftCheck_ = MakeControl(
        0, L"BUTTON", L"Neural Uplift",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        20, 508, 145, 28, hwnd_, kIdNeuralUplift);

    nrUpscalingCheck_ = MakeControl(
        0, L"BUTTON", L"NR Upscaling",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        175, 508, 145, 28, hwnd_, kIdNrUpscaling);

    MakeControl(
        0, L"BUTTON", L"Save settings",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        330, 505, 120, 32, hwnd_, kIdSaveSettings);

    settingsHint_ = MakeControl(
        0, L"STATIC",
        L"Backend-specific settings are applied on next mirror start.",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        20, 545, 520, 24, hwnd_, 0);

    MakeControl(0, L"STATIC", L"Frame Generation",
                WS_CHILD | WS_VISIBLE,
                20, 580, 160, 24, hwnd_, 0);

    frameGenCheck_ = MakeControl(
        0, L"BUTTON", L"XeFG enabled",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        20, 608, 145, 28, hwnd_, kIdFrameGen);

    frameGenMultiplierCombo_ = MakeControl(
        0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        175, 606, 120, 160, hwnd_, kIdFrameGenMultiplier);
    SendMessageW(frameGenMultiplierCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"2x"));
    SendMessageW(frameGenMultiplierCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"3x"));
    SendMessageW(frameGenMultiplierCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"4x"));

    MakeControl(
        0, L"STATIC",
        L"XeFG uses OptiFG from the DLSS carrier. Restart mirror after changes.",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        310, 606, 280, 42, hwnd_, 0);

    SetRendererSettings(true, false);
    SetFrameGenSettings(true, 2);

    EnumChildWindows(hwnd_, [](HWND child, LPARAM param) -> BOOL {
        reinterpret_cast<ControlPanel*>(param)->ApplyFont(child);
        return TRUE;
    }, reinterpret_cast<LPARAM>(this));

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    return true;
}

void ControlPanel::ApplyFont(HWND hwnd)
{
    if (font_) SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
}

CaptureTargetPreference ControlPanel::SelectedTarget() const
{
    const LRESULT selected = SendMessageW(targetCombo_, CB_GETCURSEL, 0, 0);
    if (selected == 1) return CaptureTargetPreference::Browser;
    if (selected == 2) return CaptureTargetPreference::Auto;
    return CaptureTargetPreference::XboxApp;
}

ControlPanelCommand ControlPanel::TakeCommand()
{
    const auto command = pending_;
    pending_ = ControlPanelCommand::None;
    return command;
}

void ControlPanel::Queue(ControlPanelCommand command)
{
    pending_ = command;
}

void ControlPanel::SetStatus(const std::wstring& text)
{
    if (statusText_) SetWindowTextW(statusText_, text.c_str());
}

void ControlPanel::SetStats(const std::wstring& text)
{
    if (statsText_) SetWindowTextW(statsText_, text.c_str());
}

void ControlPanel::SetRunning(bool running)
{
    running_ = running;
    if (startButton_) EnableWindow(startButton_, running ? FALSE : TRUE);
    if (stopButton_) EnableWindow(stopButton_, running ? TRUE : FALSE);
    if (toggleButton_) EnableWindow(toggleButton_, running ? TRUE : FALSE);
    if (targetCombo_) EnableWindow(targetCombo_, running ? FALSE : TRUE);
}

void ControlPanel::SetMirrorVisible(bool visible)
{
    mirrorVisible_ = visible;
    if (toggleButton_) {
        SetWindowTextW(toggleButton_, visible ? L"Hide mirror" : L"Show mirror");
    }
}

bool ControlPanel::NeuralUpliftEnabled() const
{
    return neuralUpliftCheck_ &&
           SendMessageW(neuralUpliftCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

bool ControlPanel::NrUpscalingEnabled() const
{
    return nrUpscalingCheck_ &&
           SendMessageW(nrUpscalingCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void ControlPanel::SetRendererSettings(bool firstOption, bool secondOption)
{
    if (neuralUpliftCheck_) {
        SendMessageW(
            neuralUpliftCheck_, BM_SETCHECK,
            firstOption ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    if (nrUpscalingCheck_) {
        SendMessageW(
            nrUpscalingCheck_, BM_SETCHECK,
            secondOption ? BST_CHECKED : BST_UNCHECKED, 0);
    }
}

bool ControlPanel::FrameGenEnabled() const
{
    return frameGenCheck_ &&
           SendMessageW(frameGenCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

int ControlPanel::FrameGenMultiplier() const
{
    if (!frameGenMultiplierCombo_) return 2;
    const LRESULT sel = SendMessageW(frameGenMultiplierCombo_, CB_GETCURSEL, 0, 0);
    return sel < 0 ? 2 : static_cast<int>(sel) + 2;
}

void ControlPanel::SetFrameGenSettings(bool enabled, int multiplier)
{
    if (frameGenCheck_) {
        SendMessageW(
            frameGenCheck_, BM_SETCHECK,
            enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    if (frameGenMultiplierCombo_) {
        const int sel = std::clamp(multiplier, 2, 4) - 2;
        SendMessageW(frameGenMultiplierCombo_, CB_SETCURSEL, sel, 0);
    }
}

void ControlPanel::SetBackendMode(bool optiScalerDirect)
{
    if (neuralUpliftCheck_) {
        SetWindowTextW(
            neuralUpliftCheck_,
            optiScalerDirect ? L"Neural Rendering" : L"Neural Uplift");
    }
    if (nrUpscalingCheck_) {
        SetWindowTextW(
            nrUpscalingCheck_,
            optiScalerDirect ? L"Run before SR (faster)" : L"NR Upscaling");
    }
    if (backendConfigButton_) {
        SetWindowTextW(
            backendConfigButton_,
            optiScalerDirect ? L"NR Tuning..." : L"ReShade.ini");
    }
    optiScalerDirect_ = optiScalerDirect;
    if (secondaryConfigButton_) {
        ShowWindow(secondaryConfigButton_, SW_SHOW);
        SetWindowTextW(
            secondaryConfigButton_,
            optiScalerDirect ? L"Native menu" : L"OptiScaler.ini");
    }
    if (rawConfigButton_) {
        ShowWindow(rawConfigButton_, optiScalerDirect ? SW_SHOW : SW_HIDE);
    }
    if (settingsHint_) {
        SetWindowTextW(
            settingsHint_,
            optiScalerDirect
                ? L"Direct NR defaults: enabled, 1 pass, 50% model scale. Pre-SR can reduce GPU cost."
                : L"Legacy defaults: Neural Uplift ON, NR Upscaling OFF.");
    }
}

LRESULT CALLBACK ControlPanel::WndProc(
    HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    ControlPanel* self = reinterpret_cast<ControlPanel*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        self = reinterpret_cast<ControlPanel*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }

    if (self) return self->HandleMessage(hwnd, message, wparam, lparam);
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

LRESULT ControlPanel::HandleMessage(
    HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
        case WM_COMMAND:
            if (HIWORD(wparam) == BN_CLICKED) {
                switch (LOWORD(wparam)) {
                    case kIdStart: Queue(ControlPanelCommand::Start); return 0;
                    case kIdStop: Queue(ControlPanelCommand::Stop); return 0;
                    case kIdToggle: Queue(ControlPanelCommand::ToggleMirror); return 0;
                    case kIdOpenLogs: Queue(ControlPanelCommand::OpenLogs); return 0;
                    case kIdOpenReShade:
                        Queue(optiScalerDirect_
                            ? ControlPanelCommand::OpenNrTuner
                            : ControlPanelCommand::OpenReShadeConfig);
                        return 0;
                    case kIdOpenOptiScaler:
                        Queue(optiScalerDirect_
                            ? ControlPanelCommand::OpenBackendMenu
                            : ControlPanelCommand::OpenOptiScalerConfig);
                        return 0;
                    case kIdRawConfig: Queue(ControlPanelCommand::OpenOptiScalerConfig); return 0;
                    case kIdCopyDiagnostics: Queue(ControlPanelCommand::CopyDiagnostics); return 0;
                    case kIdSaveSettings: Queue(ControlPanelCommand::SaveSettings); return 0;
                    default: break;
                }
            }
            break;
        case WM_CLOSE:
            alive_ = false;
            Queue(ControlPanelCommand::Close);
            DestroyWindow(hwnd);
            hwnd_ = nullptr;
            return 0;
        case WM_DESTROY:
            alive_ = false;
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}
