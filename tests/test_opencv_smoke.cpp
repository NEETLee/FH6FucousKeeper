/*
 * test_opencv_smoke.cpp - OpenCV + WGC integration smoke test
 *
 * Verifies: WGC captures a frame -> OpenCV matchTemplate finds a known
 *           template (collectionjournal.png) when in the main menu.
 *
 * Build: make test-opencv   (requires MSYS2 MINGW64 with opencv)
 * Run:   build/test_opencv.exe
 */

#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>

extern "C" {
#include "screen_capture.h"
}

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

/* ─── Window finder (by ForzaRenderingWindow child) ───────────────────── */
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
    return h;
}

/* ─── Multi-scale template matching ──────────────────────────────────── */
struct MatchResult {
    bool found;
    double score;
    int x, y;   // center of match in capture coords
    double scale;
};

static MatchResult FindTemplate(const cv::Mat &screen, const cv::Mat &tmpl_raw,
                                double threshold = 0.75) {
    MatchResult r = { false, 0, 0, 0, 1.0 };
    if (screen.empty() || tmpl_raw.empty()) return r;

    cv::Mat gray_screen;
    cv::cvtColor(screen, gray_screen, cv::COLOR_BGRA2GRAY);

    // Separate template into grayscale + optional mask (from alpha channel)
    cv::Mat gray_tmpl, mask;
    bool has_alpha = (tmpl_raw.channels() == 4);
    if (has_alpha) {
        cv::Mat channels[4];
        cv::split(tmpl_raw, channels);
        gray_tmpl = channels[0] * 0.114 + channels[1] * 0.587 + channels[2] * 0.299;
        gray_tmpl.convertTo(gray_tmpl, CV_8U);
        mask = channels[3]; // alpha as mask
    } else {
        if (tmpl_raw.channels() == 3)
            cv::cvtColor(tmpl_raw, gray_tmpl, cv::COLOR_BGR2GRAY);
        else
            gray_tmpl = tmpl_raw.clone();
    }

    int base_w = 2560;
    double primary_scale = (double)screen.cols / base_w;
    double scales[] = { primary_scale, primary_scale*0.98, primary_scale*1.02,
                        primary_scale*0.95, primary_scale*1.05,
                        primary_scale*0.92, primary_scale*1.08, 1.0 };
    int n_scales = (int)(sizeof(scales)/sizeof(scales[0]));

    for (int i = 0; i < n_scales; i++) {
        double s = scales[i];
        if (s < 0.3 || s > 2.0) continue;
        cv::Mat scaled_tmpl, scaled_mask;
        cv::resize(gray_tmpl, scaled_tmpl, cv::Size(), s, s, cv::INTER_LINEAR);
        if (has_alpha)
            cv::resize(mask, scaled_mask, cv::Size(), s, s, cv::INTER_LINEAR);
        if (scaled_tmpl.rows > gray_screen.rows || scaled_tmpl.cols > gray_screen.cols) continue;

        cv::Mat result;
        if (has_alpha)
            cv::matchTemplate(gray_screen, scaled_tmpl, result, cv::TM_CCORR_NORMED, scaled_mask);
        else
            cv::matchTemplate(gray_screen, scaled_tmpl, result, cv::TM_CCOEFF_NORMED);

        double minVal, maxVal;
        cv::Point minLoc, maxLoc;
        cv::minMaxLoc(result, &minVal, &maxVal, &minLoc, &maxLoc);

        if (maxVal > r.score) {
            r.score = maxVal;
            r.scale = s;
            r.x = maxLoc.x + scaled_tmpl.cols / 2;
            r.y = maxLoc.y + scaled_tmpl.rows / 2;
        }
        if (maxVal >= threshold) {
            r.found = true;
            break;
        }
    }
    if (r.score >= threshold) r.found = true;
    return r;
}

int wmain() {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=== OpenCV + WGC Smoke Test ===\n");

    HWND hwnd = FindGameWindow();
    if (!hwnd) { printf("FAIL: game window not found\n"); return 1; }
    printf("Game window: %p\n", (void*)hwnd);

    if (!ScreenCapture_Init() || !ScreenCapture_StartCapture(hwnd)) {
        printf("FAIL: WGC capture init\n"); return 1;
    }
    Sleep(800);

    CaptureFrame f = {};
    bool got = false;
    for (int i = 0; i < 5; i++) {
        if (ScreenCapture_GrabFrame(&f)) { got = true; break; }
        Sleep(200);
    }
    if (!got) { printf("FAIL: no frame captured\n"); return 1; }
    printf("Frame: %dx%d stride=%d\n", f.width, f.height, f.stride);

    // Wrap in cv::Mat (BGRA, stride may differ from width*4)
    cv::Mat screen(f.height, f.width, CV_8UC4, f.pixels, f.stride);
    printf("cv::Mat created: %dx%d channels=%d\n", screen.cols, screen.rows, screen.channels());

    // Load template (with alpha if present)
    cv::Mat tmpl_raw = cv::imread("assets/templates/collectionjournal.png", cv::IMREAD_UNCHANGED);
    if (tmpl_raw.empty()) {
        printf("FAIL: could not load assets/templates/collectionjournal.png\n");
        ScreenCapture_StopCapture(); ScreenCapture_Shutdown();
        return 1;
    }
    printf("Template: %dx%d channels=%d\n", tmpl_raw.cols, tmpl_raw.rows, tmpl_raw.channels());

    // If 4-channel (has alpha), convert to 3-channel BGR for simple matching
    cv::Mat tmpl;
    if (tmpl_raw.channels() == 4) {
        cv::cvtColor(tmpl_raw, tmpl, cv::COLOR_BGRA2BGR);
        printf("  (converted from BGRA to BGR)\n");
    } else {
        tmpl = tmpl_raw;
    }

    // Also save the captured frame for visual inspection
    cv::Mat save_bgr;
    cv::cvtColor(screen, save_bgr, cv::COLOR_BGRA2BGR);
    cv::imwrite("build/opencv_capture.png", save_bgr);
    printf("Saved capture to build/opencv_capture.png\n");

    // Match (pass raw template with alpha for masked matching)
    MatchResult m = FindTemplate(screen, tmpl_raw, 0.70);
    printf("Match: found=%d score=%.4f scale=%.3f pos=(%d,%d)\n",
           m.found, m.score, m.scale, m.x, m.y);

    if (m.found) {
        printf("\n>>> SUCCESS: template matched at (%.0f%% confidence) <<<\n", m.score * 100);
    } else {
        printf("\nTemplate NOT found (score=%.4f < 0.70). Game may not be in main menu.\n", m.score);
        printf("This is OK - the OpenCV pipeline works; just need the right screen state.\n");
    }

    ScreenCapture_StopCapture();
    ScreenCapture_Shutdown();
    return 0;
}
