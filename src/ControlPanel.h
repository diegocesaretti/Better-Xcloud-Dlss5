#pragma once

#include "WindowFinder.h"

#include <windows.h>

#include <memory>
#include <string>

enum class ControlPanelCommand {
    None,
    Start,
    Stop,
    ToggleMirror,
    OpenLogs,
    OpenReShadeConfig,
    OpenOptiScalerConfig,
    CopyDiagnostics,
    Close
};

class ControlPanel {
public:
    static std::unique_ptr<ControlPanel> Create(HINSTANCE instance);
    ~ControlPanel();

    HWND Hwnd() const { return hwnd_; }
    bool Alive() const { return alive_; }
    CaptureTargetPreference SelectedTarget() const;
    ControlPanelCommand TakeCommand();

    void SetStatus(const std::wstring& text);
    void SetStats(const std::wstring& text);
    void SetRunning(bool running);
    void SetMirrorVisible(bool visible);

private:
    explicit ControlPanel(HINSTANCE instance) : instance_(instance) {}
    bool Initialize();

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

    void Queue(ControlPanelCommand command);
    void ApplyFont(HWND hwnd);

    HINSTANCE instance_{};
    HWND hwnd_{};
    HWND targetCombo_{};
    HWND statusText_{};
    HWND statsText_{};
    HWND startButton_{};
    HWND stopButton_{};
    HWND toggleButton_{};
    HFONT font_{};
    bool alive_{true};
    bool running_{false};
    bool mirrorVisible_{true};
    ControlPanelCommand pending_{ControlPanelCommand::None};
};
