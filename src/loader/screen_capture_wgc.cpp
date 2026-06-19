/*
 * screen_capture_wgc.cpp - Windows Graphics Capture (raw COM, no WinRT headers)
 *
 * Background window capture supporting:
 *   - Windows behind other windows (occluded)
 *   - Minimized windows (Win11 22H2+)
 *   - DirectX rendered content
 *
 * Uses raw COM vtable calls for WinRT APIs while using standard C++
 * COM for D3D11/DXGI interfaces. No WinRT projection headers needed.
 *
 * Requires: Windows 10 1903+ (build 18362)
 * Links: -ld3d11 -ldxgi -lruntimeobject -lole32
 */

#include "screen_capture.h"

#include <initguid.h>
#include <d3d11.h>
#include <dxgi.h>
#include <roapi.h>
#include <winstring.h>
#include <inspectable.h>

#include <stdlib.h>
#include <string.h>

/* ─── Manual Interface/GUID Declarations ─────────────────────────── */

typedef HRESULT (WINAPI *PFN_CreateDirect3D11DeviceFromDXGIDevice)(
    IDXGIDevice *dxgiDevice, IInspectable **graphicsDevice);

static const IID IID_IDirect3DDxgiInterfaceAccess =
    {0xa9b3d012, 0x3df2, 0x4ee3, {0xb8,0xd1,0x86,0x95,0xf4,0x57,0xd3,0xc1}};

static const IID IID_IGraphicsCaptureItemInterop =
    {0x3628e81b, 0x3cac, 0x4c60, {0xb7,0xf4,0x23,0xce,0x0e,0x0c,0x33,0x56}};

static const IID IID_IGraphicsCaptureItem =
    {0x79c3f95b, 0x31f7, 0x4ec2, {0xa4,0x64,0x63,0x2e,0xf5,0xd3,0x07,0x60}};

static const IID IID_FramePoolStatics2 =
    {0x589b103f, 0x6bbc, 0x5df5, {0xa9,0x91,0x02,0xe2,0x8b,0x3b,0x66,0xd5}};

static const IID IID_FramePoolStatics =
    {0x7784056a, 0x67aa, 0x4d53, {0xae,0x54,0x10,0x88,0xd5,0xa8,0xca,0x21}};

typedef struct { INT32 Width; INT32 Height; } SizeInt32;

/*
 * Vtable slot layout reference:
 *
 * IInspectable-based: [0-2] IUnknown, [3-5] IInspectable, [6+] methods
 * IUnknown-based:     [0-2] IUnknown, [3+] methods
 *
 * IDirect3D11CaptureFramePool:
 *   [6] Recreate  [7] TryGetNextFrame  [8] add_FrameArrived
 *   [9] remove_FrameArrived  [10] CreateCaptureSession  [11] get_DispatcherQueue
 *
 * IDirect3D11CaptureFrame:
 *   [6] get_Surface  [7] get_SystemRelativeTime  [8] get_ContentSize
 *
 * IGraphicsCaptureSession:
 *   [6] StartCapture
 *
 * IGraphicsCaptureItemInterop (IUnknown-based):
 *   [3] CreateForWindow  [4] CreateForMonitor
 *
 * IDirect3D11CaptureFramePoolStatics2:
 *   [6] CreateFreeThreaded
 */

struct VTable { void *methods[64]; };
struct ComObj { VTable *vtbl; };
#define SLOT(obj, n) (((ComObj*)(obj))->vtbl->methods[n])

template<typename Ret, typename... Args>
static inline Ret vcall(void *obj, int slot, Args... args) {
    auto fn = reinterpret_cast<Ret(STDMETHODCALLTYPE*)(void*, Args...)>(
        ((ComObj*)obj)->vtbl->methods[slot]);
    return fn(obj, args...);
}

static inline void SafeRelease(void **p) {
    if (*p) {
        vcall<ULONG>(*p, 2);
        *p = nullptr;
    }
}

/* ─── Internal State ─────────────────────────────────────────────── */

static struct {
    ID3D11Device        *d3d_device;
    ID3D11DeviceContext *d3d_context;
    IInspectable        *winrt_device;

    void *capture_item;
    void *frame_pool;
    void *session;

    ID3D11Texture2D     *staging_tex;
    BYTE                *pixel_buffer;
    int                  width;
    int                  height;
    int                  stride;
    int                  pool_w;     /* frame-pool size; recreated on resize */
    int                  pool_h;
    BOOL                 frame_ready;

    BOOL                 active;
    BOOL                 initialized;
    CRITICAL_SECTION     cs;

    PFN_CreateDirect3D11DeviceFromDXGIDevice pfnCreate;
} s_wgc = {};

/* ─── Helpers ────────────────────────────────────────────────────── */

static BOOL CreateD3D(void)
{
    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL actual;

    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        levels, 1, D3D11_SDK_VERSION,
        &s_wgc.d3d_device, &actual, &s_wgc.d3d_context);

    if (FAILED(hr)) {
        hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            levels, 1, D3D11_SDK_VERSION,
            &s_wgc.d3d_device, &actual, &s_wgc.d3d_context);
    }
    if (FAILED(hr)) return FALSE;

    IDXGIDevice *dxgi = nullptr;
    hr = s_wgc.d3d_device->QueryInterface(IID_IDXGIDevice, (void**)&dxgi);
    if (FAILED(hr)) return FALSE;

    hr = s_wgc.pfnCreate(dxgi, &s_wgc.winrt_device);
    dxgi->Release();
    return SUCCEEDED(hr);
}

static BOOL EnsureStaging(int w, int h)
{
    if (s_wgc.staging_tex && s_wgc.width == w && s_wgc.height == h)
        return TRUE;

    if (s_wgc.staging_tex) { s_wgc.staging_tex->Release(); s_wgc.staging_tex = nullptr; }
    free(s_wgc.pixel_buffer); s_wgc.pixel_buffer = nullptr;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = (UINT)w;
    desc.Height = (UINT)h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    HRESULT hr = s_wgc.d3d_device->CreateTexture2D(&desc, nullptr, &s_wgc.staging_tex);
    if (FAILED(hr)) return FALSE;

    s_wgc.width = w;
    s_wgc.height = h;
    s_wgc.stride = w * 4;
    s_wgc.pixel_buffer = (BYTE*)malloc(w * h * 4);
    return s_wgc.pixel_buffer != nullptr;
}

static BOOL PullFrame(void)
{
    if (!s_wgc.frame_pool) return FALSE;

    typedef HRESULT (STDMETHODCALLTYPE *FN_TryGet)(void*, void**);
    FN_TryGet fnTry = (FN_TryGet)(SLOT(s_wgc.frame_pool, 7));
    void *frame_obj = nullptr;
    HRESULT hr = fnTry(s_wgc.frame_pool, &frame_obj);
    if (FAILED(hr) || !frame_obj) return FALSE;

    /* Handle game resolution / window-size changes: when the captured content
     * size differs from the frame-pool size, recreate the pool at the new size.
     * Without this, post-resize frames stay at the old buffer size (content
     * scaled/letterboxed) and every template/OCR region is wrong. */
    {
        typedef HRESULT (STDMETHODCALLTYPE *FN_GetCS)(void*, SizeInt32*);
        FN_GetCS fnCS = (FN_GetCS)(SLOT(frame_obj, 8));
        SizeInt32 cs = {0, 0};
        if (fnCS && SUCCEEDED(fnCS(frame_obj, &cs)) &&
            cs.Width > 0 && cs.Height > 0 &&
            (cs.Width != s_wgc.pool_w || cs.Height != s_wgc.pool_h)) {
            typedef HRESULT (STDMETHODCALLTYPE *FN_Recreate)(
                void*, void*, INT32, INT32, SizeInt32);
            FN_Recreate fnRe = (FN_Recreate)(SLOT(s_wgc.frame_pool, 6));
            if (fnRe && SUCCEEDED(fnRe(s_wgc.frame_pool, s_wgc.winrt_device,
                                       87/*B8G8R8A8*/, 1, cs))) {
                s_wgc.pool_w = cs.Width;
                s_wgc.pool_h = cs.Height;
            }
            /* This frame is from the old pool; skip it. Next pull is correct. */
            SafeRelease(&frame_obj);
            return FALSE;
        }
    }

    typedef HRESULT (STDMETHODCALLTYPE *FN_GetSurf)(void*, void**);
    FN_GetSurf fnSurf = (FN_GetSurf)(SLOT(frame_obj, 6));
    void *surface = nullptr;
    hr = fnSurf(frame_obj, &surface);
    if (FAILED(hr) || !surface) {
        SafeRelease(&frame_obj);
        return FALSE;
    }

    void *access = nullptr;
    hr = ((IUnknown*)surface)->QueryInterface(IID_IDirect3DDxgiInterfaceAccess, &access);

    ID3D11Texture2D *tex = nullptr;
    if (SUCCEEDED(hr) && access) {
        typedef HRESULT (STDMETHODCALLTYPE *FN_GetIface)(void*, REFIID, void**);
        FN_GetIface fnGet = (FN_GetIface)(SLOT(access, 3));
        hr = fnGet(access, IID_ID3D11Texture2D, (void**)&tex);
        SafeRelease(&access);
    }

    SafeRelease(&surface);
    SafeRelease(&frame_obj);
    if (!tex) return FALSE;

    D3D11_TEXTURE2D_DESC td;
    tex->GetDesc(&td);

    EnterCriticalSection(&s_wgc.cs);
    BOOL ok = FALSE;

    if (EnsureStaging((int)td.Width, (int)td.Height)) {
        s_wgc.d3d_context->CopyResource(s_wgc.staging_tex, tex);

        D3D11_MAPPED_SUBRESOURCE mapped;
        hr = s_wgc.d3d_context->Map(s_wgc.staging_tex, 0, D3D11_MAP_READ, 0, &mapped);
        if (SUCCEEDED(hr)) {
            for (int y = 0; y < s_wgc.height; y++) {
                memcpy(s_wgc.pixel_buffer + y * s_wgc.stride,
                       (BYTE*)mapped.pData + y * mapped.RowPitch,
                       s_wgc.stride);
            }
            s_wgc.d3d_context->Unmap(s_wgc.staging_tex, 0);
            s_wgc.frame_ready = TRUE;
            ok = TRUE;
        }
    }

    LeaveCriticalSection(&s_wgc.cs);
    tex->Release();
    return ok;
}

/* ─── Public API ─────────────────────────────────────────────────── */

extern "C" {

BOOL ScreenCapture_Init(void)
{
    if (s_wgc.initialized) return TRUE;

    HMODULE hd3d = LoadLibraryW(L"d3d11.dll");
    if (!hd3d) return FALSE;
    s_wgc.pfnCreate = (PFN_CreateDirect3D11DeviceFromDXGIDevice)
        GetProcAddress(hd3d, "CreateDirect3D11DeviceFromDXGIDevice");
    if (!s_wgc.pfnCreate) return FALSE;

    HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(hr) && hr != (HRESULT)RPC_E_CHANGED_MODE && hr != S_FALSE)
        return FALSE;

    if (!CreateD3D()) return FALSE;

    InitializeCriticalSection(&s_wgc.cs);
    s_wgc.initialized = TRUE;
    return TRUE;
}

void ScreenCapture_Shutdown(void)
{
    if (!s_wgc.initialized) return;
    ScreenCapture_StopCapture();

    if (s_wgc.winrt_device) { s_wgc.winrt_device->Release(); s_wgc.winrt_device = nullptr; }
    if (s_wgc.staging_tex)  { s_wgc.staging_tex->Release(); s_wgc.staging_tex = nullptr; }
    free(s_wgc.pixel_buffer); s_wgc.pixel_buffer = nullptr;
    if (s_wgc.d3d_context) { s_wgc.d3d_context->Release(); s_wgc.d3d_context = nullptr; }
    if (s_wgc.d3d_device)  { s_wgc.d3d_device->Release(); s_wgc.d3d_device = nullptr; }

    DeleteCriticalSection(&s_wgc.cs);
    s_wgc.initialized = FALSE;
}

BOOL ScreenCapture_StartCapture(HWND target_hwnd)
{
    if (!s_wgc.initialized || !target_hwnd) return FALSE;
    if (s_wgc.active) ScreenCapture_StopCapture();

    HRESULT hr;

    /* Activate factory and QI for IGraphicsCaptureItemInterop */
    HSTRING hcls = nullptr;
    const WCHAR *cls = L"Windows.Graphics.Capture.GraphicsCaptureItem";
    hr = WindowsCreateString(cls, (UINT32)wcslen(cls), &hcls);
    if (FAILED(hr)) return FALSE;

    void *factory = nullptr;
    hr = RoGetActivationFactory(hcls, IID_IInspectable, &factory);
    WindowsDeleteString(hcls);
    if (FAILED(hr) || !factory) return FALSE;

    void *interop = nullptr;
    hr = ((IUnknown*)factory)->QueryInterface(IID_IGraphicsCaptureItemInterop, &interop);
    ((IUnknown*)factory)->Release();
    if (FAILED(hr) || !interop) return FALSE;

    /* CreateForWindow (slot 3 on IUnknown-based interop interface) */
    typedef HRESULT (STDMETHODCALLTYPE *FN_CreateForWindow)(void*, HWND, const IID*, void**);
    FN_CreateForWindow fnCreate = (FN_CreateForWindow)(SLOT(interop, 3));
    hr = fnCreate(interop, target_hwnd, &IID_IGraphicsCaptureItem, &s_wgc.capture_item);
    ((IUnknown*)interop)->Release();
    if (FAILED(hr) || !s_wgc.capture_item) return FALSE;

    /* Determine capture size from window rect */
    SizeInt32 item_size = {0, 0};
    RECT rc;
    GetWindowRect(target_hwnd, &rc);
    item_size.Width = rc.right - rc.left;
    item_size.Height = rc.bottom - rc.top;
    if (item_size.Width <= 0) item_size.Width = 1920;
    if (item_size.Height <= 0) item_size.Height = 1080;

    /* Create frame pool via statics factory */
    HSTRING hpool = nullptr;
    const WCHAR *pool_cls = L"Windows.Graphics.Capture.Direct3D11CaptureFramePool";
    hr = WindowsCreateString(pool_cls, (UINT32)wcslen(pool_cls), &hpool);
    if (FAILED(hr)) { ScreenCapture_StopCapture(); return FALSE; }

    void *pool_factory = nullptr;
    hr = RoGetActivationFactory(hpool, IID_IInspectable, &pool_factory);
    WindowsDeleteString(hpool);
    if (FAILED(hr) || !pool_factory) { ScreenCapture_StopCapture(); return FALSE; }

    /* Try CreateFreeThreaded (statics2), fallback to Create (statics1) */
    void *statics = nullptr;
    hr = ((IUnknown*)pool_factory)->QueryInterface(IID_FramePoolStatics2, &statics);
    if (SUCCEEDED(hr) && statics) {
        typedef HRESULT (STDMETHODCALLTYPE *FN_CreateFT)(
            void*, void*, INT32, INT32, SizeInt32, void**);
        FN_CreateFT fn = (FN_CreateFT)(SLOT(statics, 6));
        hr = fn(statics, s_wgc.winrt_device, 87/*B8G8R8A8*/, 1, item_size, &s_wgc.frame_pool);
        ((IUnknown*)statics)->Release();
    } else {
        hr = ((IUnknown*)pool_factory)->QueryInterface(IID_FramePoolStatics, &statics);
        if (SUCCEEDED(hr) && statics) {
            typedef HRESULT (STDMETHODCALLTYPE *FN_Create)(
                void*, void*, INT32, INT32, SizeInt32, void**);
            FN_Create fn = (FN_Create)(SLOT(statics, 6));
            hr = fn(statics, s_wgc.winrt_device, 87, 1, item_size, &s_wgc.frame_pool);
            ((IUnknown*)statics)->Release();
        }
    }
    ((IUnknown*)pool_factory)->Release();
    if (FAILED(hr) || !s_wgc.frame_pool) { ScreenCapture_StopCapture(); return FALSE; }
    s_wgc.pool_w = item_size.Width;
    s_wgc.pool_h = item_size.Height;

    /* CreateCaptureSession (slot 10) */
    typedef HRESULT (STDMETHODCALLTYPE *FN_CreateSession)(void*, void*, void**);
    FN_CreateSession fnSess = (FN_CreateSession)(SLOT(s_wgc.frame_pool, 10));
    hr = fnSess(s_wgc.frame_pool, s_wgc.capture_item, &s_wgc.session);
    if (FAILED(hr) || !s_wgc.session) { ScreenCapture_StopCapture(); return FALSE; }

    /* StartCapture (slot 6 on IGraphicsCaptureSession) */
    typedef HRESULT (STDMETHODCALLTYPE *FN_Start)(void*);
    FN_Start fnStart = (FN_Start)(SLOT(s_wgc.session, 6));
    hr = fnStart(s_wgc.session);
    if (FAILED(hr)) { ScreenCapture_StopCapture(); return FALSE; }

    s_wgc.active = TRUE;
    return TRUE;
}

void ScreenCapture_StopCapture(void)
{
    s_wgc.active = FALSE;
    s_wgc.frame_ready = FALSE;
    SafeRelease(&s_wgc.session);
    SafeRelease(&s_wgc.frame_pool);
    SafeRelease(&s_wgc.capture_item);
}

BOOL ScreenCapture_GrabFrame(CaptureFrame *frame)
{
    if (!frame || !s_wgc.active) return FALSE;

    PullFrame();
    if (!s_wgc.frame_ready) return FALSE;

    EnterCriticalSection(&s_wgc.cs);
    frame->pixels = s_wgc.pixel_buffer;
    frame->width  = s_wgc.width;
    frame->height = s_wgc.height;
    frame->stride = s_wgc.stride;
    LeaveCriticalSection(&s_wgc.cs);
    return TRUE;
}

BOOL ScreenCapture_IsActive(void)
{
    return s_wgc.active;
}

} /* extern "C" */
