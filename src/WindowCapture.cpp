#include "WindowCapture.h"

#include <dxgi1_2.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <iterator>
#include <sstream>

using Microsoft::WRL::ComPtr;
namespace capture = winrt::Windows::Graphics::Capture;
namespace directx = winrt::Windows::Graphics::DirectX;
namespace d3d11rt = winrt::Windows::Graphics::DirectX::Direct3D11;

WindowCapture::~WindowCapture()
{
    Stop();
}

void WindowCapture::SetError(const wchar_t* text, HRESULT hr)
{
    std::wostringstream out;
    out << text;
    if (FAILED(hr)) {
        out << L" (HRESULT 0x" << std::hex << std::uppercase
            << static_cast<unsigned long>(hr) << L")";
    }
    lastError_ = out.str();
}

bool WindowCapture::CreateDevices()
{
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    static constexpr D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    D3D_FEATURE_LEVEL selected{};
    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        flags,
        levels,
        static_cast<UINT>(std::size(levels)),
        D3D11_SDK_VERSION,
        d3dDevice_.ReleaseAndGetAddressOf(),
        &selected,
        d3dContext_.ReleaseAndGetAddressOf());

    // Some systems reject 11_1 when the installed runtime predates it.
    if (hr == E_INVALIDARG) {
        hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            flags,
            levels + 1,
            static_cast<UINT>(std::size(levels) - 1),
            D3D11_SDK_VERSION,
            d3dDevice_.ReleaseAndGetAddressOf(),
            &selected,
            d3dContext_.ReleaseAndGetAddressOf());
    }

    if (FAILED(hr)) {
        SetError(L"Unable to create the Direct3D 11 capture device", hr);
        return false;
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    hr = d3dDevice_.As(&dxgiDevice);
    if (FAILED(hr)) {
        SetError(L"Unable to query IDXGIDevice for capture", hr);
        return false;
    }

    winrt::com_ptr<IInspectable> inspectable;
    hr = CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.Get(), inspectable.put());
    if (FAILED(hr)) {
        SetError(L"Unable to create the WinRT Direct3D device", hr);
        return false;
    }

    winrtDevice_ = inspectable.as<d3d11rt::IDirect3DDevice>();
    return true;
}

bool WindowCapture::CreateCaptureItem(HWND hwnd)
{
    try {
        auto interop = winrt::get_activation_factory<capture::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        winrt::check_hresult(interop->CreateForWindow(
            hwnd,
            winrt::guid_of<capture::GraphicsCaptureItem>(),
            winrt::put_abi(item_)));
    } catch (const winrt::hresult_error& error) {
        SetError(L"Windows Graphics Capture could not attach to the browser window", error.code());
        return false;
    }

    return item_ != nullptr;
}

bool WindowCapture::Start(HWND hwnd)
{
    Stop();
    lastError_.clear();

    try {
        if (!CreateDevices() || !CreateCaptureItem(hwnd)) return false;

        const auto size = item_.Size();
        if (size.Width <= 0 || size.Height <= 0) {
            SetError(L"The selected browser window has no capturable area");
            return false;
        }

        framePool_ = capture::Direct3D11CaptureFramePool::CreateFreeThreaded(
            winrtDevice_,
            directx::DirectXPixelFormat::B8G8R8A8UIntNormalized,
            2,
            size);

        session_ = framePool_.CreateCaptureSession(item_);

        // These properties are version-dependent. Failure is harmless; capture
        // still works, just with the OS border/cursor policy.
        try { session_.IsCursorCaptureEnabled(false); } catch (...) {}
        try { session_.IsBorderRequired(false); } catch (...) {}

        session_.StartCapture();
        sequence_ = 0;
        return true;
    } catch (const winrt::hresult_error& error) {
        SetError(L"Unable to start Windows Graphics Capture", error.code());
        Stop();
        return false;
    }
}

void WindowCapture::Stop()
{
    if (session_) {
        try { session_.Close(); } catch (...) {}
    }
    if (framePool_) {
        try { framePool_.Close(); } catch (...) {}
    }

    session_ = nullptr;
    framePool_ = nullptr;
    item_ = nullptr;
    winrtDevice_ = nullptr;
    staging_.Reset();
    d3dContext_.Reset();
    d3dDevice_.Reset();
    stagingDesc_ = {};
    sequence_ = 0;
}

bool WindowCapture::EnsureStaging(ID3D11Texture2D* source)
{
    if (!source) return false;

    D3D11_TEXTURE2D_DESC sourceDesc{};
    source->GetDesc(&sourceDesc);

    if (staging_ &&
        stagingDesc_.Width == sourceDesc.Width &&
        stagingDesc_.Height == sourceDesc.Height &&
        stagingDesc_.Format == sourceDesc.Format) {
        return true;
    }

    D3D11_TEXTURE2D_DESC desc = sourceDesc;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;

    staging_.Reset();
    const HRESULT hr = d3dDevice_->CreateTexture2D(&desc, nullptr, staging_.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        SetError(L"Unable to create the capture readback texture", hr);
        return false;
    }

    stagingDesc_ = desc;
    return true;
}

bool WindowCapture::CopyToCpu(ID3D11Texture2D* source, CapturedFrame& out)
{
    if (!EnsureStaging(source)) return false;

    d3dContext_->CopyResource(staging_.Get(), source);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    const HRESULT hr = d3dContext_->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        SetError(L"Unable to map the captured frame", hr);
        return false;
    }

    const std::uint32_t width = stagingDesc_.Width;
    const std::uint32_t height = stagingDesc_.Height;
    const size_t tightPitch = static_cast<size_t>(width) * 4u;
    const size_t bytes = tightPitch * static_cast<size_t>(height);

    out.bgra.resize(bytes);
    const auto* src = static_cast<const std::uint8_t*>(mapped.pData);
    auto* dst = out.bgra.data();

    for (std::uint32_t y = 0; y < height; ++y) {
        std::memcpy(dst + static_cast<size_t>(y) * tightPitch,
                    src + static_cast<size_t>(y) * mapped.RowPitch,
                    tightPitch);
    }

    d3dContext_->Unmap(staging_.Get(), 0);

    out.width = width;
    out.height = height;
    out.capturedAt = std::chrono::steady_clock::now();
    return true;
}

bool WindowCapture::TryGetLatest(CapturedFrame& out)
{
    if (!framePool_) return false;

    try {
        capture::Direct3D11CaptureFrame newest{nullptr};
        std::uint32_t drained = 0;

        for (;;) {
            auto next = framePool_.TryGetNextFrame();
            if (!next) break;
            newest = std::move(next);
            ++drained;
            ++sequence_;
        }

        if (!newest) return false;

        auto access = newest.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        ComPtr<ID3D11Texture2D> texture;
        const HRESULT hr = access->GetInterface(IID_PPV_ARGS(texture.ReleaseAndGetAddressOf()));
        if (FAILED(hr)) {
            SetError(L"Unable to access the captured Direct3D texture", hr);
            return false;
        }

        CapturedFrame frame;
        if (!CopyToCpu(texture.Get(), frame)) return false;
        frame.sequence = sequence_;
        frame.drainedFrames = std::max<std::uint32_t>(1, drained);
        out = std::move(frame);
        return true;
    } catch (const winrt::hresult_error& error) {
        SetError(L"Windows Graphics Capture failed while receiving a frame", error.code());
        return false;
    }
}
