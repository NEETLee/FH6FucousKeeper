/*
 * test_match_all.cpp - Batch template matching validation
 *
 * Captures a single frame from the game (or loads a fixture PNG) and
 * runs all templates through TM_FindImage, reporting scores.
 * This validates the matching pipeline works with real templates.
 *
 * Build: make test-match-all
 * Run:   build/test_match_all.exe [fixture.png]
 */

#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>

extern "C" {
#include "screen_capture.h"
#include "template_match.h"
}

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

/* ─── Window finder ───────────────────────────────────────────────────── */
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

int wmain(int argc, wchar_t *argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=== Batch Template Match Validation ===\n\n");

    TM_Init();

    BOOL from_file = FALSE;
    if (argc > 1) {
        char path[512];
        WideCharToMultiByte(CP_UTF8, 0, argv[1], -1, path, 512, NULL, NULL);
        cv::Mat img = cv::imread(path, cv::IMREAD_UNCHANGED);
        if (img.empty()) { printf("FAIL: cannot load %s\n", path); return 1; }
        if (img.channels() == 3) cv::cvtColor(img, img, cv::COLOR_BGR2BGRA);
        TM_SetFrame(img.data, img.cols, img.rows, (int)img.step);
        printf("Source: %s (%dx%d)\n\n", path, img.cols, img.rows);
        from_file = TRUE;
    } else {
        HWND hwnd = FindGameWindow();
        if (!hwnd) { printf("FAIL: game window not found\n"); return 1; }
        printf("Game window: %p\n", (void*)hwnd);
        if (!ScreenCapture_Init() || !ScreenCapture_StartCapture(hwnd)) {
            printf("FAIL: WGC init\n"); return 1;
        }
        Sleep(800);
        CaptureFrame f = {};
        for (int i = 0; i < 5; i++) { if (ScreenCapture_GrabFrame(&f)) break; Sleep(200); }
        if (!f.pixels) { printf("FAIL: no frame\n"); return 1; }
        TM_SetFrame(f.pixels, f.width, f.height, f.stride);
        printf("Live frame: %dx%d\n\n", f.width, f.height);
    }

    const char *tmpl_dir = "assets/templates";
    DIR *d = opendir(tmpl_dir);
    if (!d) { printf("FAIL: cannot open %s\n", tmpl_dir); return 1; }

    int total = 0, matched = 0;
    printf("%-30s %8s %8s %10s %10s\n", "TEMPLATE", "FOUND", "SCORE", "POS", "SCALE");
    printf("%-30s %8s %8s %10s %10s\n", "--------", "-----", "-----", "---", "-----");

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (!strstr(ent->d_name, ".png")) continue;
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", tmpl_dir, ent->d_name);

        TMResult r = TM_FindImageTransparent(path, 0.70);
        total++;
        if (r.found) matched++;
        printf("%-30s %8s %8.4f   (%4d,%4d) %8.3f\n",
               ent->d_name,
               r.found ? "YES" : "no",
               r.score,
               r.cx, r.cy,
               r.scale);
    }
    closedir(d);

    printf("\n--- Summary: %d/%d templates matched (threshold=0.70) ---\n", matched, total);

    if (!from_file) {
        ScreenCapture_StopCapture();
        ScreenCapture_Shutdown();
    }
    TM_Shutdown();
    return 0;
}
