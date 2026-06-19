/*
 * farm_recovery.c - Error recovery implementation
 *
 * Scans for obstacle popups, attempts to return to known game states,
 * and handles VRAM warnings.
 */

#include "farm_recovery.h"
#include "game_input.h"
#include "template_match.h"
#include "screen_capture.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>

/* ─── Helpers ────────────────────────────────────────────────────────── */

static BOOL grab_frame(void) {
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

/* ─── Obstacle scanning ──────────────────────────────────────────────── */

BOOL Recovery_ScanObstacles(HWND game_hwnd, const char *obstacles_dir) {
    if (!obstacles_dir) return FALSE;

    WIN32_FIND_DATAA fd;
    char pattern[512];
    snprintf(pattern, sizeof(pattern), "%s\\*.png", obstacles_dir);
    HANDLE hf = FindFirstFileA(pattern, &fd);
    if (hf == INVALID_HANDLE_VALUE) return FALSE;

    GameInput *gi = GameInput_Create();
    GameInput_Init(gi, game_hwnd);
    BOOL handled = FALSE;

    do {
        char path[512];
        snprintf(path, sizeof(path), "%s\\%s", obstacles_dir, fd.cFileName);

        grab_frame();
        TMResult r = TM_FindImageGray(path, 0.65, FALSE);
        if (!r.found) r = TM_FindImageTransparent(path, 0.65);

        if (r.found) {
            GameInput_Click(gi, r.cx, r.cy);
            Sleep(800);
            handled = TRUE;
            break;
        }
    } while (FindNextFileA(hf, &fd));

    FindClose(hf);
    GameInput_Destroy(gi);
    return handled;
}

/* ─── Advanced enter menu ────────────────────────────────────────────── */

BOOL Recovery_AdvancedEnterMenu(HWND game_hwnd, const char *assets_dir,
                                const char *obstacles_dir) {
    GameInput *gi = GameInput_Create();
    GameInput_Init(gi, game_hwnd);
    char tmpl_path[512];
    snprintf(tmpl_path, sizeof(tmpl_path), "%s/collectionjournal.png", assets_dir);

    for (int i = 0; i < 80; i++) {
        grab_frame();

        /* Check if we're in menu */
        TMResult r = TM_FindImageGray(tmpl_path, 0.60, FALSE);
        if (!r.found) r = TM_FindImageTransparent(tmpl_path, 0.60);
        if (r.found) {
            GameInput_Destroy(gi);
            return TRUE;
        }

        /* Scan obstacles first */
        if (obstacles_dir) {
            if (Recovery_ScanObstacles(game_hwnd, obstacles_dir)) {
                Sleep(1000);
                continue;
            }
        }

        /* Check VRAM warning */
        if (Recovery_CheckVRAM()) {
            GameInput_Press(gi, VK_RETURN, 80);
            Sleep(1000);
            continue;
        }

        /* Default: ESC */
        GameInput_Press(gi, VK_ESCAPE, 80);
        Sleep(1000);
    }

    GameInput_Destroy(gi);
    return FALSE;
}

/* ─── Attempt recovery ───────────────────────────────────────────────── */

BOOL Recovery_AttemptRecovery(HWND game_hwnd, const char *assets_dir,
                              const char *obstacles_dir) {
    /* First try the advanced enter menu */
    if (Recovery_AdvancedEnterMenu(game_hwnd, assets_dir, obstacles_dir))
        return TRUE;

    /* If that fails, try rapid ESC spam */
    GameInput *gi = GameInput_Create();
    GameInput_Init(gi, game_hwnd);
    for (int i = 0; i < 10; i++) {
        GameInput_Press(gi, VK_ESCAPE, 80);
        Sleep(500);
    }
    GameInput_Destroy(gi);

    /* Check one more time */
    return Recovery_AdvancedEnterMenu(game_hwnd, assets_dir, obstacles_dir);
}

/* ─── VRAM check ─────────────────────────────────────────────────────── */

BOOL Recovery_CheckVRAM(void) {
    grab_frame();
    TMResult r = TM_FindImageGray("assets/templates/VRAMNE.png", 0.65, FALSE);
    return r.found;
}

/* ─── Restart game ───────────────────────────────────────────────────── */

BOOL Recovery_RestartGame(const char *restart_cmd) {
    if (!restart_cmd || !restart_cmd[0]) return FALSE;

    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    char cmd[512];
    strncpy(cmd, restart_cmd, sizeof(cmd) - 1);

    BOOL ok = CreateProcessA(NULL, cmd, NULL, NULL, FALSE,
                             CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    if (ok) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    return ok;
}
