/*
 * test_capture_gdi.c - Quick test for GDI-based screen capture
 *
 * Tests the PrintWindow-based capture without any WinRT dependency.
 * Useful for verifying the capture pipeline works before adding OCR.
 *
 * Build: make test-capture-gdi
 * Usage: test_capture_gdi.exe "Window Title"
 */

#include <windows.h>
#include <stdio.h>
#include <wchar.h>

#include "screen_capture.h"

/* ─── Save frame as BMP ──────────────────────────────────────────── */

static BOOL SaveBMP(const CaptureFrame *frame, const WCHAR *filename)
{
    FILE *f = _wfopen(filename, L"wb");
    if (!f) return FALSE;

    int row_size = ((frame->width * 3 + 3) / 4) * 4;
    int data_size = row_size * frame->height;

    BITMAPFILEHEADER bfh = {0};
    bfh.bfType = 0x4D42;
    bfh.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + data_size;
    bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

    BITMAPINFOHEADER bih = {0};
    bih.biSize = sizeof(BITMAPINFOHEADER);
    bih.biWidth = frame->width;
    bih.biHeight = -frame->height;
    bih.biPlanes = 1;
    bih.biBitCount = 24;
    bih.biCompression = BI_RGB;
    bih.biSizeImage = data_size;

    fwrite(&bfh, sizeof(bfh), 1, f);
    fwrite(&bih, sizeof(bih), 1, f);

    BYTE *row = (BYTE*)malloc(row_size);
    for (int y = 0; y < frame->height; y++) {
        BYTE *src = frame->pixels + y * frame->stride;
        for (int x = 0; x < frame->width; x++) {
            row[x * 3 + 0] = src[x * 4 + 0];
            row[x * 3 + 1] = src[x * 4 + 1];
            row[x * 3 + 2] = src[x * 4 + 2];
        }
        memset(row + frame->width * 3, 0, row_size - frame->width * 3);
        fwrite(row, row_size, 1, f);
    }

    free(row);
    fclose(f);
    return TRUE;
}

/* ─── Pixel analysis demo ────────────────────────────────────────── */

static void AnalyzeFrame(const CaptureFrame *frame)
{
    /* Sample some pixels to demonstrate analysis capability */
    wprintf(L"\n--- Pixel Sampling ---\n");

    typedef struct { float x; float y; const WCHAR *label; } SamplePoint;
    SamplePoint points[] = {
        {0.50f, 0.50f, L"Center"},
        {0.10f, 0.10f, L"Top-left"},
        {0.90f, 0.10f, L"Top-right"},
        {0.10f, 0.90f, L"Bottom-left"},
        {0.90f, 0.90f, L"Bottom-right"},
    };

    for (int i = 0; i < 5; i++) {
        int px = (int)(points[i].x * frame->width);
        int py = (int)(points[i].y * frame->height);
        int offset = py * frame->stride + px * 4;

        BYTE b = frame->pixels[offset + 0];
        BYTE g = frame->pixels[offset + 1];
        BYTE r = frame->pixels[offset + 2];

        wprintf(L"  %-12ls (%4d,%4d): R=%3d G=%3d B=%3d\n",
                points[i].label, px, py, r, g, b);
    }

    /* Check if frame is mostly black (loading screen indicator) */
    int dark_count = 0;
    int sample_count = 100;
    for (int i = 0; i < sample_count; i++) {
        int sx = (frame->width / 10) + (i % 10) * (frame->width * 8 / 100);
        int sy = (frame->height / 10) + (i / 10) * (frame->height * 8 / 100);
        int off = sy * frame->stride + sx * 4;

        BYTE b = frame->pixels[off + 0];
        BYTE g = frame->pixels[off + 1];
        BYTE r = frame->pixels[off + 2];

        if (r < 30 && g < 30 && b < 30) dark_count++;
    }

    float dark_ratio = (float)dark_count / sample_count;
    wprintf(L"\n  Dark pixel ratio: %.1f%% ", dark_ratio * 100);
    if (dark_ratio > 0.8f)
        wprintf(L"(likely loading screen or minimized)\n");
    else if (dark_ratio > 0.5f)
        wprintf(L"(partially dark - could be skill tree background)\n");
    else
        wprintf(L"(normal content visible)\n");
}

/* ─── Main ───────────────────────────────────────────────────────── */

int wmain(int argc, WCHAR *argv[])
{
    wprintf(L"=== FH6 FocusKeeper - GDI Capture Test ===\n\n");

    /* Find window */
    HWND hwnd = NULL;
    if (argc > 1) {
        hwnd = FindWindowW(NULL, argv[1]);
        if (!hwnd) {
            wprintf(L"[ERROR] Window \"%ls\" not found\n", argv[1]);
            return 1;
        }
    } else {
        hwnd = FindWindowW(L"ForzaHorizon6", L"Forza Horizon 6");
        if (!hwnd) hwnd = FindWindowW(NULL, L"Forza Horizon 6");
        if (!hwnd) {
            wprintf(L"[INFO] FH6 not found, trying Notepad as test target...\n");
            hwnd = FindWindowW(L"Notepad", NULL);
        }
        if (!hwnd) {
            wprintf(L"[ERROR] No suitable window found.\n");
            wprintf(L"  Usage: %ls \"Window Title\"\n", argv[0]);
            return 1;
        }
    }

    WCHAR title[256];
    GetWindowTextW(hwnd, title, 256);
    wprintf(L"[OK] Target: \"%ls\" (HWND: 0x%p)\n", title, (void*)hwnd);

    /* Init capture */
    if (!ScreenCapture_Init()) {
        wprintf(L"[ERROR] ScreenCapture_Init failed\n");
        return 1;
    }

    if (!ScreenCapture_StartCapture(hwnd)) {
        wprintf(L"[ERROR] ScreenCapture_StartCapture failed\n");
        ScreenCapture_Shutdown();
        return 1;
    }
    wprintf(L"[OK] Capture started\n");

    /* Grab frame */
    CaptureFrame frame;
    if (!ScreenCapture_GrabFrame(&frame)) {
        wprintf(L"[ERROR] Failed to grab frame\n");
        ScreenCapture_StopCapture();
        ScreenCapture_Shutdown();
        return 1;
    }

    wprintf(L"[OK] Frame: %d x %d pixels (stride: %d)\n",
            frame.width, frame.height, frame.stride);

    /* Save BMP */
    const WCHAR *bmp_file = L"test_gdi_capture.bmp";
    if (SaveBMP(&frame, bmp_file)) {
        wprintf(L"[OK] Saved: %ls\n", bmp_file);
    } else {
        wprintf(L"[WARN] Failed to save BMP\n");
    }

    /* Analyze */
    AnalyzeFrame(&frame);

    /* Cleanup */
    ScreenCapture_StopCapture();
    ScreenCapture_Shutdown();
    wprintf(L"\n[OK] Done.\n");
    return 0;
}
