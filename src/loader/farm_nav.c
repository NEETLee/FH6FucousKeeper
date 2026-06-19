/*
 * farm_nav.c - Farm Navigation Engine
 *
 * Core state machine for automated game menu navigation.
 * Uses WGC background capture + OCR to verify screen state,
 * and PostMessage keyboard input to navigate menus.
 */

#include "farm_nav.h"
#include "screen_capture.h"
#include "ocr_engine.h"
#include "input_backend.h"
#include "input_hook_backend.h"
#include "logger.h"

#include <stdlib.h>
#include <string.h>
#include <wchar.h>

/* ─── Internal Types ─────────────────────────────────────────────── */

struct FarmEngine {
    /* Dependencies */
    InputBackend       *input;
    HWND                game_hwnd;
    FarmConfig          config;

    /* Callback */
    FarmProgressCallback callback;
    void               *cb_user_data;

    /* Thread control */
    HANDLE              thread;
    HANDLE              stop_event;
    HANDLE              pause_event;  /* signaled = not paused */

    /* Runtime state */
    volatile FarmState  state;
    int                 count;
};

/* ─── Primitives ─────────────────────────────────────────────────── */

static BOOL IsStopped(FarmEngine *e) {
    return WaitForSingleObject(e->stop_event, 0) != WAIT_TIMEOUT;
}

static void CheckPause(FarmEngine *e) {
    WaitForSingleObject(e->pause_event, INFINITE);
}

static BOOL WaitMs(FarmEngine *e, DWORD ms) {
    if (ms == 0) return !IsStopped(e);
    DWORD remaining = ms;
    while (remaining > 0) {
        DWORD slice = (remaining > 100) ? 100 : remaining;
        if (WaitForSingleObject(e->stop_event, slice) != WAIT_TIMEOUT)
            return FALSE;
        CheckPause(e);
        remaining -= slice;
    }
    return TRUE;
}

static BOOL PressKey(FarmEngine *e, DWORD vk, int post_delay_ms) {
    if (IsStopped(e)) return FALSE;
    CheckPause(e);
    e->input->key_down(e->input, vk);
    WaitMs(e, 80);
    e->input->key_up(e->input, vk);
    if (post_delay_ms > 0)
        return WaitMs(e, post_delay_ms);
    return !IsStopped(e);
}

static BOOL PressKeyN(FarmEngine *e, DWORD vk, int count, int interval_ms) {
    for (int i = 0; i < count; i++) {
        if (!PressKey(e, vk, interval_ms)) return FALSE;
    }
    return TRUE;
}

/* ─── OCR Helpers ────────────────────────────────────────────────── */

/*
 * Check if OCR text contains target, ignoring spaces.
 * Chinese OCR often inserts spaces between characters.
 */
static BOOL TextContainsIgnoreSpaces(const WCHAR *haystack, const WCHAR *needle) {
    if (!haystack || !needle) return FALSE;

    /* Build space-stripped version of haystack */
    int hlen = (int)wcslen(haystack);
    WCHAR *stripped = (WCHAR *)_alloca((hlen + 1) * sizeof(WCHAR));
    int si = 0;
    for (int i = 0; i < hlen; i++) {
        if (haystack[i] != L' ') stripped[si++] = haystack[i];
    }
    stripped[si] = L'\0';

    /* Also strip spaces from needle for comparison */
    int nlen = (int)wcslen(needle);
    WCHAR *needle_stripped = (WCHAR *)_alloca((nlen + 1) * sizeof(WCHAR));
    int ni = 0;
    for (int i = 0; i < nlen; i++) {
        if (needle[i] != L' ') needle_stripped[ni++] = needle[i];
    }
    needle_stripped[ni] = L'\0';

    return wcsstr(stripped, needle_stripped) != NULL;
}

static BOOL OcrContainsText(FarmEngine *e, const WCHAR *target, RECT *roi) {
    (void)e;
    CaptureFrame frame;
    if (!ScreenCapture_GrabFrame(&frame)) return FALSE;

    static OcrResult result;
    BOOL ok;
    if (roi) {
        ok = OcrEngine_RecognizeRegion(frame.pixels, frame.width, frame.height,
                                       frame.stride, *roi, &result);
    } else {
        ok = OcrEngine_Recognize(frame.pixels, frame.width, frame.height,
                                  frame.stride, &result);
    }
    if (!ok) return FALSE;

    return TextContainsIgnoreSpaces(result.all_text, target);
}

/*
 * Wait until OCR detects target text, or timeout.
 * Returns TRUE if found, FALSE if timeout or stopped.
 */
static BOOL WaitForOcr(FarmEngine *e, const WCHAR *target, int timeout_ms, RECT *roi) {
    DWORD start = GetTickCount();
    while ((int)(GetTickCount() - start) < timeout_ms) {
        if (IsStopped(e)) return FALSE;
        CheckPause(e);

        if (OcrContainsText(e, target, roi)) return TRUE;

        if (!WaitMs(e, 300)) return FALSE;
    }
    return FALSE;
}

/* ─── Navigation Building Blocks ─────────────────────────────────── */

/*
 * enter_menu: Press ESC repeatedly until OCR finds anchor text.
 * Equivalent to FH6Auto's enter_menu() function.
 */
static BOOL EnterMenu(FarmEngine *e, const WCHAR *anchor_text) {
    for (int attempt = 0; attempt < 60; attempt++) {
        if (IsStopped(e)) return FALSE;
        CheckPause(e);

        if (OcrContainsText(e, anchor_text, NULL)) {
            WaitMs(e, 500);
            return TRUE;
        }

        PressKey(e, VK_ESCAPE, 1000);
    }
    return FALSE;
}

/* ─── Notify Helper ──────────────────────────────────────────────── */

static void Notify(FarmEngine *e, FarmState state, const WCHAR *msg) {
    e->state = state;
    if (e->callback) {
        e->callback(state, e->count, e->config.target_count, msg, e->cb_user_data);
    }
}

/* ─── Flow: Remove Car (Mode 2 - Sort by Recent) ─────────────────── */

static DWORD WINAPI FlowRemoveCarRecent(LPVOID param) {
    FarmEngine *e = (FarmEngine *)param;
    Notify(e, FARM_RUNNING, L"移除车辆: 开始导航");

    /* Step 1: Enter main menu */
    if (!EnterMenu(e, L"\x6536\x96C6\x7C3F")) {  /* 收集簿 */
        Notify(e, FARM_ERROR, L"无法进入主菜单");
        return 1;
    }
    Notify(e, FARM_RUNNING, L"已进入主菜单");

    /* Step 2: PageDown to reveal buy/sell section */
    if (!PressKey(e, VK_NEXT, 1000)) return 0;

    /* Step 3: Enter (购买新车与二手车) */
    if (!PressKey(e, VK_RETURN, 5000)) return 0;
    Notify(e, FARM_RUNNING, L"等待加载...");

    /* Step 4: Wait for "购买与出售" to appear, then Enter */
    if (!WaitForOcr(e, L"\x8D2D\x4E70\x4E0E\x51FA\x552E", 40000, NULL)) {
        Notify(e, FARM_ERROR, L"未找到'购买与出售'");
        return 1;
    }
    if (!PressKey(e, VK_RETURN, 1000)) return 0;

    /* Step 5: PageDown + Enter (我的车辆) */
    if (!PressKey(e, VK_NEXT, 1000)) return 0;
    if (!PressKey(e, VK_RETURN, 2000)) return 0;

    /* Step 6: Set favorite car: Y + Enter + ESC */
    if (!PressKey(e, 'Y', 1000)) return 0;
    if (!PressKey(e, VK_RETURN, 800)) return 0;
    if (!PressKey(e, VK_ESCAPE, 1500)) return 0;

    /* Step 7: Enter (drive favorite car) */
    if (!PressKey(e, VK_RETURN, 800)) return 0;

    /* Step 8: Wait for car to load, try Enter to get in */
    if (!WaitMs(e, 2000)) return 0;
    if (!PressKey(e, VK_RETURN, 2000)) return 0;

    /* Step 9: Wait to return to buy/sell screen */
    Notify(e, FARM_RUNNING, L"等待回到车辆界面...");
    BOOL found_buysell = FALSE;
    for (int retry = 0; retry < 60 && !found_buysell; retry++) {
        if (IsStopped(e)) return 0;
        if (OcrContainsText(e, L"\x8D2D\x4E70\x4E0E\x51FA\x552E", NULL)) {
            found_buysell = TRUE;
            break;
        }
        WaitMs(e, 1000);
    }
    if (!found_buysell) {
        Notify(e, FARM_ERROR, L"超时等待购买与出售界面");
        return 1;
    }
    if (!PressKey(e, VK_RETURN, 1500)) return 0;

    /* Step 10: Sort by recent: X → Down×6 → Enter */
    if (!PressKey(e, 'X', 500)) return 0;
    if (!PressKeyN(e, VK_DOWN, 6, 250)) return 0;
    if (!PressKey(e, VK_RETURN, 1200)) return 0;

    /* Step 11: Go to first item: Backspace → Enter */
    if (!PressKey(e, VK_BACK, 800)) return 0;
    if (!PressKey(e, VK_RETURN, 1500)) return 0;

    /* Step 12: Delete loop */
    Notify(e, FARM_RUNNING, L"开始删除车辆");
    while (e->count < e->config.target_count) {
        if (IsStopped(e)) break;
        CheckPause(e);

        /* Enter car detail */
        if (!PressKey(e, VK_RETURN, 1200)) break;

        /* Down×6 to "从车库移除" */
        if (!PressKeyN(e, VK_DOWN, 6, 200)) break;

        /* Enter to select "remove" */
        if (!PressKey(e, VK_RETURN, 500)) break;

        /* Down to "嗯" (confirm) */
        if (!PressKey(e, VK_DOWN, 300)) break;

        /* Enter to confirm */
        if (!PressKey(e, VK_RETURN, 800)) break;

        e->count++;
        Notify(e, FARM_RUNNING, NULL);
    }

    /* Step 13: Exit: ESC × 3 */
    PressKeyN(e, VK_ESCAPE, 3, 1000);

    Notify(e, FARM_DONE, L"移除车辆完成");
    return 0;
}

/* ─── Flow: Buy Car ──────────────────────────────────────────────── */

static DWORD WINAPI FlowBuyCar(LPVOID param) {
    FarmEngine *e = (FarmEngine *)param;
    Notify(e, FARM_RUNNING, L"批量买车: 开始导航");

    /* Step 1: Enter main menu */
    if (!EnterMenu(e, L"\x6536\x96C6\x7C3F")) {  /* 收集簿 */
        Notify(e, FARM_ERROR, L"无法进入主菜单");
        return 1;
    }

    /* Step 2: Enter 收集簿 (should be highlighted) */
    if (!PressKey(e, VK_RETURN, 1000)) return 0;

    /* Step 3: Wait for 探索/车辆收集 then Enter */
    if (!WaitForOcr(e, L"\x63A2\x7D22", 30000, NULL)) {  /* 探索 */
        Notify(e, FARM_ERROR, L"未找到'探索'");
        return 1;
    }
    if (!PressKey(e, VK_RETURN, 600)) return 0;

    /* Step 4: Wait for 车辆收集 then Enter */
    if (!WaitForOcr(e, L"\x8F66\x8F86\x6536\x96C6", 30000, NULL)) {  /* 车辆收集 */
        Notify(e, FARM_ERROR, L"未找到'车辆收集'");
        return 1;
    }
    if (!PressKey(e, VK_RETURN, 1000)) return 0;

    /* Step 5: Backspace to brand list */
    if (!PressKey(e, VK_BACK, 500)) return 0;

    /* Step 6: Navigate up to find consumable brand (up to 5 attempts) */
    /* TODO: Use OCR to verify brand name once calibrated */
    if (!PressKeyN(e, VK_UP, 3, 300)) return 0;
    if (!PressKey(e, VK_RETURN, 800)) return 0;

    /* Step 7: Down + Enter to select target car */
    if (!PressKey(e, VK_DOWN, 400)) return 0;
    if (!PressKey(e, VK_RETURN, 1000)) return 0;

    /* Step 8: Purchase loop */
    Notify(e, FARM_RUNNING, L"开始购买");
    while (e->count < e->config.target_count) {
        if (IsStopped(e)) break;
        CheckPause(e);

        /* Space → Down → Enter → Enter → Enter */
        if (!PressKey(e, VK_SPACE, 600)) break;
        if (!PressKey(e, VK_DOWN, 200)) break;
        if (!PressKey(e, VK_RETURN, 600)) break;
        if (!PressKey(e, VK_RETURN, 600)) break;
        if (!PressKey(e, VK_RETURN, 700)) break;

        e->count++;
        Notify(e, FARM_RUNNING, NULL);
    }

    /* Step 9: Exit: ESC × 5 */
    PressKeyN(e, VK_ESCAPE, 5, 800);

    Notify(e, FARM_DONE, L"批量买车完成");
    return 0;
}

/* ─── Flow: Super Wheelspin ──────────────────────────────────────── */

static DWORD WINAPI FlowWheelspin(LPVOID param) {
    FarmEngine *e = (FarmEngine *)param;
    Notify(e, FARM_RUNNING, L"超级抽奖: 开始导航");

    /* Step 1: Enter main menu */
    if (!EnterMenu(e, L"\x6536\x96C6\x7C3F")) {  /* 收集簿 */
        Notify(e, FARM_ERROR, L"无法进入主菜单");
        return 1;
    }

    /* Step 2: PageDown → Enter "购买新车与二手车" */
    if (!PressKey(e, VK_NEXT, 1000)) return 0;
    if (!PressKey(e, VK_RETURN, 5000)) return 0;
    Notify(e, FARM_RUNNING, L"等待加载...");

    /* Step 3: Wait for "购买与出售" → Enter */
    if (!WaitForOcr(e, L"\x8D2D\x4E70\x4E0E\x51FA\x552E", 60000, NULL)) {
        Notify(e, FARM_ERROR, L"未找到'购买与出售'");
        return 1;
    }
    if (!PressKey(e, VK_RETURN, 1000)) return 0;

    /* Step 4: PageDown → Enter (我的车辆) */
    if (!PressKey(e, VK_NEXT, 500)) return 0;
    if (!PressKey(e, VK_RETURN, 2000)) return 0;

    /* === Wheelspin loop === */
    while (e->count < e->config.target_count) {
        if (IsStopped(e)) break;
        CheckPause(e);

        Notify(e, FARM_RUNNING, L"寻找目标车辆...");

        /* Backspace to brand list, find consumable brand */
        if (!PressKey(e, VK_BACK, 1000)) break;
        if (!PressKeyN(e, VK_UP, 5, 250)) break;
        if (!PressKey(e, VK_RETURN, 1000)) break;

        /* TODO: Flip pages to find NEW car (Right×4 per page)
         * For now, just select first available car */
        if (!PressKey(e, VK_RETURN, 1000)) break;

        /* Enter car (get in) */
        if (!PressKey(e, VK_RETURN, 2000)) break;

        /* Find "升级与调校": ESC loop until found */
        BOOL found_ut = FALSE;
        for (int i = 0; i < 20 && !found_ut; i++) {
            if (IsStopped(e)) break;
            if (OcrContainsText(e, L"\x5347\x7EA7\x4E0E\x8C03\x6821", NULL)) {
                found_ut = TRUE;
                break;
            }
            PressKey(e, VK_ESCAPE, 500);
        }
        if (!found_ut) {
            Notify(e, FARM_ERROR, L"未找到'升级与调校'");
            break;
        }
        if (!PressKey(e, VK_RETURN, 500)) break;

        /* Wait for "车辆熟练度" → Enter */
        if (!WaitForOcr(e, L"\x8F66\x8F86\x719F\x7EC3\x5EA6", 20000, NULL)) {
            Notify(e, FARM_ERROR, L"未找到'车辆熟练度'");
            break;
        }
        if (!PressKey(e, VK_RETURN, 1500)) break;

        /* Enter to start skill tree */
        if (!WaitMs(e, 1000)) break;
        if (!PressKey(e, VK_RETURN, 1500)) break;

        /* Navigate skill path */
        for (int d = 0; d < e->config.skill_dir_count; d++) {
            if (IsStopped(e)) break;
            DWORD dir_vk = 0;
            if (wcscmp(e->config.skill_dirs[d], L"up") == 0) dir_vk = VK_UP;
            else if (wcscmp(e->config.skill_dirs[d], L"down") == 0) dir_vk = VK_DOWN;
            else if (wcscmp(e->config.skill_dirs[d], L"left") == 0) dir_vk = VK_LEFT;
            else if (wcscmp(e->config.skill_dirs[d], L"right") == 0) dir_vk = VK_RIGHT;

            if (dir_vk) {
                PressKey(e, dir_vk, 200);
                PressKey(e, VK_RETURN, 1200);
            }
        }

        /* Check for "技能点不足" (skill points exhausted) */
        if (OcrContainsText(e, L"\x6280\x80FD\x70B9", NULL)) {
            /* Might be "技能点不足", try to detect and exit */
            if (OcrContainsText(e, L"\x4E0D\x8DB3", NULL)) {
                PressKey(e, VK_RETURN, 800);
                PressKeyN(e, VK_ESCAPE, 3, 1000);
                Notify(e, FARM_DONE, L"技能点不足，提前结束");
                return 0;
            }
        }

        e->count++;
        Notify(e, FARM_RUNNING, NULL);

        /* Reset for next car: ESC×2 + Up */
        if (!PressKey(e, VK_ESCAPE, 1200)) break;
        if (!PressKey(e, VK_ESCAPE, 800)) break;
        if (!PressKey(e, VK_UP, 800)) break;
    }

    /* Exit */
    PressKeyN(e, VK_ESCAPE, 2, 1200);
    Notify(e, FARM_DONE, L"超级抽奖完成");
    return 0;
}

/* ─── Public API ─────────────────────────────────────────────────── */

FarmEngine* Farm_Create(void) {
    FarmEngine *e = (FarmEngine *)calloc(1, sizeof(FarmEngine));
    if (!e) return NULL;
    e->state = FARM_IDLE;
    return e;
}

void Farm_Destroy(FarmEngine *engine) {
    if (!engine) return;
    Farm_Shutdown(engine);
    free(engine);
}

BOOL Farm_Init(FarmEngine *engine, HWND game_hwnd, const FarmConfig *config) {
    if (!engine || !game_hwnd || !config) return FALSE;

    engine->game_hwnd = game_hwnd;
    engine->config = *config;

    /* Init screen capture */
    if (!ScreenCapture_Init()) {
        Logger_Log(LOG_ERROR, L"[Farm] ScreenCapture_Init failed");
        return FALSE;
    }
    if (!ScreenCapture_StartCapture(game_hwnd)) {
        Logger_Log(LOG_ERROR, L"[Farm] StartCapture failed");
        return FALSE;
    }

    /* Init OCR */
    const WCHAR *lang = config->ocr_lang[0] ? config->ocr_lang : L"zh-Hans-CN";
    if (!OcrEngine_Init(lang)) {
        Logger_Log(LOG_WARN, L"[Farm] OCR init with %s failed, trying profile", lang);
        if (!OcrEngine_Init(NULL)) {
            Logger_Log(LOG_ERROR, L"[Farm] OCR init failed completely");
            return FALSE;
        }
    }

    /* Init input backend */
    engine->input = HookBackend_Create();
    if (!engine->input || !engine->input->init(engine->input, game_hwnd)) {
        Logger_Log(LOG_ERROR, L"[Farm] Input backend init failed");
        return FALSE;
    }

    /* Create sync objects */
    engine->stop_event = CreateEvent(NULL, TRUE, FALSE, NULL);
    engine->pause_event = CreateEvent(NULL, TRUE, TRUE, NULL);  /* signaled = not paused */

    return TRUE;
}

void Farm_Shutdown(FarmEngine *engine) {
    if (!engine) return;
    Farm_Stop(engine);

    if (engine->input) {
        engine->input->shutdown(engine->input);
        engine->input->destroy(engine->input);
        engine->input = NULL;
    }

    OcrEngine_Shutdown();
    ScreenCapture_StopCapture();
    ScreenCapture_Shutdown();

    if (engine->stop_event) { CloseHandle(engine->stop_event); engine->stop_event = NULL; }
    if (engine->pause_event) { CloseHandle(engine->pause_event); engine->pause_event = NULL; }
}

void Farm_SetCallback(FarmEngine *engine, FarmProgressCallback cb, void *user_data) {
    if (!engine) return;
    engine->callback = cb;
    engine->cb_user_data = user_data;
}

static BOOL StartFlow(FarmEngine *engine, LPTHREAD_START_ROUTINE flow_fn) {
    if (!engine || engine->state == FARM_RUNNING) return FALSE;

    ResetEvent(engine->stop_event);
    SetEvent(engine->pause_event);
    engine->count = 0;
    engine->state = FARM_RUNNING;

    engine->thread = CreateThread(NULL, 0, flow_fn, engine, 0, NULL);
    return engine->thread != NULL;
}

BOOL Farm_StartBuyCar(FarmEngine *engine) {
    return StartFlow(engine, FlowBuyCar);
}

BOOL Farm_StartWheelspin(FarmEngine *engine) {
    return StartFlow(engine, FlowWheelspin);
}

BOOL Farm_StartRemoveCar(FarmEngine *engine) {
    if (!engine) return FALSE;
    if (engine->config.sell_mode == 2) {
        return StartFlow(engine, FlowRemoveCarRecent);
    }
    /* Mode 1: TODO - filter consumable + OCR-based removal */
    return StartFlow(engine, FlowRemoveCarRecent);
}

void Farm_Stop(FarmEngine *engine) {
    if (!engine || engine->state != FARM_RUNNING) return;

    SetEvent(engine->stop_event);
    SetEvent(engine->pause_event);  /* unblock if paused */

    if (engine->thread) {
        WaitForSingleObject(engine->thread, 10000);
        CloseHandle(engine->thread);
        engine->thread = NULL;
    }

    if (engine->input)
        engine->input->release_all(engine->input);

    engine->state = FARM_IDLE;
}

void Farm_Pause(FarmEngine *engine) {
    if (!engine || engine->state != FARM_RUNNING) return;
    ResetEvent(engine->pause_event);
    engine->state = FARM_PAUSED;
}

void Farm_Resume(FarmEngine *engine) {
    if (!engine || engine->state != FARM_PAUSED) return;
    SetEvent(engine->pause_event);
    engine->state = FARM_RUNNING;
}

FarmState Farm_GetState(FarmEngine *engine) {
    return engine ? engine->state : FARM_IDLE;
}

int Farm_GetCount(FarmEngine *engine) {
    return engine ? engine->count : 0;
}
