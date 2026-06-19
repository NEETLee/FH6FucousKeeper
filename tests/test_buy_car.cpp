/*
 * test_buy_car.cpp - End-to-end buy_car flow test
 *
 * Integrates: WGC capture + template_match + game_input + farm_flow
 * to run the full buy_car sequence in the background.
 *
 * Build: make test-buy-car
 * Run:   build/test_buy_car.exe [count]   (default: 1)
 */

#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>

extern "C" {
#include "screen_capture.h"
#include "template_match.h"
#include "game_input.h"
#include "farm_flow.h"
}

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

/* ─── Log callback ────────────────────────────────────────────────────── */
static void log_cb(const char *msg, void *ctx) {
    (void)ctx;
    SYSTEMTIME st;
    GetLocalTime(&st);
    printf("[%02d:%02d:%02d] %s\n", st.wHour, st.wMinute, st.wSecond, msg);
}

/* ─── Frame grab callback (pump for fresh frame) ──────────────────────── */
static BOOL grab_cb(void *ctx) {
    (void)ctx;
    CaptureFrame last = {};
    BOOL got = FALSE;
    for (int i = 0; i < 8; i++) {
        CaptureFrame f = {};
        if (ScreenCapture_GrabFrame(&f)) { last = f; got = TRUE; }
        Sleep(45);
    }
    if (!got) return FALSE;
    TM_SetFrame(last.pixels, last.width, last.height, last.stride);
    return TRUE;
}

int wmain(int argc, wchar_t *argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);
    int target = 1;
    if (argc > 1) target = _wtoi(argv[1]);
    if (target < 1) target = 1;

    printf("=== FH6 Buy Car Test (target=%d) ===\n\n", target);

    HWND hwnd = FindGameWindow();
    if (!hwnd) { printf("FAIL: game window not found\n"); return 1; }
    printf("Game window: %p\n", (void*)hwnd);

    if (!ScreenCapture_Init()) { printf("FAIL: ScreenCapture_Init\n"); return 1; }
    if (!ScreenCapture_StartCapture(hwnd)) { printf("FAIL: StartCapture\n"); return 1; }
    Sleep(800);

    TM_Init();

    FarmConfig cfg = {};
    cfg.game_hwnd = hwnd;
    cfg.assets_dir = "assets/templates";
    cfg.log_func = log_cb;
    cfg.log_ctx = NULL;
    cfg.grab_func = grab_cb;
    cfg.grab_ctx = NULL;

    FarmEngine *fe = Farm_Create();
    if (!Farm_Init(fe, &cfg)) {
        printf("FAIL: Farm_Init\n");
        Farm_Destroy(fe);
        return 1;
    }

    printf("Starting buy_car flow...\n\n");
    int bought = Farm_BuyCar(fe, target);
    printf("\n=== RESULT: bought %d/%d cars ===\n", bought, target);

    Farm_Destroy(fe);
    TM_Shutdown();
    ScreenCapture_StopCapture();
    ScreenCapture_Shutdown();
    return (bought >= target) ? 0 : 1;
}
