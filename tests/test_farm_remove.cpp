/*
 * test_farm_remove.cpp - Test farm navigation: remove car (mode 2)
 *
 * Tests the simplest flow: sort by recent + keyboard-only deletion.
 * Usage: test_farm_remove.exe [count]
 */

#include <windows.h>
#include <stdio.h>

extern "C" {
#include "farm_nav.h"
}

static void ProgressCallback(FarmState state, int current, int target,
                              const WCHAR *msg, void *user_data) {
    (void)user_data;
    const WCHAR *state_str = L"?";
    switch (state) {
        case FARM_IDLE:    state_str = L"IDLE"; break;
        case FARM_RUNNING: state_str = L"RUN"; break;
        case FARM_PAUSED:  state_str = L"PAUSE"; break;
        case FARM_DONE:    state_str = L"DONE"; break;
        case FARM_ERROR:   state_str = L"ERROR"; break;
    }
    wprintf(L"[%ls] %d/%d", state_str, current, target);
    if (msg) wprintf(L" - %ls", msg);
    wprintf(L"\n");
}

int wmain(int argc, wchar_t *argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);
    wprintf(L"=== Farm Remove Car Test ===\n");

    int target_count = 3;
    if (argc > 1) target_count = _wtoi(argv[1]);
    wprintf(L"Target: remove %d cars\n", target_count);

    /* Find game window */
    HWND hwnd = FindWindowW(NULL, L"Forza Horizon 6");
    if (!hwnd) {
        wprintf(L"ERROR: Forza Horizon 6 window not found\n");
        return 1;
    }
    wprintf(L"Game window: %p\n", (void*)hwnd);

    /* Create and init farm engine */
    FarmEngine *engine = Farm_Create();
    if (!engine) { wprintf(L"ERROR: Farm_Create failed\n"); return 1; }

    FarmConfig config = {};
    config.target_count = target_count;
    config.sell_mode = 2;
    wcscpy(config.ocr_lang, L"zh-Hans-CN");

    wprintf(L"Initializing (WGC + OCR + Input)...\n");
    if (!Farm_Init(engine, hwnd, &config)) {
        wprintf(L"ERROR: Farm_Init failed\n");
        Farm_Destroy(engine);
        return 1;
    }
    wprintf(L"Init OK\n");

    Farm_SetCallback(engine, ProgressCallback, NULL);

    /* Start flow */
    wprintf(L"\nStarting remove car flow in 3 seconds...\n");
    wprintf(L"(Press Ctrl+C to abort)\n");
    Sleep(3000);

    if (!Farm_StartRemoveCar(engine)) {
        wprintf(L"ERROR: Farm_StartRemoveCar failed\n");
        Farm_Destroy(engine);
        return 1;
    }

    /* Wait for completion */
    while (Farm_GetState(engine) == FARM_RUNNING ||
           Farm_GetState(engine) == FARM_PAUSED) {
        Sleep(500);
    }

    wprintf(L"\nFinal state: %d, removed: %d cars\n",
            Farm_GetState(engine), Farm_GetCount(engine));

    Farm_Destroy(engine);
    wprintf(L"Done.\n");
    return 0;
}
