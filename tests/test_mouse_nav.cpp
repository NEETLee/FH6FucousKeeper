/*
 * test_mouse_nav.cpp - Demonstrate whether background MOUSE CLICKS work.
 *
 * Sequence:
 *   1. Find FH6 window, start WGC capture.
 *   2. enter_menu (fresh-frame ESC loop until collectionjournal in left/0.70).
 *   3. Locate collectionjournal, send a REAL mouse click (WM_LBUTTONDOWN/UP).
 *   4. Capture before/after, report screen-change %, save PNGs.
 *
 * If the mouse click works, the screen will change to the collection journal.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdlib>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

extern "C" {
#include "screen_capture.h"
#include "game_input.h"
#include "template_match.h"
}

static CaptureFrame g_frame = {};

static BOOL grab(void) {
    BOOL got = FALSE;
    for (int i = 0; i < 8; i++) {
        CaptureFrame f = {};
        if (ScreenCapture_GrabFrame(&f)) { g_frame = f; got = TRUE; }
        Sleep(45);
    }
    if (got) TM_SetFrame(g_frame.pixels, g_frame.width, g_frame.height, g_frame.stride);
    return got;
}

static cv::Mat frame_to_mat(void) {
    cv::Mat bgra(g_frame.height, g_frame.width, CV_8UC4, g_frame.pixels, g_frame.stride);
    cv::Mat bgr;
    cv::cvtColor(bgra, bgr, cv::COLOR_BGRA2BGR);
    return bgr.clone();
}

static double frame_diff(const cv::Mat &a, const cv::Mat &b) {
    if (a.empty() || b.empty() || a.size() != b.size()) return 100.0;
    cv::Mat d; cv::absdiff(a, b, d);
    cv::cvtColor(d, d, cv::COLOR_BGR2GRAY);
    cv::threshold(d, d, 30, 255, cv::THRESH_BINARY);
    return 100.0 * cv::countNonZero(d) / (double)(d.rows * d.cols);
}

static HWND find_game(void) {
    HWND h = FindWindowW(NULL, L"Forza Horizon 6");
    if (!h) h = FindWindowW(NULL, L"Forza Horizon 5");
    return h;
}

int main(int argc, char **argv) {
    const char *target = (argc > 1) ? argv[1] : "collectionjournal.png";
    const char *region = (argc > 2) ? argv[2] : "left";
    char tpath[512];
    snprintf(tpath, sizeof(tpath), "assets/templates/%s", target);

    HWND hwnd = find_game();
    if (!hwnd) { printf("FH6 window not found\n"); return 1; }
    printf("Game window: %p\n", (void*)hwnd);

    if (!ScreenCapture_Init() || !ScreenCapture_StartCapture(hwnd)) {
        printf("capture init failed\n"); return 1;
    }
    TM_Init();
    GameInput *gi = GameInput_Create();
    GameInput_Init(gi, hwnd);
    Sleep(300);

    /* enter menu */
    printf("\n[enter_menu]\n");
    BOOL in_menu = FALSE;
    for (int i = 0; i < 10; i++) {
        grab();
        TMRegion left = TM_NamedRegion("left");
        TMResult r = TM_FindGray(tpath, 0.70, TRUE, FALSE, left);
        printf("  cj gray/left = %.3f (%d,%d)\n", r.score, r.cx, r.cy);
        if (r.found) { in_menu = TRUE; break; }
        GameInput_Press(gi, VK_ESCAPE, 80);
        Sleep(1200);
    }
    if (!in_menu) { printf("  not in menu, abort\n"); return 1; }

    /* locate target */
    grab();
    TMRegion rg = TM_NamedRegion(region);
    TMResult t = TM_FindGray(tpath, 0.0, TRUE, FALSE, rg);
    TMResult tc = TM_FindColor(tpath, 0.0, TRUE, rg);
    if (tc.score > t.score) t = tc;
    printf("\n[target] %s best score=%.3f at (%d,%d)\n", target, t.score, t.cx, t.cy);

    cv::Mat before = frame_to_mat();
    cv::imwrite("build/mouse_before.png", before);

    /* REAL mouse click */
    printf("[click] WM_LBUTTONDOWN/UP at (%d,%d)\n", t.cx, t.cy);
    GameInput_MouseClick(gi, t.cx, t.cy);
    Sleep(1800);

    grab();
    cv::Mat after = frame_to_mat();
    cv::imwrite("build/mouse_after.png", after);

    double diff = frame_diff(before, after);
    printf("\n[result] screen change = %.1f%%  -> %s\n", diff,
           diff > 15.0 ? "CHANGED (mouse click likely worked)"
                       : "no change (mouse click ineffective)");

    GameInput_Destroy(gi);
    TM_Shutdown();
    ScreenCapture_StopCapture();
    ScreenCapture_Shutdown();
    return 0;
}
