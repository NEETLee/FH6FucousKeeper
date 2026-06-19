/*
 * test_buy_diag.cpp - Step-by-step buy_car diagnostic
 *
 * Runs each navigation step of buy_car, saving a labeled screenshot
 * (with the matched bounding box drawn) after every action so we can
 * visually verify each step lands on the correct UI element.
 *
 * Build: make test-buy-diag
 * Run:   build/test_buy_diag.exe
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>

extern "C" {
#include "screen_capture.h"
#include "template_match.h"
#include "game_input.h"
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

static CaptureFrame g_frame = {};

/* Pump several frames so WGC delivers the freshest one (avoids stale frames). */
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

/* Save current frame with an optional marker box + label */
static void save_shot(const char *label, const char *file, TMResult *r) {
    if (!g_frame.pixels) return;
    cv::Mat bgra(g_frame.height, g_frame.width, CV_8UC4, g_frame.pixels, g_frame.stride);
    cv::Mat bgr;
    cv::cvtColor(bgra, bgr, cv::COLOR_BGRA2BGR);

    if (r && r->found) {
        cv::rectangle(bgr, cv::Rect(r->x, r->y, r->w, r->h),
                      cv::Scalar(0, 0, 255), 4);
        cv::circle(bgr, cv::Point(r->cx, r->cy), 10, cv::Scalar(0, 255, 0), -1);
        char txt[128];
        snprintf(txt, sizeof(txt), "%s score=%.2f", label, r->score);
        cv::putText(bgr, txt, cv::Point(r->x, r->y - 12),
                    cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 0, 255), 2);
    } else {
        cv::putText(bgr, label, cv::Point(40, 60),
                    cv::FONT_HERSHEY_SIMPLEX, 1.2, cv::Scalar(0, 0, 255), 3);
    }
    cv::imwrite(file, bgr);
    printf("  saved %s\n", file);
}

static const char *DIR = "assets/templates";
static char pbuf[512];
static const char* T(const char *name) {
    snprintf(pbuf, sizeof(pbuf), "%s/%s", DIR, name);
    return pbuf;
}

/* Report all 3 modes for a template within a region; return the best. */
static TMResult find_best(const char *name, const char *region) {
    grab();
    TMRegion rg = TM_NamedRegion(region);
    TMResult g = TM_FindGray(T(name), 0.0, TRUE, FALSE, rg);
    TMResult c = TM_FindColor(T(name), 0.0, TRUE, rg);
    TMResult t = TM_FindTransparent(T(name), 0.0, TRUE, rg);
    printf("  %-22s [%s] gray=%.3f color=%.3f transp=%.3f\n",
           name, region, g.score, c.score, t.score);
    TMResult best = g;
    if (c.score > best.score) best = c;
    if (t.score > best.score) best = t;
    return best;
}

/* Find with region + mode (g/c/t), reporting result. */
static TMResult find(const char *name, char mode, double th, const char *region) {
    grab();
    TMRegion rg = TM_NamedRegion(region);
    TMResult r;
    if (mode == 'c')      r = TM_FindColor(T(name), th, TRUE, rg);
    else if (mode == 't') r = TM_FindTransparent(T(name), th, TRUE, rg);
    else                  r = TM_FindGray(T(name), th, TRUE, FALSE, rg);
    printf("  find %-22s [%c %s] found=%d score=%.3f pos=(%d,%d) scale=%.3f\n",
           name, mode, region, r.found, r.score, r.cx, r.cy, r.scale);
    return r;
}

int wmain() {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=== Buy Car STEP DIAGNOSTIC ===\n\n");

    HWND hwnd = FindGameWindow();
    if (!hwnd) { printf("FAIL: no game window\n"); return 1; }
    printf("Game window: %p\n", (void*)hwnd);

    ScreenCapture_Init();
    ScreenCapture_StartCapture(hwnd);
    Sleep(800);
    TM_Init();

    GameInput *gi = GameInput_Create();
    GameInput_Init(gi, hwnd);

    /* Step 0: snapshot current state */
    printf("\n[step0] initial state\n");
    grab();
    save_shot("step0 initial", "build/diag_0_initial.png", NULL);

    /* Step 1: enter menu - gray, "left" region, 0.70 (faithful to FH6Auto) */
    printf("\n[step1] enter_menu loop (collectionjournal gray/left/0.70)\n");
    TMResult cj = {};
    BOOL in_menu = FALSE;
    for (int i = 0; i < 10; i++) {
        cj = find("collectionjournal.png", 'g', 0.70, "left");
        if (cj.found) { in_menu = TRUE; break; }
        printf("  not in menu, pressing ESC (%d/10)...\n", i+1);
        GameInput_Press(gi, VK_ESCAPE, 80);
        Sleep(1200);
    }
    save_shot("collectionjournal", "build/diag_1_cj.png", &cj);
    if (!in_menu) {
        printf("  FAILED to enter menu\n");
        GameInput_Destroy(gi); TM_Shutdown();
        ScreenCapture_StopCapture(); ScreenCapture_Shutdown();
        return 1;
    }

    /* Step 2: click collectionjournal (double-click) */
    printf("\n[step2] click collectionjournal\n");
    cj = find_best("collectionjournal.png", "left");
    save_shot("collectionjournal", "build/diag_1b_cj.png", &cj);
    printf("  dclick collectionjournal at (%d,%d) score=%.3f\n", cj.cx, cj.cy, cj.score);
    GameInput_MouseDoubleClick(gi, cj.cx, cj.cy);
    Sleep(1500);
    grab();
    save_shot("after click collectionjournal", "build/diag_2_after_cj.png", NULL);

    /* Step 3: masterexplorer */
    printf("\n[step3] looking for masterexplorer\n");
    TMResult me = find_best("masterexplorer.png", "full");
    save_shot("masterexplorer", "build/diag_3_me.png", &me);
    if (me.score >= 0.70) {
        printf("  dclick masterexplorer at (%d,%d) score=%.3f\n", me.cx, me.cy, me.score);
        GameInput_MouseDoubleClick(gi, me.cx, me.cy);
        Sleep(1200);
        grab();
        save_shot("after click masterexplorer", "build/diag_3b_after_me.png", NULL);
    } else {
        printf("  masterexplorer score too low, stopping\n");
    }

    /* Step 4: carcollection */
    printf("\n[step4] looking for carcollection\n");
    TMResult cc = find_best("carcollection.png", "full");
    save_shot("carcollection", "build/diag_4_cc.png", &cc);
    if (cc.score >= 0.70) {
        printf("  dclick carcollection at (%d,%d) score=%.3f\n", cc.cx, cc.cy, cc.score);
        GameInput_MouseDoubleClick(gi, cc.cx, cc.cy);
        Sleep(1500);
        grab();
        save_shot("after click carcollection", "build/diag_4b_after_cc.png", NULL);

        /* Step 5: backspace -> scroll UP to find 斯巴鲁(CCbrand) -> save each */
        printf("\n[step5] backspace + scroll up for CCbrand(斯巴鲁)\n");
        GameInput_Press(gi, VK_BACK, 80);
        Sleep(800);
        TMResult brand = {};
        for (int a = 0; a < 6; a++) {
            brand = find_best("CCbrand.png", "full");
            printf("  attempt %d: CCbrand best=%.3f at (%d,%d)\n", a, brand.score, brand.cx, brand.cy);
            char fn[64]; snprintf(fn, sizeof(fn), "build/diag_5_brand_%d.png", a);
            save_shot("CCbrand scan", fn, &brand);
            if (brand.score >= 0.75) break;
            GameInput_Press(gi, VK_UP, 80);
            Sleep(400);
        }

        /* Step 6: click brand -> down -> find consumablecar(22B) */
        if (brand.score >= 0.75) {
            printf("\n[step6] click brand(斯巴鲁) -> down -> consumablecar\n");
            GameInput_MouseClick(gi, brand.cx, brand.cy);
            Sleep(1000);
            grab();
            save_shot("after click brand", "build/diag_6_after_brand.png", NULL);
            GameInput_Press(gi, VK_DOWN, 80);
            Sleep(600);
            TMResult car = find_best("consumablecar.png", "full");
            printf("  consumablecar best=%.3f at (%d,%d)\n", car.score, car.cx, car.cy);
            save_shot("consumablecar(22B)", "build/diag_7_22b.png", &car);
        }
    }

    printf("\n=== DIAGNOSTIC DONE - check build/diag_*.png ===\n");

    GameInput_Destroy(gi);
    TM_Shutdown();
    ScreenCapture_StopCapture();
    ScreenCapture_Shutdown();
    return 0;
}
