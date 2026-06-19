/*
 * test_economy.cpp - Calibration probe for OCR economy reading.
 *
 * Captures the current FH6 frame, runs full-frame OCR, and dumps every word
 * with both pixel and normalized (0..1) coordinates so we can locate where the
 * credits (CR) balance and skill points (SP) numbers appear, then define ROIs
 * for farm_economy. Also saves the frame to build/econ_frame.png for visual
 * reference.
 *
 * Build: make test-economy
 * Run:   build/test_economy.exe
 */

#include <windows.h>
#include <stdio.h>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

extern "C" {
#include "screen_capture.h"
#include "ocr_engine.h"
#include "farm_economy.h"
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

static void SendKey(HWND hwnd, WORD vk) {
    UINT sc = MapVirtualKey(vk, MAPVK_VK_TO_VSC);
    LPARAM down = 1 | (sc << 16);
    LPARAM up   = down | (1u << 30) | (1u << 31);
    PostMessageW(hwnd, WM_KEYDOWN, vk, down);
    Sleep(60);
    PostMessageW(hwnd, WM_KEYUP, vk, up);
}

int wmain(int argc, wchar_t *argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);
    /* argv[1] = number of ESC presses before capture (default 0)
     * argv[2] = OCR language (default en-US) */
    int esc_count = (argc > 1) ? _wtoi(argv[1]) : 0;
    const WCHAR *lang = (argc > 2) ? argv[2] : L"en-US";

    HWND hwnd = FindGameWindow();
    if (!hwnd) { wprintf(L"game window not found\n"); return 1; }

    for (int i = 0; i < esc_count; i++) {
        SendKey(hwnd, VK_ESCAPE);
        Sleep(900);
    }

    if (!ScreenCapture_Init() || !ScreenCapture_StartCapture(hwnd)) {
        wprintf(L"capture init failed\n"); return 1;
    }
    if (!FarmEconomy_Init(lang)) {
        wprintf(L"OCR init failed\n"); return 1;
    }

    /* Pump fresh frames */
    CaptureFrame frame = {};
    BOOL got = FALSE;
    for (int i = 0; i < 8; i++) {
        if (ScreenCapture_GrabFrame(&frame)) got = TRUE;
        Sleep(45);
    }
    if (!got) { wprintf(L"no frame\n"); return 1; }
    wprintf(L"frame %dx%d stride=%d\n", frame.width, frame.height, frame.stride);

    /* Save frame for visual reference */
    cv::Mat bgra(frame.height, frame.width, CV_8UC4, frame.pixels, frame.stride);
    cv::Mat bgr; cv::cvtColor(bgra, bgr, cv::COLOR_BGRA2BGR);
    cv::imwrite("build/econ_frame.png", bgr);
    wprintf(L"saved build/econ_frame.png\n");

    /* Full-frame OCR, dump every word with pixel + normalized coords */
    static OcrResult r = {};
    if (!OcrEngine_Recognize(frame.pixels, frame.width, frame.height, frame.stride, &r)) {
        wprintf(L"OCR: no text\n");
    } else {
        wprintf(L"OCR: %d lines\n", r.line_count);
        for (int i = 0; i < r.line_count; i++) {
            wprintf(L"[L%d] %ls\n", i, r.lines[i].full_text);
            for (int j = 0; j < r.lines[i].word_count; j++) {
                OcrWord *w = &r.lines[i].words[j];
                double nx = (double)w->bounds.left / frame.width;
                double ny = (double)w->bounds.top / frame.height;
                double nw = (double)(w->bounds.right - w->bounds.left) / frame.width;
                double nh = (double)(w->bounds.bottom - w->bounds.top) / frame.height;
                wprintf(L"    \"%ls\"  px(%ld,%ld,%ld,%ld)  norm(%.3f,%.3f,%.3f,%.3f)\n",
                        w->text, w->bounds.left, w->bounds.top,
                        w->bounds.right, w->bounds.bottom, nx, ny, nw, nh);
            }
        }
    }

    /* Test the calibrated economy readers. ESC toggles menu/free-roam, so if
     * the balance isn't found (likely free-roam), press ESC again and retry. */
    int bal = FarmEconomy_ReadBalance(frame.pixels, frame.width, frame.height, frame.stride);
    for (int attempt = 0; bal < 1000 && attempt < 4; attempt++) {
        SendKey(hwnd, VK_ESCAPE);    /* toggle menu */
        Sleep(1000);
        for (int i = 0; i < 6; i++) { ScreenCapture_GrabFrame(&frame); Sleep(45); }
        cv::Mat b2(frame.height, frame.width, CV_8UC4, frame.pixels, frame.stride);
        cv::Mat g2; cv::cvtColor(b2, g2, cv::COLOR_BGRA2BGR);
        cv::imwrite("build/econ_frame.png", g2);
        bal = FarmEconomy_ReadBalance(frame.pixels, frame.width, frame.height, frame.stride);
    }
    int sp  = FarmEconomy_ReadSkillPoints(frame.pixels, frame.width, frame.height, frame.stride);
    wprintf(L"\n[ECON] balance=%d  skill=%d  count(cost81700,sp30)=%d\n",
            bal, sp, FarmEconomy_ComputeCount(bal, sp, 81700, 30));

    ScreenCapture_StopCapture();
    FarmEconomy_Shutdown();
    ScreenCapture_Shutdown();
    return 0;
}
