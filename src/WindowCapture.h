#pragma once

#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <chrono>
#include <cstdint>
#include <vector>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

struct CapturedFrame {
    std::vector<std::uint8_t> bgra;
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint64_t sequence{};
    std::uint32_t drainedFrames{1};
    std::chrono::steady_clock::time_point capturedAt{};
};

class WindowCapture {
public:
    WindowCapture() = default;
    ~WindowCapture();

    WindowCapture(const WindowCapture&) = delete;
    WindowCapture& operator=(const WindowCapture&) = delete;

    bool Start(HWND hwnd);
    void Stop();

    // Drains every currently queued WGC frame and returns only the newest one.
    // This is deliberate for cloud gaming: latency is preferable to completeness.
    bool TryGetLatest(CapturedFrame& out);

    const std::wstring& LastError() const noexcept { return lastError_; }

private:
    bool CreateDevices();
    bool CreateCaptureItem(HWND hwnd);
    bool EnsureStaging(ID3D11Texture2D* source);
    bool CopyToCpu(ID3D11Texture2D* source, CapturedFrame& out);
    void SetError(const wchar_t* text, HRESULT hr = S_OK);

    Microsoft::WRL::ComPtr<ID3D11Device> d3dDevice_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3dContext_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging_;

    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice winrtDevice_{nullptr};
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem item_{nullptr};
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool framePool_{nullptr};
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession session_{nullptr};

    D3D11_TEXTURE2D_DESC stagingDesc_{};
    std::uint64_t sequence_{};
    std::wstring lastError_;
};
