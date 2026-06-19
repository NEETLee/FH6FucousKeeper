/*
 * test_nav_buy.cpp - Navigate to 购买新车 instead of 我的车辆
 * Try: PageDown → DOWN (to move from default to 购买新车) → ENTER
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>

extern "C" {
#include "screen_capture.h"
#include "ocr_engine.h"
}
#include "input_backend.h"
#include "input_hook_backend.h"

static FILE *g_log = nullptr;
static InputBackend *g_input = nullptr;

static void logw(const WCHAR *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    WCHAR buf[4096];
    int len = vswprintf(buf, 4096, fmt, ap);
    va_end(ap);
    if (len > 0 && g_log) {
        int utf8_len = WideCharToMultiByte(CP_UTF8, 0, buf, len, NULL, 0, NULL, NULL);
        if (utf8_len > 0) {
            char *utf8 = (char*)malloc(utf8_len + 1);
            WideCharToMultiByte(CP_UTF8, 0, buf, len, utf8, utf8_len, NULL, NULL);
            utf8[utf8_len] = '\0';
            fputs(utf8, g_log);
            fflush(g_log);
            fputs(utf8, stdout);
            free(utf8);
        }
    }
}

static void do_ocr(const char *label) {
    CaptureFrame frame = {};
    if (!ScreenCapture_GrabFrame(&frame)) { logw(L"[%hs] FAIL\n", label); return; }
    static OcrResult result = {};
    memset(&result, 0, sizeof(result));
    BOOL ok = OcrEngine_Recognize(frame.pixels, frame.width, frame.height, frame.stride, &result);
    logw(L"\n[%hs]:\n", label);
    if (ok && result.line_count > 0) {
        for (int i = 0; i < result.line_count && i < 30; i++) {
            OcrLine *l = &result.lines[i];
            if (l->word_count > 0)
                logw(L"  (%3d,%3d) %ls\n", l->words[0].bounds.left, l->words[0].bounds.top, l->full_text);
        }
    } else { logw(L"  (empty)\n"); }
}

static bool ocr_contains(const WCHAR *target) {
    CaptureFrame frame = {};
    if (!ScreenCapture_GrabFrame(&frame)) return false;
    static OcrResult result = {};
    memset(&result, 0, sizeof(result));
    if (!OcrEngine_Recognize(frame.pixels, frame.width, frame.height, frame.stride, &result))
        return false;
    WCHAR tns[256] = {};
    int ti = 0;
    for (const WCHAR *p = target; *p && ti < 255; p++)
        if (*p != L' ') tns[ti++] = *p;
    tns[ti] = 0;
    for (int i = 0; i < result.line_count; i++) {
        WCHAR lns[1024] = {};
        int li = 0;
        for (const WCHAR *p = result.lines[i].full_text; *p && li < 1023; p++)
            if (*p != L' ') lns[li++] = *p;
        lns[li] = 0;
        if (wcsstr(lns, tns)) return true;
    }
    return false;
}

static void key(DWORD vk, int hold_ms = 100) {
    g_input->key_down(g_input, vk);
    Sleep(hold_ms);
    g_input->key_up(g_input, vk);
}

int wmain() {
    setvbuf(stdout, NULL, _IONBF, 0);
    g_log = fopen("build/nav_buy_log.txt", "w");
    if (!g_log) g_log = stdout;
    fprintf(g_log, "\xEF\xBB\xBF");

    HWND hwnd = FindWindowW(NULL, L"Forza Horizon 6");
    if (!hwnd) { logw(L"Window not found\n"); return 1; }
    if (!ScreenCapture_Init()) return 1;
    if (!OcrEngine_Init(L"zh-Hans-CN")) return 1;
    if (!ScreenCapture_StartCapture(hwnd)) return 1;
    Sleep(500);

    g_input = HookBackend_Create();
    if (!g_input || !g_input->init(g_input, hwnd)) return 1;
    logw(L"Ready.\n");

    /* === Phase 0: Get back to main menu === */
    logw(L"--- Get to main menu ---\n");
    for (int i = 0; i < 10; i++) {
        key(VK_ESCAPE); Sleep(1200);
        if (ocr_contains(L"收集簿")) {
            logw(L"  Found 收集簿 at ESC %d\n", i+1);
            break;
        }
    }
    Sleep(1000);
    do_ocr("Main menu");

    /* === Test A: PageDown → (no DOWN) → ENTER → see where we go === */
    logw(L"\n=== Test A: PgDn → ENTER (baseline) ===\n");
    key(VK_NEXT); Sleep(1200);
    key(VK_RETURN); Sleep(6000);
    do_ocr("A: PgDn+Enter");

    /* Go back */
    key(VK_ESCAPE); Sleep(1500);
    key(VK_ESCAPE); Sleep(1500);
    key(VK_ESCAPE); Sleep(1500);
    key(VK_ESCAPE); Sleep(1500);

    /* Wait for main menu */
    Sleep(2000);
    for (int i = 0; i < 5; i++) {
        if (ocr_contains(L"收集簿")) break;
        key(VK_ESCAPE); Sleep(1500);
    }
    do_ocr("Back to menu");

    /* === Test B: PageDown → DOWN×1 → ENTER === */
    logw(L"\n=== Test B: PgDn → DOWN×1 → ENTER ===\n");
    key(VK_NEXT); Sleep(1200);
    key(VK_DOWN); Sleep(800);
    key(VK_RETURN); Sleep(6000);
    do_ocr("B: PgDn+Down1+Enter");

    /* Go back */
    for (int i = 0; i < 5; i++) { key(VK_ESCAPE); Sleep(1500); }
    Sleep(2000);
    for (int i = 0; i < 5; i++) {
        if (ocr_contains(L"收集簿")) break;
        key(VK_ESCAPE); Sleep(1500);
    }

    /* === Test C: PageDown → DOWN×2 → ENTER === */
    logw(L"\n=== Test C: PgDn → DOWN×2 → ENTER ===\n");
    key(VK_NEXT); Sleep(1200);
    key(VK_DOWN); Sleep(500);
    key(VK_DOWN); Sleep(800);
    key(VK_RETURN); Sleep(6000);
    do_ocr("C: PgDn+Down2+Enter");

    /* Go back */
    for (int i = 0; i < 5; i++) { key(VK_ESCAPE); Sleep(1500); }
    Sleep(2000);
    for (int i = 0; i < 5; i++) {
        if (ocr_contains(L"收集簿")) break;
        key(VK_ESCAPE); Sleep(1500);
    }

    /* === Test D: PageDown×2 → ENTER (different tab) === */
    logw(L"\n=== Test D: PgDn×2 → ENTER ===\n");
    key(VK_NEXT); Sleep(1200);
    key(VK_NEXT); Sleep(1200);
    key(VK_RETURN); Sleep(6000);
    do_ocr("D: PgDn2+Enter");

    logw(L"\n=== DONE ===\n");
    g_input->release_all(g_input);
    g_input->shutdown(g_input);
    g_input->destroy(g_input);
    ScreenCapture_StopCapture();
    OcrEngine_Shutdown();
    ScreenCapture_Shutdown();
    if (g_log != stdout) fclose(g_log);
    return 0;
}
