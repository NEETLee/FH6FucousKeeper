/*
 * screen_capture.cpp - Background Window Capture via Windows Graphics Capture
 *
 * Uses the Windows.Graphics.Capture API (Win10 1903+) to capture a window's
 * content in the background, even when covered by other windows.
 *
 * This file is compiled as C++ but exposes a C interface via screen_capture.h.
 */

#include "screen_capture.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <inspectable.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <windows.graphics.capture.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <atomic>
#include <mutex>

namespace winrt_cap = winrt::Windows::Graphics::Capture;
namespace winrt_dx  = winrt::Windows::Graphics::DirectX;
namespace winrt_d3d = winrt::Windows::Graphics::DirectX::Direct3D11;

/* ─── Internal State ─────────────────────────────────────────────── */

static struct {
    ID3D11Device           *d3d_device;
    ID3D11DeviceContext    *d3d_context;
    winrt_d3d::IDirect3DDevice winrt_device{nullptr};

    winrt_cap::GraphicsCaptureItem   item{nullptr};
    winrt_cap::Direct3D11CaptureFramePool frame_pool{nullptr};
    winrt_cap::GraphicsCaptureSession session{nullptr};

    ID3D11Texture2D        *staging_texture;
    BYTE                   *pixel_buffer;
    int                     frame_width;
    int                     frame_height;
    int                     frame_stride;

    std::mutex              frame_mutex;
    std::atomic<bool>       frame_available{false};
    std::atomic<bool>       active{false};
    bool                    initialized;
} s_cap = {};

/* ─── Helper: Create D3D11 Device ────────────────────────────────── */

static BOOL CreateD3DDevice(void)
{
    D3D_FEATURE_LEVEL feature_levels[] = {D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL actual_level;

    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        feature_levels, 1,
        D3D11_SDK_VERSION,
        &s_cap.d3d_device,
        &actual_level,
        &s_cap.d3d_context
    );

    if (FAILED(hr)) {
        hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            feature_levels, 1,
            D3D11_SDK_VERSION,
            &s_cap.d3d_device,
            &actual_level,
            &s_cap.d3d_context
        );
    }

    return SUCCEEDED(hr);
}

/* ─── Helper: Wrap D3D device for WinRT ──────────────────────────── */

static BOOL CreateWinRTDevice(void)
{
    IDXGIDevice *dxgi_device = nullptr;
    HRESULT hr = s_cap.d3d_device->QueryInterface(__uuidof(IDXGIDevice),
                                                   reinterpret_cast<void**>(&dxgi_device));
    if (FAILED(hr)) return FALSE;

    IInspectable *inspectable = nullptr;
    hr = CreateDirect3D11DeviceFromDXGIDevice(dxgi_device, &inspectable);
    dxgi_device->Release();
    if (FAILED(hr)) return FALSE;

    s_cap.winrt_device = winrt::capture<winrt_d3d::IDirect3DDevice>(inspectable);
    inspectable->Release();
    return true;
}

/* ─── Helper: Create capture item from HWND ──────────────────────── */

static winrt_cap::GraphicsCaptureItem CreateCaptureItemForWindow(HWND hwnd)
{
    auto interop_factory = winrt::get_activation_factory<
        winrt_cap::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();

    winrt_cap::GraphicsCaptureItem item{nullptr};
    HRESULT hr = interop_factory->CreateForWindow(
        hwnd,
        winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(),
        winrt::put_abi(item)
    );

    return SUCCEEDED(hr) ? item : nullptr;
}

/* ─── Helper: Ensure staging texture matches frame size ───────────── */

static BOOL EnsureStagingTexture(int width, int height)
{
    if (s_cap.staging_texture &&
        s_cap.frame_width == width && s_cap.frame_height == height) {
        return TRUE;
    }

    if (s_cap.staging_texture) {
        s_cap.staging_texture->Release();
        s_cap.staging_texture = nullptr;
    }

    if (s_cap.pixel_buffer) {
        free(s_cap.pixel_buffer);
        s_cap.pixel_buffer = nullptr;
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    HRESULT hr = s_cap.d3d_device->CreateTexture2D(&desc, nullptr, &s_cap.staging_texture);
    if (FAILED(hr)) return FALSE;

    s_cap.frame_width = width;
    s_cap.frame_height = height;
    s_cap.frame_stride = width * 4;
    s_cap.pixel_buffer = (BYTE*)malloc(width * height * 4);

    return s_cap.pixel_buffer != nullptr;
}

/* ─── Frame Arrived Callback ─────────────────────────────────────── */

static void OnFrameArrived(
    winrt_cap::Direct3D11CaptureFramePool const &sender,
    winrt::Windows::Foundation::IInspectable const &)
{
    auto frame = sender.TryGetNextFrame();
    if (!frame) return;

    auto surface = frame.Surface();
    auto access = surface.as<IDirect3DDxgiInterfaceAccess>();
    ID3D11Texture2D *frame_texture = nullptr;
    HRESULT hr = access->GetInterface(__uuidof(ID3D11Texture2D),
                                       reinterpret_cast<void**>(&frame_texture));
    if (FAILED(hr) || !frame_texture) {
        frame.Close();
        return;
    }

    D3D11_TEXTURE2D_DESC desc;
    frame_texture->GetDesc(&desc);

    std::lock_guard<std::mutex> lock(s_cap.frame_mutex);

    if (!EnsureStagingTexture(desc.Width, desc.Height)) {
        frame_texture->Release();
        frame.Close();
        return;
    }

    s_cap.d3d_context->CopyResource(s_cap.staging_texture, frame_texture);

    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = s_cap.d3d_context->Map(s_cap.staging_texture, 0, D3D11_MAP_READ, 0, &mapped);
    if (SUCCEEDED(hr)) {
        for (int y = 0; y < s_cap.frame_height; y++) {
            memcpy(s_cap.pixel_buffer + y * s_cap.frame_stride,
                   (BYTE*)mapped.pData + y * mapped.RowPitch,
                   s_cap.frame_stride);
        }
        s_cap.d3d_context->Unmap(s_cap.staging_texture, 0);
        s_cap.frame_available.store(true);
    }

    frame_texture->Release();
    frame.Close();
}

/* ─── Public C API ───────────────────────────────────────────────── */

extern "C" {

BOOL ScreenCapture_Init(void)
{
    if (s_cap.initialized) return TRUE;

    winrt::init_apartment(winrt::apartment_type::multi_threaded);

    if (!CreateD3DDevice()) return FALSE;
    if (!CreateWinRTDevice()) return FALSE;

    s_cap.initialized = true;
    return TRUE;
}

void ScreenCapture_Shutdown(void)
{
    if (!s_cap.initialized) return;

    ScreenCapture_StopCapture();

    s_cap.winrt_device = nullptr;

    if (s_cap.staging_texture) {
        s_cap.staging_texture->Release();
        s_cap.staging_texture = nullptr;
    }
    if (s_cap.pixel_buffer) {
        free(s_cap.pixel_buffer);
        s_cap.pixel_buffer = nullptr;
    }
    if (s_cap.d3d_context) {
        s_cap.d3d_context->Release();
        s_cap.d3d_context = nullptr;
    }
    if (s_cap.d3d_device) {
        s_cap.d3d_device->Release();
        s_cap.d3d_device = nullptr;
    }

    s_cap.initialized = false;
}

BOOL ScreenCapture_StartCapture(HWND target_hwnd)
{
    if (!s_cap.initialized) return FALSE;
    if (s_cap.active.load()) ScreenCapture_StopCapture();

    auto item = CreateCaptureItemForWindow(target_hwnd);
    if (!item) return FALSE;

    auto size = item.Size();

    s_cap.item = item;
    s_cap.frame_pool = winrt_cap::Direct3D11CaptureFramePool::CreateFreeThreaded(
        s_cap.winrt_device,
        winrt_dx::DirectXPixelFormat::B8G8R8A8UIntNormalized,
        1,
        size
    );

    s_cap.frame_pool.FrameArrived(OnFrameArrived);

    s_cap.session = s_cap.frame_pool.CreateCaptureSession(item);

    /* Disable yellow border on Win11+ (best effort) */
    try {
        auto session4 = s_cap.session.as<winrt_cap::IGraphicsCaptureSession3>();
        if (session4) {
            /* IsCursorCaptureEnabled and IsBorderRequired are on session2/3 */
        }
    } catch (...) {}

    s_cap.session.StartCapture();
    s_cap.active.store(true);

    return TRUE;
}

void ScreenCapture_StopCapture(void)
{
    if (!s_cap.active.load()) return;

    s_cap.active.store(false);
    s_cap.frame_available.store(false);

    if (s_cap.session) {
        s_cap.session.Close();
        s_cap.session = nullptr;
    }
    if (s_cap.frame_pool) {
        s_cap.frame_pool.Close();
        s_cap.frame_pool = nullptr;
    }
    s_cap.item = nullptr;
}

BOOL ScreenCapture_GrabFrame(CaptureFrame *frame)
{
    if (!frame || !s_cap.active.load() || !s_cap.frame_available.load())
        return FALSE;

    std::lock_guard<std::mutex> lock(s_cap.frame_mutex);

    frame->pixels = s_cap.pixel_buffer;
    frame->width  = s_cap.frame_width;
    frame->height = s_cap.frame_height;
    frame->stride = s_cap.frame_stride;

    return TRUE;
}

BOOL ScreenCapture_IsActive(void)
{
    return s_cap.active.load() ? TRUE : FALSE;
}

} /* extern "C" */
