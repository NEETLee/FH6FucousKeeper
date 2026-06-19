/*
 * game_input.c - Background input via PostMessage (hover + keyboard)
 *
 * Implements the "golden combo" verified by test_click_poc:
 *   - WM_MOUSEMOVE positions the highlight (hover)
 *   - WM_KEYDOWN/UP Enter confirms (click)
 *   - All via PostMessage -> works in background
 */

#include "game_input.h"
#include <stdlib.h>

struct GameInput {
    HWND    hwnd;
    BYTE    key_state[256];
    BOOL    ready;
    int     off_x;   /* client-area origin relative to window top-left */
    int     off_y;   /* (WGC frame == full window, PostMessage uses client) */
};

/* Convert window/frame coords (what template matching returns) to client
 * coords (what WM_MOUSE* messages expect). */
static void ToClient(GameInput *gi, int *x, int *y) {
    /* Recompute each call: window may have moved. */
    RECT wr; POINT o = {0, 0};
    if (GetWindowRect(gi->hwnd, &wr) && ClientToScreen(gi->hwnd, &o)) {
        gi->off_x = (int)(o.x - wr.left);
        gi->off_y = (int)(o.y - wr.top);
    }
    *x -= gi->off_x;
    *y -= gi->off_y;
}

/* ─── Helpers ────────────────────────────────────────────────────────── */

static BOOL IsExtendedKey(DWORD vk) {
    switch (vk) {
    case VK_UP: case VK_DOWN: case VK_LEFT: case VK_RIGHT:
    case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END:
    case VK_PRIOR: case VK_NEXT:
    case VK_RCONTROL: case VK_RMENU:
    case VK_NUMLOCK: case VK_DIVIDE:
        return TRUE;
    default:
        return FALSE;
    }
}

static LPARAM MakeKeyLParam(DWORD vk, BOOL up, BOOL repeat) {
    UINT sc = MapVirtualKey(vk, MAPVK_VK_TO_VSC);
    LPARAM ext = IsExtendedKey(vk) ? (1 << 24) : 0;
    LPARAM lp = 1 | (sc << 16) | ext;
    if (repeat) lp |= (1 << 30);
    if (up)     lp |= (1 << 30) | (1 << 31);
    return lp;
}

/* ─── Public API ─────────────────────────────────────────────────────── */

GameInput* GameInput_Create(void) {
    GameInput *gi = (GameInput*)calloc(1, sizeof(GameInput));
    return gi;
}

void GameInput_Destroy(GameInput *gi) {
    if (!gi) return;
    GameInput_Shutdown(gi);
    free(gi);
}

BOOL GameInput_Init(GameInput *gi, HWND game_hwnd) {
    if (!gi || !IsWindow(game_hwnd)) return FALSE;
    if (gi->ready && gi->hwnd != game_hwnd)
        GameInput_ReleaseAll(gi);
    gi->hwnd = game_hwnd;
    ZeroMemory(gi->key_state, sizeof(gi->key_state));
    gi->ready = TRUE;
    return TRUE;
}

void GameInput_Shutdown(GameInput *gi) {
    if (!gi || !gi->ready) return;
    GameInput_ReleaseAll(gi);
    gi->ready = FALSE;
}

BOOL GameInput_IsReady(GameInput *gi) {
    return gi && gi->ready && IsWindow(gi->hwnd);
}

HWND GameInput_GetHwnd(GameInput *gi) {
    return gi ? gi->hwnd : NULL;
}

void GameInput_Hover(GameInput *gi, int x, int y) {
    if (!gi || !gi->ready) return;
    ToClient(gi, &x, &y);
    PostMessageW(gi->hwnd, WM_MOUSEMOVE, 0, MAKELPARAM((WORD)x, (WORD)y));
}

void GameInput_Click(GameInput *gi, int x, int y) {
    GameInput_ClickKey(gi, x, y, VK_RETURN);
}

void GameInput_ClickKey(GameInput *gi, int x, int y, DWORD confirm_vk) {
    if (!gi || !gi->ready) return;
    ToClient(gi, &x, &y);
    PostMessageW(gi->hwnd, WM_MOUSEMOVE, 0, MAKELPARAM((WORD)x, (WORD)y));
    Sleep(120);
    PostMessageW(gi->hwnd, WM_KEYDOWN, confirm_vk, MakeKeyLParam(confirm_vk, FALSE, FALSE));
    Sleep(80);
    PostMessageW(gi->hwnd, WM_KEYUP, confirm_vk, MakeKeyLParam(confirm_vk, TRUE, FALSE));
}

void GameInput_Press(GameInput *gi, DWORD vk, int delay_ms) {
    if (!gi || !gi->ready) return;
    if (delay_ms <= 0) delay_ms = 80;
    PostMessageW(gi->hwnd, WM_KEYDOWN, vk, MakeKeyLParam(vk, FALSE, gi->key_state[vk & 0xFF]));
    gi->key_state[vk & 0xFF] = 1;
    Sleep(delay_ms);
    PostMessageW(gi->hwnd, WM_KEYUP, vk, MakeKeyLParam(vk, TRUE, FALSE));
    gi->key_state[vk & 0xFF] = 0;
}

void GameInput_KeyDown(GameInput *gi, DWORD vk) {
    if (!gi || !gi->ready) return;
    BOOL repeat = gi->key_state[vk & 0xFF];
    PostMessageW(gi->hwnd, WM_KEYDOWN, vk, MakeKeyLParam(vk, FALSE, repeat));
    gi->key_state[vk & 0xFF] = 1;
}

void GameInput_KeyUp(GameInput *gi, DWORD vk) {
    if (!gi || !gi->ready) return;
    PostMessageW(gi->hwnd, WM_KEYUP, vk, MakeKeyLParam(vk, TRUE, FALSE));
    gi->key_state[vk & 0xFF] = 0;
}

void GameInput_ReleaseAll(GameInput *gi) {
    if (!gi || !gi->ready) return;
    for (int vk = 0; vk < 256; vk++) {
        if (gi->key_state[vk]) {
            PostMessageW(gi->hwnd, WM_KEYUP, (WPARAM)vk, MakeKeyLParam((DWORD)vk, TRUE, FALSE));
            gi->key_state[vk] = 0;
        }
    }
}

void GameInput_MoveAway(GameInput *gi) {
    if (!gi || !gi->ready) return;
    PostMessageW(gi->hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(5, 5));
}

void GameInput_MouseClick(GameInput *gi, int x, int y) {
    if (!gi || !gi->ready) return;
    ToClient(gi, &x, &y);
    LPARAM pos = MAKELPARAM((WORD)x, (WORD)y);
    PostMessageW(gi->hwnd, WM_MOUSEMOVE, 0, pos);
    Sleep(200);
    PostMessageW(gi->hwnd, WM_LBUTTONDOWN, MK_LBUTTON, pos);
    Sleep(100);
    PostMessageW(gi->hwnd, WM_LBUTTONUP, 0, pos);
    Sleep(100);
    PostMessageW(gi->hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(5, 5));
}

void GameInput_MouseDoubleClick(GameInput *gi, int x, int y) {
    if (!gi || !gi->ready) return;
    ToClient(gi, &x, &y);
    LPARAM pos = MAKELPARAM((WORD)x, (WORD)y);
    PostMessageW(gi->hwnd, WM_MOUSEMOVE, 0, pos);
    Sleep(200);
    for (int i = 0; i < 2; i++) {
        PostMessageW(gi->hwnd, WM_LBUTTONDOWN, MK_LBUTTON, pos);
        Sleep(100);
        PostMessageW(gi->hwnd, WM_LBUTTONUP, 0, pos);
        Sleep(100);
    }
    Sleep(100);
    PostMessageW(gi->hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(5, 5));
}
