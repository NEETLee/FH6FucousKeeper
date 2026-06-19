/*
 * screen_capture_gdi.c - Fallback GDI-based Window Capture
 *
 * Uses PrintWindow (PW_RENDERFULLCONTENT) for window capture.
 * This works with any compiler (no WinRT dependency) but may return
 * black frames for DirectX games. Useful for testing the pipeline
 * and works well for non-DX windows.
 *
 * The WGC (Windows Graphics Capture) implementation in screen_capture.cpp
 * is preferred for DX games but requires C++/WinRT headers.
 *
 * Build gate: Define USE_WGC_CAPTURE=1 to use WGC, otherwise this
 * fallback is used.
 */

#include "screen_capture.h"
#include <wingdi.h>

/* ─── Internal State ─────────────────────────────────────────────── */

static struct {
    HWND    target_hwnd;
    HDC     hdc_mem;
    HBITMAP hbm_capture;
    HBITMAP hbm_old;
    BYTE   *pixel_buffer;
    int     width;
    int     height;
    int     stride;
    BOOL    active;
    BOOL    initialized;
} s_gdi = {0};

/* ─── Public API ─────────────────────────────────────────────────── */

BOOL ScreenCapture_Init(void)
{
    if (s_gdi.initialized) return TRUE;
    s_gdi.initialized = TRUE;
    return TRUE;
}

void ScreenCapture_Shutdown(void)
{
    ScreenCapture_StopCapture();
    s_gdi.initialized = FALSE;
}

BOOL ScreenCapture_StartCapture(HWND target_hwnd)
{
    if (!s_gdi.initialized || !target_hwnd) return FALSE;
    if (!IsWindow(target_hwnd)) return FALSE;

    if (s_gdi.active) ScreenCapture_StopCapture();

    s_gdi.target_hwnd = target_hwnd;
    s_gdi.active = TRUE;
    return TRUE;
}

void ScreenCapture_StopCapture(void)
{
    if (s_gdi.hbm_old && s_gdi.hdc_mem) {
        SelectObject(s_gdi.hdc_mem, s_gdi.hbm_old);
        s_gdi.hbm_old = NULL;
    }
    if (s_gdi.hbm_capture) {
        DeleteObject(s_gdi.hbm_capture);
        s_gdi.hbm_capture = NULL;
    }
    if (s_gdi.hdc_mem) {
        DeleteDC(s_gdi.hdc_mem);
        s_gdi.hdc_mem = NULL;
    }
    if (s_gdi.pixel_buffer) {
        free(s_gdi.pixel_buffer);
        s_gdi.pixel_buffer = NULL;
    }

    s_gdi.target_hwnd = NULL;
    s_gdi.active = FALSE;
    s_gdi.width = 0;
    s_gdi.height = 0;
}

BOOL ScreenCapture_GrabFrame(CaptureFrame *frame)
{
    if (!frame || !s_gdi.active || !s_gdi.target_hwnd) return FALSE;
    if (!IsWindow(s_gdi.target_hwnd)) return FALSE;

    /* Cannot capture minimized windows with GDI */
    if (IsIconic(s_gdi.target_hwnd)) return FALSE;

    RECT rc;
    if (!GetClientRect(s_gdi.target_hwnd, &rc)) return FALSE;

    int width  = rc.right - rc.left;
    int height = rc.bottom - rc.top;
    if (width <= 0 || height <= 0) return FALSE;

    /* Recreate resources if window size changed */
    if (width != s_gdi.width || height != s_gdi.height) {
        if (s_gdi.hbm_old && s_gdi.hdc_mem) {
            SelectObject(s_gdi.hdc_mem, s_gdi.hbm_old);
            s_gdi.hbm_old = NULL;
        }
        if (s_gdi.hbm_capture) {
            DeleteObject(s_gdi.hbm_capture);
            s_gdi.hbm_capture = NULL;
        }
        if (s_gdi.hdc_mem) {
            DeleteDC(s_gdi.hdc_mem);
            s_gdi.hdc_mem = NULL;
        }
        if (s_gdi.pixel_buffer) {
            free(s_gdi.pixel_buffer);
            s_gdi.pixel_buffer = NULL;
        }

        HDC hdc_window = GetDC(s_gdi.target_hwnd);
        if (!hdc_window) return FALSE;

        s_gdi.hdc_mem = CreateCompatibleDC(hdc_window);

        BITMAPINFO bmi = {0};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = width;
        bmi.bmiHeader.biHeight = -height; /* top-down */
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        s_gdi.hbm_capture = CreateDIBSection(hdc_window, &bmi, DIB_RGB_COLORS,
                                             (void**)&s_gdi.pixel_buffer, NULL, 0);
        ReleaseDC(s_gdi.target_hwnd, hdc_window);

        if (!s_gdi.hbm_capture || !s_gdi.pixel_buffer) return FALSE;

        s_gdi.hbm_old = (HBITMAP)SelectObject(s_gdi.hdc_mem, s_gdi.hbm_capture);
        s_gdi.width = width;
        s_gdi.height = height;
        s_gdi.stride = width * 4;
    }

    /*
     * PW_RENDERFULLCONTENT (0x00000002) - Available on Win 8.1+
     * Renders the full window content including DWM composition.
     * For DX games this may still return black, but it works for most
     * windowed applications.
     */
    #ifndef PW_RENDERFULLCONTENT
    #define PW_RENDERFULLCONTENT 0x00000002
    #endif

    BOOL captured = PrintWindow(s_gdi.target_hwnd, s_gdi.hdc_mem,
                                PW_RENDERFULLCONTENT);

    if (!captured) {
        /* Fallback to BitBlt (only works if window is visible) */
        HDC hdc_src = GetDC(s_gdi.target_hwnd);
        if (hdc_src) {
            captured = BitBlt(s_gdi.hdc_mem, 0, 0, s_gdi.width, s_gdi.height,
                             hdc_src, 0, 0, SRCCOPY);
            ReleaseDC(s_gdi.target_hwnd, hdc_src);
        }
    }

    if (!captured) return FALSE;

    frame->pixels = s_gdi.pixel_buffer;
    frame->width  = s_gdi.width;
    frame->height = s_gdi.height;
    frame->stride = s_gdi.stride;

    return TRUE;
}

BOOL ScreenCapture_IsActive(void)
{
    return s_gdi.active;
}
