/*
 * hook_manager.c - Hook Lifecycle Manager (Facade Pattern)
 *
 * Manages the complete lifecycle of the hook DLL:
 * - Extract embedded hook.dll (RCDATA) to %TEMP%\FH6FocusKeeper\
 * - Dynamic LoadLibrary + symbol resolution
 * - Hook installation/removal
 * - Delete the extracted DLL on shutdown
 */

#include "hook_manager.h"
#include "hook/hook.h"
#include "resource.h"
#include <stdio.h>
#include <stdarg.h>
#include <wchar.h>

/* ─── Function pointer types for DLL imports ──────────────────────── */
typedef BOOL    (*PFN_Hook_Install)(HWND);
typedef void    (*PFN_Hook_Uninstall)(void);
typedef BOOL    (*PFN_Hook_IsActive)(void);
typedef BOOL    (*PFN_Hook_IsSubclassed)(void);
typedef void    (*PFN_Hook_GetStats)(HookStats*);
typedef void    (*PFN_Hook_ResetStats)(void);
typedef HWND    (*PFN_Hook_GetTarget)(void);

/* ─── Module State ────────────────────────────────────────────────── */
static struct {
    HMODULE             dll_handle;
    HookManagerState    state;
    HookStateCallback   callback;
    WCHAR               last_error[512];
    WCHAR               extracted_path[MAX_PATH];
    HWND                target_hwnd;

    /* Resolved function pointers */
    PFN_Hook_Install    pfnInstall;
    PFN_Hook_Uninstall  pfnUninstall;
    PFN_Hook_IsActive   pfnIsActive;
    PFN_Hook_IsSubclassed pfnIsSubclassed;
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

static BOOL WriteExact(HANDLE h, const void *data, DWORD size)
{
    const BYTE *p = (const BYTE *)data;
    DWORD written_total = 0;
    while (written_total < size) {
        DWORD written = 0;
        if (!WriteFile(h, p + written_total, size - written_total, &written, NULL) ||
            written == 0) {
            return FALSE;
        }
        written_total += written;
    }
    return TRUE;
}

/* Mix pid/tick/perf-counter into a per-launch random token (not crypto-grade). */
static void RandomToken(WCHAR *out, int out_cch)
{
    LARGE_INTEGER qpc = {0};
    QueryPerformanceCounter(&qpc);
    unsigned a = (unsigned)GetCurrentProcessId();
    unsigned b = (unsigned)GetTickCount();
    unsigned c = (unsigned)qpc.LowPart ^ (unsigned)(qpc.HighPart * 0x9E3779B9u);
    unsigned d = (unsigned)GetCurrentThreadId();
    _snwprintf(out, out_cch, L"%08X%08X",
               a ^ (c * 0x85EBCA6Bu) ^ (d << 16),
               b ^ (c >> 7) ^ (a * 0xC2B2AE35u));
}

/* Best-effort: remove leftover hook_*.dll from a previous crash / killed process. */
static void CleanupStaleHookDlls(const WCHAR *dir)
{
    WCHAR pattern[MAX_PATH];
    if (_snwprintf(pattern, MAX_PATH, L"%s\\hook_*.dll", dir) >= MAX_PATH)
        return;

    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW(pattern, &fd);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        WCHAR path[MAX_PATH];
        if (_snwprintf(path, MAX_PATH, L"%s\\%s", dir, fd.cFileName) < MAX_PATH)
            DeleteFileW(path);
    } while (FindNextFileW(find, &fd));
    FindClose(find);
}

/*
 * Extract the embedded RCDATA payload to
 *   %TEMP%\FH6FocusKeeper\hook_<random>.dll
 * A fresh random name each launch avoids sharing a locked file across runs.
 */
static BOOL ExtractEmbeddedDll(WCHAR *out_path, int out_cch)
{
    HRSRC hrsrc = FindResourceW(NULL, MAKEINTRESOURCEW(IDR_HOOK_DLL), RT_RCDATA);
    if (!hrsrc) {
        SetError(L"Embedded hook.dll resource missing (error: %lu)", GetLastError());
        return FALSE;
    }

    HGLOBAL hglobal = LoadResource(NULL, hrsrc);
    if (!hglobal) {
        SetError(L"LoadResource failed (error: %lu)", GetLastError());
        return FALSE;
    }

    const BYTE *data = (const BYTE *)LockResource(hglobal);
    DWORD size = SizeofResource(NULL, hrsrc);
    if (!data || size == 0) {
        SetError(L"Embedded hook.dll resource is empty");
        return FALSE;
    }

    WCHAR temp_root[MAX_PATH];
    DWORD n = GetTempPathW(MAX_PATH, temp_root);
    if (n == 0 || n >= MAX_PATH) {
        SetError(L"GetTempPath failed (error: %lu)", GetLastError());
        return FALSE;
    }

    WCHAR dir[MAX_PATH];
    if (_snwprintf(dir, MAX_PATH, L"%sFH6FocusKeeper", temp_root) >= MAX_PATH) {
        SetError(L"Temp path too long");
        return FALSE;
    }
    if (!CreateDirectoryW(dir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
        SetError(L"Cannot create temp dir (error: %lu)", GetLastError());
        return FALSE;
    }

    CleanupStaleHookDlls(dir);

    WCHAR token[20];
    RandomToken(token, 20);
    if (_snwprintf(out_path, out_cch, L"%s\\hook_%s.dll", dir, token) >= out_cch) {
        SetError(L"Extracted DLL path too long");
        return FALSE;
    }

    HANDLE out = CreateFileW(out_path, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                             FILE_ATTRIBUTE_NORMAL, NULL);
    if (out == INVALID_HANDLE_VALUE) {
        /* Extremely unlikely collision — retry once with a new token. */
        RandomToken(token, 20);
        if (_snwprintf(out_path, out_cch, L"%s\\hook_%s.dll", dir, token) >= out_cch ||
            (out = CreateFileW(out_path, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                               FILE_ATTRIBUTE_NORMAL, NULL)) == INVALID_HANDLE_VALUE) {
            SetError(L"Cannot write extracted hook.dll (error: %lu)", GetLastError());
            return FALSE;
        }
    }
    BOOL wrote = WriteExact(out, data, size);
    CloseHandle(out);
    if (!wrote) {
        DeleteFileW(out_path);
        SetError(L"Failed writing extracted hook.dll");
        return FALSE;
    }
    return TRUE;
}

static void DeleteExtractedDll(void)
{
    if (!s_mgr.extracted_path[0]) return;

    /* Game process may still hold the mapping briefly after UnhookWindowsHookEx. */
    for (int i = 0; i < 20; i++) {
        if (DeleteFileW(s_mgr.extracted_path))
            break;
        Sleep(50);
    }

    WCHAR *slash = wcsrchr(s_mgr.extracted_path, L'\\');
    if (slash) {
        WCHAR dir[MAX_PATH];
        size_t len = (size_t)(slash - s_mgr.extracted_path);
        if (len < MAX_PATH) {
            wcsncpy(dir, s_mgr.extracted_path, len);
            dir[len] = L'\0';
            CleanupStaleHookDlls(dir);
            RemoveDirectoryW(dir); /* no-op unless empty */
        }
    }
    s_mgr.extracted_path[0] = L'\0';
}

static BOOL ResolveFunctions(void)
{
    s_mgr.pfnInstall    = (PFN_Hook_Install)GetProcAddress(s_mgr.dll_handle, "Hook_Install");
    s_mgr.pfnUninstall  = (PFN_Hook_Uninstall)GetProcAddress(s_mgr.dll_handle, "Hook_Uninstall");
    s_mgr.pfnIsActive   = (PFN_Hook_IsActive)GetProcAddress(s_mgr.dll_handle, "Hook_IsActive");
    s_mgr.pfnIsSubclassed = (PFN_Hook_IsSubclassed)GetProcAddress(
        s_mgr.dll_handle, "Hook_IsSubclassed");
    s_mgr.pfnGetStats   = (PFN_Hook_GetStats)GetProcAddress(s_mgr.dll_handle, "Hook_GetStats");
    s_mgr.pfnResetStats = (PFN_Hook_ResetStats)GetProcAddress(s_mgr.dll_handle, "Hook_ResetStats");
    s_mgr.pfnGetTarget  = (PFN_Hook_GetTarget)GetProcAddress(s_mgr.dll_handle, "Hook_GetTarget");

    if (!s_mgr.pfnInstall || !s_mgr.pfnUninstall || !s_mgr.pfnIsActive ||
        !s_mgr.pfnIsSubclassed ||
        !s_mgr.pfnGetStats || !s_mgr.pfnResetStats || !s_mgr.pfnGetTarget) {
        SetError(L"hook.dll 导出函数解析失败");
        return FALSE;
    }
    return TRUE;
}

/* ─── Public API ──────────────────────────────────────────────────── */

BOOL HookMgr_Init(void)
{
    if (s_mgr.dll_handle) return TRUE;

    if (!ExtractEmbeddedDll(s_mgr.extracted_path, MAX_PATH))
        return FALSE;

    s_mgr.dll_handle = LoadLibraryW(s_mgr.extracted_path);
    if (!s_mgr.dll_handle) {
        SetError(L"Cannot load extracted hook.dll (error: %lu)", GetLastError());
        DeleteExtractedDll();
        return FALSE;
    }

    if (!ResolveFunctions()) {
        FreeLibrary(s_mgr.dll_handle);
        s_mgr.dll_handle = NULL;
        DeleteExtractedDll();
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

    DeleteExtractedDll();
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

    /* SetWindowsHookEx only confirms registration. Wait until the DLL running
     * in the target process confirms that it actually subclassed this HWND. */
    BOOL confirmed = FALSE;
    for (int i = 0; i < 30; i++) {
        if (s_mgr.pfnIsSubclassed()) {
            confirmed = TRUE;
            break;
        }
        DWORD_PTR ignored = 0;
        SendMessageTimeoutW(target_hwnd, WM_NULL, 0, 0,
                            SMTO_ABORTIFHUNG | SMTO_BLOCK, 100, &ignored);
        Sleep(50);
    }
    if (!confirmed) {
        s_mgr.pfnUninstall();
        SetError(L"Hook 已注册但目标窗口未确认安装。目标可能受保护、位数不兼容或无响应。");
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
