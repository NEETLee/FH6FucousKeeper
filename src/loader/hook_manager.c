/*
 * hook_manager.c - Hook Lifecycle Manager (Facade Pattern)
 *
 * Manages the complete lifecycle of the hook DLL:
 * - Dynamic loading of hook.dll
 * - Symbol resolution for exported functions
 * - Hook installation/removal
 * - Error handling and state management
 * - Observer notification on state changes
 */

#include "hook_manager.h"
#include "hook/hook.h"
#include <stdio.h>
#include <wchar.h>

/* ─── Function pointer types for DLL imports ──────────────────────── */
typedef BOOL    (*PFN_Hook_Install)(HWND);
typedef void    (*PFN_Hook_Uninstall)(void);
typedef BOOL    (*PFN_Hook_IsActive)(void);
typedef void    (*PFN_Hook_GetStats)(HookStats*);
typedef void    (*PFN_Hook_ResetStats)(void);
typedef HWND    (*PFN_Hook_GetTarget)(void);

/* ─── Module State ────────────────────────────────────────────────── */
static struct {
    HMODULE             dll_handle;
    HookManagerState    state;
    HookStateCallback   callback;
    WCHAR               last_error[512];
    HWND                target_hwnd;

    /* Resolved function pointers */
    PFN_Hook_Install    pfnInstall;
    PFN_Hook_Uninstall  pfnUninstall;
    PFN_Hook_IsActive   pfnIsActive;
    PFN_Hook_GetStats   pfnGetStats;
    PFN_Hook_ResetStats pfnResetStats;
    PFN_Hook_GetTarget  pfnGetTarget;
} s_mgr = {0};

/* ─── Internal Helpers ────────────────────────────────────────────── */

static void SetState(HookManagerState new_state, const WCHAR *msg)
{
    s_mgr.state = new_state;
    if (msg) {
        wcsncpy(s_mgr.last_error, msg, 511);
        s_mgr.last_error[511] = L'\0';
    }
    if (s_mgr.callback) {
        s_mgr.callback(new_state, msg);
    }
}

static void SetError(const WCHAR *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    _vsnwprintf(s_mgr.last_error, 511, fmt, args);
    va_end(args);
    s_mgr.last_error[511] = L'\0';
    s_mgr.state = HOOK_STATE_ERROR;
    if (s_mgr.callback) {
        s_mgr.callback(HOOK_STATE_ERROR, s_mgr.last_error);
    }
}

static BOOL ResolveFunctions(void)
{
    s_mgr.pfnInstall    = (PFN_Hook_Install)GetProcAddress(s_mgr.dll_handle, "Hook_Install");
    s_mgr.pfnUninstall  = (PFN_Hook_Uninstall)GetProcAddress(s_mgr.dll_handle, "Hook_Uninstall");
    s_mgr.pfnIsActive   = (PFN_Hook_IsActive)GetProcAddress(s_mgr.dll_handle, "Hook_IsActive");
    s_mgr.pfnGetStats   = (PFN_Hook_GetStats)GetProcAddress(s_mgr.dll_handle, "Hook_GetStats");
    s_mgr.pfnResetStats = (PFN_Hook_ResetStats)GetProcAddress(s_mgr.dll_handle, "Hook_ResetStats");
    s_mgr.pfnGetTarget  = (PFN_Hook_GetTarget)GetProcAddress(s_mgr.dll_handle, "Hook_GetTarget");

    if (!s_mgr.pfnInstall || !s_mgr.pfnUninstall || !s_mgr.pfnIsActive ||
        !s_mgr.pfnGetStats || !s_mgr.pfnResetStats || !s_mgr.pfnGetTarget) {
        SetError(L"hook.dll 导出函数解析失败");
        return FALSE;
    }
    return TRUE;
}

/* ─── Public API ──────────────────────────────────────────────────── */

BOOL HookMgr_Init(void)
{
    WCHAR dll_path[MAX_PATH];
    DWORD len;

    if (s_mgr.dll_handle) return TRUE;

    /* Load hook.dll from the same directory as the executable */
    len = GetModuleFileNameW(NULL, dll_path, MAX_PATH);
    if (len == 0) {
        SetError(L"Cannot get module path");
        return FALSE;
    }

    /* Replace exe filename with hook.dll */
    WCHAR *last_slash = wcsrchr(dll_path, L'\\');
    if (last_slash) {
        wcscpy(last_slash + 1, L"hook.dll");
    } else {
        wcscpy(dll_path, L"hook.dll");
    }

    s_mgr.dll_handle = LoadLibraryW(dll_path);
    if (!s_mgr.dll_handle) {
        SetError(L"Cannot load hook.dll (error: %lu)", GetLastError());
        return FALSE;
    }

    if (!ResolveFunctions()) {
        FreeLibrary(s_mgr.dll_handle);
        s_mgr.dll_handle = NULL;
        return FALSE;
    }

    SetState(HOOK_STATE_IDLE, L"Ready");
    return TRUE;
}

void HookMgr_Shutdown(void)
{
    HookMgr_Detach();

    if (s_mgr.dll_handle) {
        FreeLibrary(s_mgr.dll_handle);
        s_mgr.dll_handle = NULL;
    }

    ZeroMemory(&s_mgr, sizeof(s_mgr));
}

BOOL HookMgr_Attach(HWND target_hwnd)
{
    if (!s_mgr.dll_handle || !s_mgr.pfnInstall) {
        SetError(L"Hook manager not initialized");
        return FALSE;
    }

    if (!IsWindow(target_hwnd)) {
        SetError(L"Invalid window handle: 0x%p", (void*)target_hwnd);
        return FALSE;
    }

    /* Detach from any existing target first */
    if (s_mgr.state == HOOK_STATE_ACTIVE) {
        HookMgr_Detach();
    }

    if (!s_mgr.pfnInstall(target_hwnd)) {
        DWORD err = GetLastError();
        SetError(L"Hook 安装失败 (错误码: %lu)。请确认以管理员身份运行。", err);
        return FALSE;
    }

    s_mgr.target_hwnd = target_hwnd;
    SetState(HOOK_STATE_ACTIVE, L"Hook 已激活");
    return TRUE;
}

void HookMgr_Detach(void)
{
    if (s_mgr.state != HOOK_STATE_ACTIVE) return;

    if (s_mgr.pfnUninstall) {
        s_mgr.pfnUninstall();
    }

    s_mgr.target_hwnd = NULL;
    SetState(HOOK_STATE_IDLE, L"Hook 已卸载");
}

HookManagerState HookMgr_GetState(void)
{
    return s_mgr.state;
}

const WCHAR* HookMgr_GetLastError(void)
{
    return s_mgr.last_error;
}

void HookMgr_SetCallback(HookStateCallback callback)
{
    s_mgr.callback = callback;
}

void HookMgr_GetStats(LONG *killfocus, LONG *activateapp,
                       LONG *ncactivate, LONG *activate)
{
    HookStats stats = {0};

    if (s_mgr.pfnGetStats) {
        s_mgr.pfnGetStats(&stats);
    }

    if (killfocus)   *killfocus   = stats.killfocus_count;
    if (activateapp) *activateapp = stats.activateapp_count;
    if (ncactivate)  *ncactivate  = stats.ncactivate_count;
    if (activate)    *activate    = stats.activate_count;
}

void HookMgr_ResetStats(void)
{
    if (s_mgr.pfnResetStats) {
        s_mgr.pfnResetStats();
    }
}
