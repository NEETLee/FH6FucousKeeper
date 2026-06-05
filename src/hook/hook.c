/*
 * hook.c - FH6 FocusKeeper Hook DLL
 *
 * This DLL is injected into the game process via SetWindowsHookEx.
 * It subclasses the game window to intercept and neutralize focus-loss
 * messages, preventing the game from detecting window deactivation.
 *
 * Design Pattern: Chain of Responsibility (message filtering)
 * The SubclassProc acts as a filter chain, each message type is handled
 * by its own case, either modified or swallowed before reaching the
 * original window procedure.
 */

#ifndef HOOK_EXPORTS
#define HOOK_EXPORTS
#endif
#include "hook.h"

/* ─── Shared Data Segment ─────────────────────────────────────────────
 * This segment is shared between all instances of the DLL (loader process
 * and the injected copy in the game process). Allows cross-process
 * communication of state and statistics.
 *
 * For GCC/MinGW: use __attribute__((section, shared)) + .def file
 */
#define SHARED __attribute__((section("FKShared"), shared))

static HHOOK  s_hook_handle  SHARED = NULL;
static HWND   s_target_hwnd  SHARED = NULL;
static BOOL   s_active       SHARED = FALSE;
static DWORD  s_target_tid   SHARED = 0;
static DWORD  s_target_pid   SHARED = 0;

static volatile LONG s_stat_killfocus   SHARED = 0;
static volatile LONG s_stat_activateapp SHARED = 0;
static volatile LONG s_stat_ncactivate  SHARED = 0;
static volatile LONG s_stat_activate    SHARED = 0;

/* ─── Per-Instance Data ───────────────────────────────────────────── */
static HINSTANCE s_dll_instance = NULL;
static WNDPROC   s_orig_wndproc = NULL;
static BOOL      s_subclassed   = FALSE;

/* ─── Subclass Window Procedure ───────────────────────────────────────
 * Core filtering logic. Runs inside the game process.
 * Intercepts deactivation messages and either neutralizes or modifies them.
 */
static LRESULT CALLBACK SubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_ACTIVATEAPP:
        if (wParam == FALSE) {
            InterlockedIncrement(&s_stat_activateapp);
            wParam = TRUE;
        }
        break;

    case WM_KILLFOCUS:
        InterlockedIncrement(&s_stat_killfocus);
        return 0;

    case WM_NCACTIVATE:
        if (wParam == FALSE) {
            InterlockedIncrement(&s_stat_ncactivate);
            wParam = TRUE;
        }
        break;

    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE) {
            InterlockedIncrement(&s_stat_activate);
            wParam = MAKEWPARAM(WA_ACTIVE, HIWORD(wParam));
        }
        break;

    case WM_HOOK_SUBCLASS:
        if (!s_subclassed && hwnd == s_target_hwnd) {
            s_orig_wndproc = (WNDPROC)SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)SubclassProc);
            if (s_orig_wndproc) {
                s_subclassed = TRUE;
            }
        }
        return 0;

    case WM_HOOK_UNSUBCLASS:
        if (s_subclassed && s_orig_wndproc) {
            SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)s_orig_wndproc);
            s_subclassed = FALSE;
            s_orig_wndproc = NULL;
        }
        return 0;
    }

    if (s_orig_wndproc) {
        return CallWindowProc(s_orig_wndproc, hwnd, msg, wParam, lParam);
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

/* ─── Hook Callback ───────────────────────────────────────────────────
 * WH_CALLWNDPROC hook. Used primarily to get the DLL loaded into the
 * game process. The actual filtering is done by the subclass procedure.
 */
static LRESULT CALLBACK HookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode >= 0) {
        CWPSTRUCT *cwp = (CWPSTRUCT *)lParam;
        if (!s_subclassed && cwp->hwnd == s_target_hwnd) {
            s_orig_wndproc = (WNDPROC)SetWindowLongPtr(
                cwp->hwnd, GWLP_WNDPROC, (LONG_PTR)SubclassProc);
            if (s_orig_wndproc) {
                s_subclassed = TRUE;
            }
        }
    }
    return CallNextHookEx(s_hook_handle, nCode, wParam, lParam);
}

/* ─── DLL Entry Point ─────────────────────────────────────────────── */
BOOL APIENTRY DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    (void)lpvReserved;

    switch (fdwReason) {
    case DLL_PROCESS_ATTACH:
        s_dll_instance = hinstDLL;
        DisableThreadLibraryCalls(hinstDLL);
        break;

    case DLL_PROCESS_DETACH:
        if (s_subclassed && s_orig_wndproc && s_target_hwnd) {
            SetWindowLongPtr(s_target_hwnd, GWLP_WNDPROC, (LONG_PTR)s_orig_wndproc);
            s_subclassed = FALSE;
        }
        break;
    }
    return TRUE;
}

/* ─── Exported API ────────────────────────────────────────────────── */

HOOK_API BOOL Hook_Install(HWND target_hwnd)
{
    if (s_active) return FALSE;
    if (!IsWindow(target_hwnd)) return FALSE;

    s_target_hwnd = target_hwnd;
    s_target_tid = GetWindowThreadProcessId(target_hwnd, &s_target_pid);

    if (s_target_tid == 0) return FALSE;

    s_hook_handle = SetWindowsHookEx(
        WH_CALLWNDPROC,
        HookProc,
        s_dll_instance,
        s_target_tid
    );

    if (!s_hook_handle) return FALSE;

    s_active = TRUE;

    SendMessage(target_hwnd, WM_NULL, 0, 0);

    return TRUE;
}

HOOK_API void Hook_Uninstall(void)
{
    if (!s_active) return;

    if (s_subclassed && s_target_hwnd) {
        SendMessage(s_target_hwnd, WM_HOOK_UNSUBCLASS, 0, 0);
    }

    if (s_hook_handle) {
        UnhookWindowsHookEx(s_hook_handle);
        s_hook_handle = NULL;
    }

    s_active = FALSE;
    s_target_hwnd = NULL;
    s_target_tid = 0;
    s_target_pid = 0;
}

HOOK_API BOOL Hook_IsActive(void)
{
    return s_active;
}

HOOK_API void Hook_GetStats(HookStats *out)
{
    if (!out) return;
    out->killfocus_count   = InterlockedCompareExchange(&s_stat_killfocus, 0, 0);
    out->activateapp_count = InterlockedCompareExchange(&s_stat_activateapp, 0, 0);
    out->ncactivate_count  = InterlockedCompareExchange(&s_stat_ncactivate, 0, 0);
    out->activate_count    = InterlockedCompareExchange(&s_stat_activate, 0, 0);
}

HOOK_API void Hook_ResetStats(void)
{
    InterlockedExchange(&s_stat_killfocus, 0);
    InterlockedExchange(&s_stat_activateapp, 0);
    InterlockedExchange(&s_stat_ncactivate, 0);
    InterlockedExchange(&s_stat_activate, 0);
}

HOOK_API HWND Hook_GetTarget(void)
{
    return s_target_hwnd;
}
