/*
 * test_flows.cpp - Run any farm flow in the background for live demo.
 *
 * Build: make test-flows
 * Run:   build/test_flows.exe <buy|wheelspin|remove> [count]
 *
 * Integrates WGC capture + template_match + game_input + farm_flow, using
 * the verified background mouse-click navigation.
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>

extern "C" {
#include "screen_capture.h"
#include "template_match.h"
#include "game_input.h"
#include "farm_flow.h"
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
    if (!h) h = FindWindowW(NULL, L"Forza Horizon 5");
    return h;
}

static void log_cb(const char *msg, void *ctx) {
    (void)ctx;
    SYSTEMTIME st; GetLocalTime(&st);
    printf("[%02d:%02d:%02d] %s\n", st.wHour, st.wMinute, st.wSecond, msg);
}

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
    const wchar_t *flow = (argc > 1) ? argv[1] : L"buy";
    int count = (argc > 2) ? _wtoi(argv[2]) : 1;
    /* Default to mode 2 (wheelspin: from Design & Paint) -- the preferred path. */
    int mode  = (argc > 3) ? _wtoi(argv[3]) : 2;
    if (count < 1) count = 1;
    if (mode != 1 && mode != 2) mode = 2;

    char flow_a[32] = {};
    WideCharToMultiByte(CP_ACP, 0, flow, -1, flow_a, sizeof(flow_a), NULL, NULL);
    printf("=== FH6 Flow Test: %s (count=%d) ===\n\n", flow_a, count);

    HWND hwnd = FindGameWindow();
    if (!hwnd) { printf("FAIL: game window not found\n"); return 1; }
    printf("Game window: %p\n", (void*)hwnd);

    if (!ScreenCapture_Init() || !ScreenCapture_StartCapture(hwnd)) {
        printf("FAIL: capture init\n"); return 1;
    }
    Sleep(800);
    TM_Init();

    FarmConfig cfg = {};
    cfg.game_hwnd = hwnd;
    cfg.assets_dir = "assets/templates";
    cfg.log_func = log_cb;
    cfg.grab_func = grab_cb;

    FarmEngine *fe = Farm_Create();
    if (!Farm_Init(fe, &cfg)) { printf("FAIL: Farm_Init\n"); Farm_Destroy(fe); return 1; }

    int result = 0;
    if (_wcsicmp(flow, L"buy") == 0) {
        result = Farm_BuyCar(fe, count);
        printf("\n=== RESULT: bought %d/%d ===\n", result, count);
    } else if (_wcsicmp(flow, L"wheelspin") == 0) {
        printf("wheelspin mode = %d\n", mode);
        result = Farm_SuperWheelspinMode(fe, count, mode);
        printf("\n=== RESULT: wheelspins %d/%d ===\n", result, count);
    } else if (_wcsicmp(flow, L"remove") == 0) {
        printf("remove mode = %d\n", mode);
        result = Farm_RemoveCarMode(fe, count, mode);
        printf("\n=== RESULT: removed %d/%d ===\n", result, count);
    } else if (_wcsicmp(flow, L"loop") == 0) {
        printf(">>> FULL LOOP: buy %d -> wheelspin %d -> remove %d\n\n", count, count, count);
        int bought = Farm_BuyCar(fe, count);
        printf("\n>>> bought %d/%d\n\n", bought, count);
        int spun = Farm_SuperWheelspinMode(fe, count, mode);
        printf("\n>>> wheelspins %d/%d\n\n", spun, count);
        int removed = Farm_RemoveCarMode(fe, count, 1);
        printf("\n>>> removed %d/%d\n\n", removed, count);
        printf("=== LOOP RESULT: bought=%d spun=%d removed=%d (target=%d) ===\n",
               bought, spun, removed, count);
    } else {
        printf("unknown flow '%s' (use buy|wheelspin|remove)\n", flow_a);
    }

    Farm_Destroy(fe);
    TM_Shutdown();
    ScreenCapture_StopCapture();
    ScreenCapture_Shutdown();
    return 0;
}
