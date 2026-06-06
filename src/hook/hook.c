/*
 * hook.c - FH6 FocusKeeper Hook DLL
 *
 * This DLL is injected into the game process via SetWindowsHookEx.
 * It subclasses the game window to intercept and neutralize focus-loss
 * messages, preventing the game from detecting window deactivation.
 *
 * Additionally, it provides virtual input injection by hooking
 * GetAsyncKeyState/GetKeyboardState in the game process's IAT,
 * allowing the loader to simulate key presses via shared memory.
 */

#ifndef HOOK_EXPORTS
#define HOOK_EXPORTS
#endif
#include "hook.h"

/* ─── Shared Data Segment ─────────────────────────────────────────────
 * This segment is shared between all instances of the DLL (loader process
 * and the injected copy in the game process). Allows cross-process
 * communication of state and statistics.
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

/* Virtual key bitmap: 256 keys, 1 bit each */
static volatile BYTE s_virtual_keys[VK_BITMAP_SIZE] SHARED = {0};
static volatile BOOL s_input_hooked SHARED = FALSE;

/* ─── Per-Instance Data ───────────────────────────────────────────── */
static HINSTANCE s_dll_instance = NULL;
static WNDPROC   s_orig_wndproc = NULL;
static BOOL      s_subclassed   = FALSE;

/* IAT Hook state (per-process, not shared) */
static SHORT (WINAPI *s_orig_GetAsyncKeyState)(int vKey) = NULL;
static BOOL  (WINAPI *s_orig_GetKeyboardState)(PBYTE lpKeyState) = NULL;
static SHORT (WINAPI *s_orig_GetKeyState)(int nVirtKey) = NULL;
static BOOL  s_iat_installed = FALSE;

/* ─── Virtual Key Helpers ────────────────────────────────────────── */

static inline BOOL IsVKeyDown(int vk)
{
    if (vk < 0 || vk > 255) return FALSE;
    return (s_virtual_keys[vk >> 3] & (1 << (vk & 7))) != 0;
}

/* ─── IAT Hook Implementation ────────────────────────────────────── */

static SHORT WINAPI Hooked_GetAsyncKeyState(int vKey)
{
    if (IsVKeyDown(vKey)) {
        return (SHORT)0x8001;
    }
    return s_orig_GetAsyncKeyState(vKey);
}

static SHORT WINAPI Hooked_GetKeyState(int nVirtKey)
{
    if (IsVKeyDown(nVirtKey)) {
        return (SHORT)0xFF81;
    }
    return s_orig_GetKeyState(nVirtKey);
}

static BOOL WINAPI Hooked_GetKeyboardState(PBYTE lpKeyState)
{
    BOOL result = s_orig_GetKeyboardState(lpKeyState);
    if (result && lpKeyState) {
        for (int vk = 0; vk < 256; vk++) {
            if (IsVKeyDown(vk)) {
                lpKeyState[vk] = 0x80;
            }
        }
    }
    return result;
}

/*
 * Patch a single IAT entry. Walks the import descriptor table of the
 * given module and replaces the target function pointer.
 */
static BOOL PatchIAT(HMODULE hModule, const char *targetDll,
                     const void *origFunc, const void *newFunc)
{
    if (!hModule || !origFunc || !newFunc) return FALSE;

    BYTE *base = (BYTE *)hModule;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return FALSE;

    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return FALSE;

    IMAGE_DATA_DIRECTORY *importDir =
        &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (importDir->VirtualAddress == 0) return FALSE;

    IMAGE_IMPORT_DESCRIPTOR *desc =
        (IMAGE_IMPORT_DESCRIPTOR *)(base + importDir->VirtualAddress);

    for (; desc->Name != 0; desc++) {
        const char *dllName = (const char *)(base + desc->Name);
        if (targetDll && _stricmp(dllName, targetDll) != 0) continue;

        IMAGE_THUNK_DATA *thunk = (IMAGE_THUNK_DATA *)(base + desc->FirstThunk);
        for (; thunk->u1.Function != 0; thunk++) {
            if ((void *)(uintptr_t)thunk->u1.Function == origFunc) {
                DWORD oldProtect;
                if (VirtualProtect(&thunk->u1.Function, sizeof(void *),
                                   PAGE_READWRITE, &oldProtect)) {
                    thunk->u1.Function = (uintptr_t)newFunc;
                    VirtualProtect(&thunk->u1.Function, sizeof(void *),
                                   oldProtect, &oldProtect);
                    return TRUE;
                }
            }
        }
    }
    return FALSE;
}

static void InstallIATHooks(void)
{
    if (s_iat_installed) return;

    HMODULE hGame = GetModuleHandle(NULL);
    HMODULE hUser32 = GetModuleHandleA("user32.dll");
    if (!hGame || !hUser32) return;

    s_orig_GetAsyncKeyState = (SHORT (WINAPI *)(int))
        GetProcAddress(hUser32, "GetAsyncKeyState");
    s_orig_GetKeyState = (SHORT (WINAPI *)(int))
        GetProcAddress(hUser32, "GetKeyState");
    s_orig_GetKeyboardState = (BOOL (WINAPI *)(PBYTE))
        GetProcAddress(hUser32, "GetKeyboardState");

    if (s_orig_GetAsyncKeyState) {
        PatchIAT(hGame, "user32.dll",
                 s_orig_GetAsyncKeyState, Hooked_GetAsyncKeyState);
    }
    if (s_orig_GetKeyState) {
        PatchIAT(hGame, "user32.dll",
                 s_orig_GetKeyState, Hooked_GetKeyState);
    }
    if (s_orig_GetKeyboardState) {
        PatchIAT(hGame, "user32.dll",
                 s_orig_GetKeyboardState, Hooked_GetKeyboardState);
    }

    s_iat_installed = TRUE;
    s_input_hooked = TRUE;
}

static void UninstallIATHooks(void)
{
    if (!s_iat_installed) return;

    HMODULE hGame = GetModuleHandle(NULL);
    if (!hGame) return;

    if (s_orig_GetAsyncKeyState) {
        PatchIAT(hGame, "user32.dll",
                 Hooked_GetAsyncKeyState, s_orig_GetAsyncKeyState);
    }
    if (s_orig_GetKeyState) {
        PatchIAT(hGame, "user32.dll",
                 Hooked_GetKeyState, s_orig_GetKeyState);
    }
    if (s_orig_GetKeyboardState) {
        PatchIAT(hGame, "user32.dll",
                 Hooked_GetKeyboardState, s_orig_GetKeyboardState);
    }

    s_iat_installed = FALSE;
    s_input_hooked = FALSE;
}

/* ─── Subclass Window Procedure ───────────────────────────────────────
 * Core filtering logic. Runs inside the game process.
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
            UninstallIATHooks();
            SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)s_orig_wndproc);
            s_subclassed = FALSE;
            s_orig_wndproc = NULL;
        }
        return 0;

    case WM_HOOK_ENABLE_INPUT:
        InstallIATHooks();
        return s_iat_installed ? 1 : 0;

    case WM_HOOK_DISABLE_INPUT:
        UninstallIATHooks();
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
        if (s_iat_installed) {
            UninstallIATHooks();
        }
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

    HWND target = s_target_hwnd;

    s_target_hwnd = NULL;
    s_active = FALSE;

    /* Clear all virtual keys before unhooking */
    memset((void *)s_virtual_keys, 0, VK_BITMAP_SIZE);

    if (target && IsWindow(target)) {
        SendMessage(target, WM_HOOK_UNSUBCLASS, 0, 0);
    }

    if (s_hook_handle) {
        UnhookWindowsHookEx(s_hook_handle);
        s_hook_handle = NULL;
    }

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

/* ─── Virtual Input API ──────────────────────────────────────────── */

HOOK_API void Hook_VKeyDown(int vk)
{
    if (vk >= 0 && vk <= 255) {
        s_virtual_keys[vk >> 3] |= (BYTE)(1 << (vk & 7));
    }
}

HOOK_API void Hook_VKeyUp(int vk)
{
    if (vk >= 0 && vk <= 255) {
        s_virtual_keys[vk >> 3] &= (BYTE)~(1 << (vk & 7));
    }
}

HOOK_API void Hook_VKeyReleaseAll(void)
{
    memset((void *)s_virtual_keys, 0, VK_BITMAP_SIZE);
}

HOOK_API BOOL Hook_VKeyIsDown(int vk)
{
    return IsVKeyDown(vk);
}

HOOK_API BOOL Hook_IsInputHooked(void)
{
    return s_input_hooked;
}

HOOK_API BOOL Hook_EnableInputHooks(void)
{
    InstallIATHooks();
    return s_iat_installed;
}

HOOK_API void Hook_DisableInputHooks(void)
{
    UninstallIATHooks();
}
