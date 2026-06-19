/*
 * input_hook_backend.c - InputBackend implementation via PostMessage
 *
 * Injects keyboard input into the game process by posting WM_KEYDOWN
 * and WM_KEYUP messages directly to the game window. This approach
 * does NOT modify game memory (no IAT hooking) and is safe from
 * anti-cheat detection.
 *
 * For "hold" behavior, the StepExecutor calls key_down periodically
 * to simulate key repeat via repeated WM_KEYDOWN messages.
 */

#include "input_hook_backend.h"
#include "hook/hook.h"
#include <stdlib.h>

typedef struct {
    InputBackend    base;
    HWND            game_hwnd;
    BYTE            key_state[256];
    BOOL            ready;
} HookBackendImpl;

/* ─── vtable implementations ─────────────────────────────────────── */

static BOOL HookBackend_Init(InputBackend *self, HWND game_hwnd)
{
    HookBackendImpl *impl = (HookBackendImpl *)self;
    if (impl->ready) return TRUE;
    if (!IsWindow(game_hwnd)) return FALSE;

    impl->game_hwnd = game_hwnd;
    ZeroMemory(impl->key_state, sizeof(impl->key_state));
    impl->ready = TRUE;
    return TRUE;
}

static void HookBackend_Shutdown(InputBackend *self)
{
    HookBackendImpl *impl = (HookBackendImpl *)self;
    if (!impl->ready) return;

    /* Release any held keys */
    for (int vk = 0; vk < 256; vk++) {
        if (impl->key_state[vk]) {
            UINT sc = MapVirtualKey(vk, MAPVK_VK_TO_VSC);
            LPARAM lp = 1 | (sc << 16) | (1 << 30) | (1 << 31);
            PostMessage(impl->game_hwnd, WM_KEYUP, vk, lp);
        }
    }
    ZeroMemory(impl->key_state, sizeof(impl->key_state));
    impl->ready = FALSE;
}

static BOOL IsExtendedKey(DWORD vk)
{
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

static void HookBackend_KeyDown(InputBackend *self, DWORD vk)
{
    HookBackendImpl *impl = (HookBackendImpl *)self;
    if (!impl->ready || !IsWindow(impl->game_hwnd)) return;

    UINT sc = MapVirtualKey(vk, MAPVK_VK_TO_VSC);
    LPARAM lParam;
    LPARAM extended = IsExtendedKey(vk) ? (1 << 24) : 0;

    if (impl->key_state[vk & 0xFF]) {
        lParam = 1 | (sc << 16) | extended | (1 << 30);
    } else {
        lParam = 1 | (sc << 16) | extended;
        impl->key_state[vk & 0xFF] = 1;
    }

    PostMessage(impl->game_hwnd, WM_KEYDOWN, vk, lParam);
}

static void HookBackend_KeyUp(InputBackend *self, DWORD vk)
{
    HookBackendImpl *impl = (HookBackendImpl *)self;
    if (!impl->ready || !IsWindow(impl->game_hwnd)) return;

    UINT sc = MapVirtualKey(vk, MAPVK_VK_TO_VSC);
    LPARAM extended = IsExtendedKey(vk) ? (1 << 24) : 0;
    LPARAM lParam = 1 | (sc << 16) | extended | (1 << 30) | (1 << 31);

    PostMessage(impl->game_hwnd, WM_KEYUP, vk, lParam);
    impl->key_state[vk & 0xFF] = 0;
}

static void HookBackend_ReleaseAll(InputBackend *self)
{
    HookBackendImpl *impl = (HookBackendImpl *)self;
    if (!impl->ready) return;

    for (int vk = 0; vk < 256; vk++) {
        if (impl->key_state[vk]) {
            UINT sc = MapVirtualKey(vk, MAPVK_VK_TO_VSC);
            LPARAM ext = IsExtendedKey((DWORD)vk) ? (1 << 24) : 0;
            LPARAM lp = 1 | (sc << 16) | ext | (1 << 30) | (1 << 31);
            PostMessage(impl->game_hwnd, WM_KEYUP, vk, lp);
            impl->key_state[vk] = 0;
        }
    }
}

static BOOL HookBackend_IsReady(InputBackend *self)
{
    HookBackendImpl *impl = (HookBackendImpl *)self;
    return impl->ready && IsWindow(impl->game_hwnd);
}

static void HookBackend_Destroy(InputBackend *self)
{
    HookBackendImpl *impl = (HookBackendImpl *)self;
    HookBackend_Shutdown(self);
    free(impl);
}

/* ─── Factory ────────────────────────────────────────────────────── */

InputBackend* HookBackend_Create(void)
{
    HookBackendImpl *impl = (HookBackendImpl *)calloc(1, sizeof(HookBackendImpl));
    if (!impl) return NULL;

    impl->base.init        = HookBackend_Init;
    impl->base.shutdown    = HookBackend_Shutdown;
    impl->base.key_down    = HookBackend_KeyDown;
    impl->base.key_up      = HookBackend_KeyUp;
    impl->base.release_all = HookBackend_ReleaseAll;
    impl->base.is_ready    = HookBackend_IsReady;
    impl->base.destroy     = HookBackend_Destroy;

    return &impl->base;
}
