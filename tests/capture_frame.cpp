/*
 * capture_frame.cpp - Curate clean full-frame reference captures for the
 * resolution harness. Finds the live FH6 window, grabs a WGC frame, and saves
 * an un-annotated BGR PNG. No OCR, no key injection: it must never mutate game
 * state so the operator can park FH6 on any screen and snapshot it verbatim.
 *
 * Build: make capture-frame
 * Run:   build/capture_frame.exe <out.png>
 */

#include <windows.h>
#include <stdio.h>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

extern "C" {
#include "screen_capture.h"
}

static BOOL CALLBACK HasForzaChildProc(HWND child, LPARAM lp) {
    WCHAR cls[64] = {};
    GetClassNameW(child, cls, 64);
    if (_wcsicmp(cls, L"ForzaRenderingWindow") == 0) { *(BOOL*)lp = TRUE; return FALSE; }
    return TRUE;
}
static BOOL CALLBACK FindGameProc(HWND h, LPARAM lp) {
    if (!IsWindowVisible(h)) return TRUE;
    BOOL found = FALSE;
    EnumChildWindows(h, HasForzaChildProc, (LPARAM)&found);
    if (found) { *(HWND*)lp = h; return FALSE; }
    return TRUE;
}
static HWND FindGameWindow(void) {
    HWND h = NULL;
    EnumWindows(FindGameProc, (LPARAM)&h);
    if (!h) h = FindWindowW(NULL, L"Forza Horizon 6");
    return h;
}

int wmain(int argc, wchar_t *argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const wchar_t *out_w = (argc > 1) ? argv[1] : L"build/capture.png";
    char out[512];
    WideCharToMultiByte(CP_UTF8, 0, out_w, -1, out, sizeof(out), NULL, NULL);

    HWND hwnd = FindGameWindow();
    if (!hwnd) { wprintf(L"game window not found\n"); return 1; }

    if (!ScreenCapture_Init() || !ScreenCapture_StartCapture(hwnd)) {
        wprintf(L"capture init failed\n"); return 1;
    }

    CaptureFrame frame = {};
    BOOL got = FALSE;
    for (int i = 0; i < 10; i++) {
        if (ScreenCapture_GrabFrame(&frame)) got = TRUE;
        Sleep(45);
    }
    if (!got || !frame.pixels) { wprintf(L"no frame\n"); return 1; }

    cv::Mat bgra(frame.height, frame.width, CV_8UC4, frame.pixels, frame.stride);
    cv::Mat bgr; cv::cvtColor(bgra, bgr, cv::COLOR_BGRA2BGR);
    if (!cv::imwrite(out, bgr)) { wprintf(L"imwrite failed: %hs\n", out); return 1; }
    wprintf(L"saved %hs (%dx%d)\n", out, frame.width, frame.height);

    ScreenCapture_StopCapture();
    ScreenCapture_Shutdown();
    return 0;
}
