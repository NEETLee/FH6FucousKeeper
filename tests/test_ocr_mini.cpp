/*
 * test_ocr_mini.cpp - Test WGC capture + OCR recognition pipeline
 */

#include <windows.h>
#include <stdio.h>

extern "C" {
#include "screen_capture.h"
#include "ocr_engine.h"
}

int wmain(int argc, wchar_t *argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);
    wprintf(L"=== WGC + OCR Pipeline Test ===\n");

    /* Find target window */
    const WCHAR *title = L"Forza Horizon 6";
    if (argc > 1) title = argv[1];

    HWND hwnd = FindWindowW(NULL, title);
    if (!hwnd) {
        wprintf(L"Window not found: %ls\n", title);
        wprintf(L"Usage: test_ocr_mini.exe [\"Window Title\"]\n");
        return 1;
    }
    wprintf(L"Target window: %ls (%p)\n", title, (void*)hwnd);

    /* Init screen capture */
    wprintf(L"[1] ScreenCapture_Init... ");
    if (!ScreenCapture_Init()) { wprintf(L"FAIL\n"); return 1; }
    wprintf(L"OK\n");

    /* Init OCR engine */
    const WCHAR *lang = L"en-US";
    if (argc > 2) lang = argv[2];
    wprintf(L"[2] OcrEngine_Init(\"%ls\")... ", lang);
    if (!OcrEngine_Init(lang)) {
        wprintf(L"FAIL (trying user profile)... ");
        if (!OcrEngine_Init(NULL)) {
            wprintf(L"FAIL\n");
            ScreenCapture_Shutdown();
            return 1;
        }
    }
    wprintf(L"OK\n");

    /* Start capture */
    wprintf(L"[3] ScreenCapture_StartCapture... ");
    if (!ScreenCapture_StartCapture(hwnd)) {
        wprintf(L"FAIL\n");
        OcrEngine_Shutdown();
        ScreenCapture_Shutdown();
        return 1;
    }
    wprintf(L"OK\n");

    /* Wait for frame */
    wprintf(L"[4] Waiting 1000ms for frame...\n");
    Sleep(1000);

    /* Grab frame */
    CaptureFrame frame = {};
    wprintf(L"[5] ScreenCapture_GrabFrame... ");
    if (!ScreenCapture_GrabFrame(&frame)) {
        wprintf(L"FAIL (no frame available)\n");
        ScreenCapture_StopCapture();
        OcrEngine_Shutdown();
        ScreenCapture_Shutdown();
        return 1;
    }
    wprintf(L"OK (%dx%d, stride=%d)\n", frame.width, frame.height, frame.stride);

    /* Run full-image OCR */
    wprintf(L"[6] OcrEngine_Recognize (full image)...\n");
    static OcrResult result = {};
    DWORD t0 = GetTickCount();
    BOOL ok = OcrEngine_Recognize(frame.pixels, frame.width, frame.height,
                                   frame.stride, &result);
    DWORD elapsed = GetTickCount() - t0;

    if (!ok) {
        wprintf(L"    OCR returned no text (took %lums)\n", elapsed);
    } else {
        wprintf(L"    Done in %lums, %d lines:\n", elapsed, result.line_count);
        for (int i = 0; i < result.line_count && i < 10; i++) {
            wprintf(L"    [L%d] %ls\n", i, result.lines[i].full_text);
            for (int j = 0; j < result.lines[i].word_count && j < 5; j++) {
                OcrWord *w = &result.lines[i].words[j];
                wprintf(L"         [W%d] \"%ls\" (%ld,%ld)-(%ld,%ld)\n",
                        j, w->text, w->bounds.left, w->bounds.top,
                        w->bounds.right, w->bounds.bottom);
            }
            if (result.lines[i].word_count > 5)
                wprintf(L"         ... +%d more words\n", result.lines[i].word_count - 5);
        }
        if (result.line_count > 10)
            wprintf(L"    ... +%d more lines\n", result.line_count - 10);
    }

    /* Test ROI OCR on top-left quadrant */
    wprintf(L"\n[7] OcrEngine_RecognizeRegion (top-left quarter)...\n");
    RECT roi = {0, 0, frame.width / 2, frame.height / 2};
    static OcrResult roi_result = {};
    t0 = GetTickCount();
    ok = OcrEngine_RecognizeRegion(frame.pixels, frame.width, frame.height,
                                    frame.stride, roi, &roi_result);
    elapsed = GetTickCount() - t0;
    if (!ok) {
        wprintf(L"    No text in ROI (took %lums)\n", elapsed);
    } else {
        wprintf(L"    Done in %lums, %d lines:\n", elapsed, roi_result.line_count);
        for (int i = 0; i < roi_result.line_count && i < 5; i++) {
            wprintf(L"    [L%d] %ls\n", i, roi_result.lines[i].full_text);
        }
    }

    /* Cleanup */
    wprintf(L"\n[8] Cleanup...\n");
    ScreenCapture_StopCapture();
    OcrEngine_Shutdown();
    ScreenCapture_Shutdown();
    wprintf(L"Done.\n");
    return 0;
}
