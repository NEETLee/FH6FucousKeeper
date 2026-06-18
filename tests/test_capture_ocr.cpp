/*
 * test_capture_ocr.cpp - Standalone test for screen capture + OCR
 *
 * This test program:
 *   1. Finds the FH6 game window (or any specified window)
 *   2. Captures a frame using Windows Graphics Capture
 *   3. Runs OCR on the captured frame
 *   4. Saves the captured frame as BMP for visual inspection
 *   5. Prints OCR results to console
 *
 * Build:
 *   g++ -std=c++20 -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN
 *       -D_WIN32_WINNT=0x0A00 -I../src/loader
 *       -o test_capture_ocr.exe test_capture_ocr.cpp
 *       ../src/loader/screen_capture.cpp ../src/loader/ocr_engine.cpp
 *       -ld3d11 -ldxgi -lwindowsapp -lruntimeobject -lole32 -loleaut32 -lgdi32
 *
 * Usage:
 *   test_capture_ocr.exe                  (auto-find FH6 window)
 *   test_capture_ocr.exe "Window Title"   (capture specific window)
 */

#include <windows.h>
#include <stdio.h>
#include <wchar.h>

#include "screen_capture.h"
#include "ocr_engine.h"

/* ─── Save frame as BMP for inspection ───────────────────────────── */

static BOOL SaveFrameAsBMP(const CaptureFrame *frame, const WCHAR *filename)
{
    FILE *f = _wfopen(filename, L"wb");
    if (!f) return FALSE;

    int row_size = ((frame->width * 3 + 3) / 4) * 4;
    int data_size = row_size * frame->height;

    /* BMP File Header */
    BITMAPFILEHEADER bfh = {0};
    bfh.bfType = 0x4D42;
    bfh.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + data_size;
    bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

    /* BMP Info Header */
    BITMAPINFOHEADER bih = {0};
    bih.biSize = sizeof(BITMAPINFOHEADER);
    bih.biWidth = frame->width;
    bih.biHeight = -frame->height;  /* top-down */
    bih.biPlanes = 1;
    bih.biBitCount = 24;
    bih.biCompression = BI_RGB;
    bih.biSizeImage = data_size;

    fwrite(&bfh, sizeof(bfh), 1, f);
    fwrite(&bih, sizeof(bih), 1, f);

    /* Convert BGRA to BGR row by row */
    BYTE *row_buf = (BYTE*)malloc(row_size);
    for (int y = 0; y < frame->height; y++) {
        BYTE *src = frame->pixels + y * frame->stride;
        for (int x = 0; x < frame->width; x++) {
            row_buf[x * 3 + 0] = src[x * 4 + 0]; /* B */
            row_buf[x * 3 + 1] = src[x * 4 + 1]; /* G */
            row_buf[x * 3 + 2] = src[x * 4 + 2]; /* R */
        }
        /* Pad remaining bytes */
        memset(row_buf + frame->width * 3, 0, row_size - frame->width * 3);
        fwrite(row_buf, row_size, 1, f);
    }

    free(row_buf);
    fclose(f);
    return TRUE;
}

/* ─── Find window by title ───────────────────────────────────────── */

static HWND FindGameWindow(const WCHAR *title)
{
    if (title && title[0]) {
        return FindWindowW(NULL, title);
    }

    /* Try FH6 window names */
    HWND hwnd = FindWindowW(L"ForzaHorizon6", L"Forza Horizon 6");
    if (hwnd) return hwnd;

    hwnd = FindWindowW(NULL, L"Forza Horizon 6");
    return hwnd;
}

/* ─── Main ───────────────────────────────────────────────────────── */

int wmain(int argc, WCHAR *argv[])
{
    wprintf(L"=== FH6 FocusKeeper - Screen Capture + OCR Test ===\n\n");

    /* Find target window */
    const WCHAR *target_title = (argc > 1) ? argv[1] : NULL;
    HWND hwnd = FindGameWindow(target_title);

    if (!hwnd) {
        wprintf(L"[ERROR] Game window not found.\n");
        wprintf(L"  Usage: %ls \"Window Title\"\n", argv[0]);
        wprintf(L"  Or launch Forza Horizon 6 first.\n");
        return 1;
    }

    WCHAR window_title[256];
    GetWindowTextW(hwnd, window_title, 256);
    wprintf(L"[OK] Found window: \"%ls\" (HWND: 0x%p)\n", window_title, (void*)hwnd);

    /* Initialize capture */
    wprintf(L"\n--- Initializing Screen Capture ---\n");
    if (!ScreenCapture_Init()) {
        wprintf(L"[ERROR] ScreenCapture_Init failed\n");
        return 1;
    }
    wprintf(L"[OK] Screen capture initialized\n");

    /* Start capture */
    if (!ScreenCapture_StartCapture(hwnd)) {
        wprintf(L"[ERROR] ScreenCapture_StartCapture failed\n");
        ScreenCapture_Shutdown();
        return 1;
    }
    wprintf(L"[OK] Capture session started\n");

    /* Wait a moment for first frame */
    wprintf(L"[..] Waiting for first frame...\n");
    Sleep(500);

    /* Grab frame */
    CaptureFrame frame;
    BOOL got_frame = FALSE;
    for (int attempt = 0; attempt < 10; attempt++) {
        if (ScreenCapture_GrabFrame(&frame)) {
            got_frame = TRUE;
            break;
        }
        Sleep(100);
    }

    if (!got_frame) {
        wprintf(L"[ERROR] Failed to grab frame after 10 attempts\n");
        ScreenCapture_StopCapture();
        ScreenCapture_Shutdown();
        return 1;
    }

    wprintf(L"[OK] Frame captured: %d x %d pixels\n", frame.width, frame.height);

    /* Save as BMP */
    const WCHAR *bmp_path = L"test_capture.bmp";
    if (SaveFrameAsBMP(&frame, bmp_path)) {
        wprintf(L"[OK] Saved screenshot to: %ls\n", bmp_path);
    } else {
        wprintf(L"[WARN] Failed to save BMP\n");
    }

    /* Initialize OCR */
    wprintf(L"\n--- Initializing OCR Engine ---\n");
    if (!OcrEngine_Init(L"en-US")) {
        wprintf(L"[WARN] OCR init with en-US failed, trying default...\n");
        if (!OcrEngine_Init(NULL)) {
            wprintf(L"[ERROR] OCR engine unavailable\n");
            ScreenCapture_StopCapture();
            ScreenCapture_Shutdown();
            return 1;
        }
    }
    wprintf(L"[OK] OCR engine initialized\n");

    /* Full image OCR */
    wprintf(L"\n--- Running Full Image OCR ---\n");
    OcrResult result;
    if (OcrEngine_Recognize(frame.pixels, frame.width, frame.height,
                            frame.stride, &result)) {
        wprintf(L"[OK] OCR complete: %d lines found\n", result.line_count);
        wprintf(L"\n--- Recognized Text ---\n");
        for (int i = 0; i < result.line_count; i++) {
            wprintf(L"  Line %d: \"%ls\"\n", i + 1, result.lines[i].full_text);
        }
        wprintf(L"\n--- Full Text ---\n%ls\n", result.all_text);
    } else {
        wprintf(L"[ERROR] OCR recognition failed\n");
    }

    /* ROI OCR test: top-right area (where skill points typically are) */
    wprintf(L"\n--- ROI OCR Test (top-right: skill points area) ---\n");
    RECT roi;
    roi.left   = (LONG)(frame.width * 0.85f);
    roi.top    = (LONG)(frame.height * 0.02f);
    roi.right  = (LONG)(frame.width * 0.99f);
    roi.bottom = (LONG)(frame.height * 0.07f);
    wprintf(L"  ROI: (%ld, %ld) - (%ld, %ld)\n",
            roi.left, roi.top, roi.right, roi.bottom);

    OcrResult roi_result;
    if (OcrEngine_RecognizeRegion(frame.pixels, frame.width, frame.height,
                                  frame.stride, roi, &roi_result)) {
        wprintf(L"  Recognized: \"%ls\"\n", roi_result.all_text);
    } else {
        wprintf(L"  (no text in region)\n");
    }

    /* Cleanup */
    wprintf(L"\n--- Cleanup ---\n");
    OcrEngine_Shutdown();
    ScreenCapture_StopCapture();
    ScreenCapture_Shutdown();
    wprintf(L"[OK] All done.\n");

    return 0;
}
