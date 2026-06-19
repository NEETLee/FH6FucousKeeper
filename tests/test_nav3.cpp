/*
 * test_nav3.cpp - Background MOUSE navigation demo.
 *
 *   menu -> click 收集簿(collectionjournal) -> click 探索大师(masterexplorer)
 *        -> click 车辆收藏(carcollection) -> done
 *
 * Uses fresh-frame WGC capture + region-restricted multi-scale template
 * matching (FH6Auto params) to locate each element, then a real background
 * mouse click (WM_LBUTTONDOWN/UP) to activate it.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

extern "C" {
#include "screen_capture.h"
#include "game_input.h"
#include "template_match.h"
}

static CaptureFrame g_frame = {};
static GameInput   *g_gi = NULL;

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

static const char* T(const char *name) {
    static char buf[512];
    snprintf(buf, sizeof(buf), "assets/templates/%s", name);
    return buf;
}

/* Locate by best of gray/color/transparent within a region. */
static TMResult locate(const char *name, const char *region) {
    TMRegion rg = TM_NamedRegion(region);
    TMResult g = TM_FindGray(T(name), 0.0, TRUE, FALSE, rg);
    TMResult c = TM_FindColor(T(name), 0.0, TRUE, rg);
    TMResult t = TM_FindTransparent(T(name), 0.0, TRUE, rg);
    TMResult best = g;
    if (c.score > best.score) best = c;
    if (t.score > best.score) best = t;
    return best;
}

/* Wait until template appears (best score >= threshold), then return it. */
static TMResult wait_for(const char *name, const char *region, double th,
                         int timeout_ms) {
    DWORD start = GetTickCount();
    TMResult r = {};
    do {
        grab();
        r = locate(name, region);
        printf("    %-22s [%s] score=%.3f at (%d,%d)\n", name, region, r.score, r.cx, r.cy);
        if (r.score >= th) { r.found = TRUE; return r; }
        Sleep(300);
    } while ((int)(GetTickCount() - start) < timeout_ms);
    return r;
}

/* Locate + mouse click an element. */
static BOOL click_element(const char *name, const char *region, double th,
                          int timeout_ms) {
    printf("  -> %s\n", name);
    TMResult r = wait_for(name, region, th, timeout_ms);
    if (!r.found) { printf("     NOT FOUND (best %.3f)\n", r.score); return FALSE; }
    printf("     DOUBLE-CLICK at (%d,%d) score=%.3f\n", r.cx, r.cy, r.score);
    GameInput_MouseDoubleClick(g_gi, r.cx, r.cy);
    Sleep(1800);
    return TRUE;
}

int main(void) {
    HWND hwnd = FindWindowW(NULL, L"Forza Horizon 6");
    if (!hwnd) hwnd = FindWindowW(NULL, L"Forza Horizon 5");
    if (!hwnd) { printf("FH6 window not found\n"); return 1; }
    printf("Game window: %p\n", (void*)hwnd);

    if (!ScreenCapture_Init() || !ScreenCapture_StartCapture(hwnd)) {
        printf("capture init failed\n"); return 1;
    }
    TM_Init();
    g_gi = GameInput_Create();
    GameInput_Init(g_gi, hwnd);
    Sleep(300);

    /* Step 0: enter menu (ESC until collectionjournal visible in left/0.70). */
    printf("\n[enter_menu]\n");
    BOOL in_menu = FALSE;
    for (int i = 0; i < 12; i++) {
        grab();
        TMRegion left = TM_NamedRegion("left");
        TMResult r = TM_FindGray(T("collectionjournal.png"), 0.70, TRUE, FALSE, left);
        printf("  cj gray/left = %.3f\n", r.score);
        if (r.found) { in_menu = TRUE; break; }
        GameInput_Press(g_gi, VK_ESCAPE, 80);
        Sleep(1200);
    }
    if (!in_menu) { printf("  failed to reach menu\n"); return 1; }

    /* Step 1: 收集簿 (left region). */
    printf("\n[1] 收集簿 collectionjournal\n");
    if (!click_element("collectionjournal.png", "left", 0.65, 8000)) goto done;

    /* Step 2: 探索大师 (full region). */
    printf("\n[2] 探索大师 masterexplorer\n");
    if (!click_element("masterexplorer.png", "full", 0.75, 10000)) goto done;

    /* Step 3: 车辆收藏 (full region). */
    printf("\n[3] 车辆收藏 carcollection\n");
    if (!click_element("carcollection.png", "full", 0.75, 10000)) goto done;

    printf("\n[DONE] navigation complete\n");

done:
    GameInput_Destroy(g_gi);
    TM_Shutdown();
    ScreenCapture_StopCapture();
    ScreenCapture_Shutdown();
    return 0;
}
