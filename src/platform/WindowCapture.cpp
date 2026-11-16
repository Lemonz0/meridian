#include "WindowCapture.h"

#include "Log.h"

#include <d3d11.h>
#include <dwmapi.h>
#include <dxgi.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>

using Microsoft::WRL::ComPtr;
namespace wgc = winrt::Windows::Graphics::Capture;
namespace wd3d = winrt::Windows::Graphics::DirectX::Direct3D11;

namespace meridian::platform {

namespace {
struct Device {
    ComPtr<ID3D11Device> d3d;
    ComPtr<ID3D11DeviceContext> context;
    wd3d::IDirect3DDevice winrtDevice { nullptr };
};

Device* device()
{
    static Device* dev = [] () -> Device* {
        auto* d = new Device;
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION,
                &d->d3d, nullptr, &d->context)))
            return nullptr;
        ComPtr<IDXGIDevice> dxgi;
        d->d3d.As(&dxgi);
        winrt::com_ptr<::IInspectable> inspectable;
        if (FAILED(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(), inspectable.put())))
            return nullptr;
        d->winrtDevice = inspectable.as<wd3d::IDirect3DDevice>();
        return d;
    }();
    return dev;
}

template<typename T>
ComPtr<T> surfaceTexture(const wd3d::IDirect3DSurface& surface)
{
    auto access = surface.as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
    ComPtr<T> tex;
    access->GetInterface(IID_PPV_ARGS(&tex));
    return tex;
}
} // namespace

bool windowCaptureSupported()
{
    static int cached = -1; // probe once; the answer does not change at runtime
    if (cached >= 0)
        return cached == 1;
    try {
        bool api = wgc::GraphicsCaptureSession::IsSupported();
        bool dev = device() != nullptr;
        log(L"capture support: api=%d device=%d", api, dev);
        cached = api && dev;
    } catch (const winrt::hresult_error& e) {
        log(L"capture support: hresult 0x%08X %s", unsigned(e.code()), e.message().c_str());
        // Transient (e.g. called inside an input-synchronous SendMessage): don't cache.
        if (e.code() == RPC_E_CANTCALLOUT_ININPUTSYNCCALL)
            return false;
        cached = 0;
    } catch (...) {
        log(L"capture support: unknown exception");
        cached = 0;
    }
    return cached == 1;
}

void requestBorderlessCapture()
{
    try {
        if (winrt::Windows::Foundation::Metadata::ApiInformation::IsMethodPresent(
                L"Windows.Graphics.Capture.GraphicsCaptureAccess", L"RequestAccessAsync"))
            wgc::GraphicsCaptureAccess::RequestAccessAsync(wgc::GraphicsCaptureAccessKind::Borderless).get();
    } catch (...) {
    }
}

std::optional<services::Bitmap> captureClientRect(HWND hwnd, const RECT& clientRect, int maxWidth)
{
    Device* dev = device();
    if (!dev || clientRect.right <= clientRect.left || clientRect.bottom <= clientRect.top) {
        log(L"capture: no device (%d) or empty rect %ld,%ld,%ld,%ld", dev != nullptr, clientRect.left, clientRect.top, clientRect.right, clientRect.bottom);
        return std::nullopt;
    }
    ULONGLONG started = GetTickCount64();
    try {
        auto interop = winrt::get_activation_factory<wgc::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        wgc::GraphicsCaptureItem item { nullptr };
        winrt::check_hresult(interop->CreateForWindow(hwnd, winrt::guid_of<wgc::GraphicsCaptureItem>(), winrt::put_abi(item)));

        auto pool = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded(dev->winrtDevice,
            winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 1, item.Size());
        auto session = pool.CreateCaptureSession(item);
        try { session.IsBorderRequired(false); } catch (...) { }
        try { session.IsCursorCaptureEnabled(false); } catch (...) { }

        HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        wd3d::IDirect3DSurface surface { nullptr };
        winrt::Windows::Graphics::SizeInt32 frameSize {};
        std::atomic<bool> got { false };
        auto token = pool.FrameArrived([&](auto& sender, auto&) {
            if (got.exchange(true))
                return;
            if (auto frame = sender.TryGetNextFrame()) {
                surface = frame.Surface();
                frameSize = frame.ContentSize();
            }
            SetEvent(ready);
        });
        session.StartCapture();
        // When capturing our own window from its UI thread, the capture
        // pipeline needs that thread to answer sent (cross-thread) messages
        // before a frame arrives. Dispatch *sent* messages only: posted input
        // stays queued, so nothing re-enters tab switching while we wait.
        DWORD wait = WAIT_TIMEOUT;
        ULONGLONG deadline = GetTickCount64() + 250;
        for (;;) {
            ULONGLONG now = GetTickCount64();
            if (now >= deadline)
                break;
            wait = MsgWaitForMultipleObjects(1, &ready, FALSE, DWORD(deadline - now), QS_SENDMESSAGE);
            if (wait == WAIT_OBJECT_0)
                break;
            if (wait == WAIT_OBJECT_0 + 1) {
                MSG m;
                PeekMessageW(&m, nullptr, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE);
                continue;
            }
            break;
        }
        pool.FrameArrived(token);
        CloseHandle(ready);

        std::optional<services::Bitmap> result;
        if (wait == WAIT_OBJECT_0 && surface) {
            // Frame origin is the window's visual (DWM extended frame) bounds.
            RECT visual {};
            DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &visual, sizeof(visual));
            POINT origin { 0, 0 };
            ClientToScreen(hwnd, &origin);
            int x0 = std::clamp<int>(origin.x - visual.left + clientRect.left, 0, frameSize.Width);
            int y0 = std::clamp<int>(origin.y - visual.top + clientRect.top, 0, frameSize.Height);
            int x1 = std::clamp<int>(x0 + (clientRect.right - clientRect.left), 0, frameSize.Width);
            int y1 = std::clamp<int>(y0 + (clientRect.bottom - clientRect.top), 0, frameSize.Height);
            int cw = x1 - x0, ch = y1 - y0;
            if (cw > 0 && ch > 0) {
                auto texture = surfaceTexture<ID3D11Texture2D>(surface);
                D3D11_TEXTURE2D_DESC desc {};
                texture->GetDesc(&desc);
                desc.Width = cw;
                desc.Height = ch;
                desc.MipLevels = 1;
                desc.ArraySize = 1;
                desc.Usage = D3D11_USAGE_STAGING;
                desc.BindFlags = 0;
                desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                desc.MiscFlags = 0;
                ComPtr<ID3D11Texture2D> staging;
                if (SUCCEEDED(dev->d3d->CreateTexture2D(&desc, nullptr, &staging))) {
                    D3D11_BOX box { UINT(x0), UINT(y0), 0, UINT(x1), UINT(y1), 1 };
                    dev->context->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, texture.Get(), 0, &box);
                    D3D11_MAPPED_SUBRESOURCE mapped {};
                    if (SUCCEEDED(dev->context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
                        // Box-filter downscale to maxWidth (integer factor, keeps it sharp and cheap).
                        int factor = std::max(1, (cw + maxWidth - 1) / std::max(1, maxWidth));
                        services::Bitmap bmp;
                        bmp.width = cw / factor;
                        bmp.height = ch / factor;
                        bmp.bgra.resize(size_t(bmp.width) * bmp.height * 4);
                        auto* src = static_cast<const uint8_t*>(mapped.pData);
                        for (int y = 0; y < bmp.height; ++y) {
                            for (int x = 0; x < bmp.width; ++x) {
                                unsigned sum[4] {};
                                for (int dy = 0; dy < factor; ++dy) {
                                    const uint8_t* row = src + size_t(y * factor + dy) * mapped.RowPitch + size_t(x * factor) * 4;
                                    for (int dx = 0; dx < factor; ++dx)
                                        for (int c = 0; c < 4; ++c)
                                            sum[c] += row[dx * 4 + c];
                                }
                                uint8_t* out = &bmp.bgra[(size_t(y) * bmp.width + x) * 4];
                                unsigned n = unsigned(factor * factor);
                                out[0] = uint8_t(sum[0] / n);
                                out[1] = uint8_t(sum[1] / n);
                                out[2] = uint8_t(sum[2] / n);
                                out[3] = 255; // window content is opaque
                            }
                        }
                        dev->context->Unmap(staging.Get(), 0);
                        if (bmp)
                            result = std::move(bmp);
                    }
                }
            }
        }
        log(L"capture: wait=%lu surface=%d frame=%dx%d result=%d in %llu ms", wait, bool(surface), frameSize.Width,
            frameSize.Height, result.has_value(), GetTickCount64() - started);
        session.Close();
        pool.Close();
        return result;
    } catch (const winrt::hresult_error& e) {
        log(L"capture: hresult 0x%08X %s", unsigned(e.code()), e.message().c_str());
        return std::nullopt;
    } catch (...) {
        log(L"capture: unknown exception");
        return std::nullopt;
    }
}

} // namespace meridian::platform
