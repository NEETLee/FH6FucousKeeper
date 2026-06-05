/*
 * window_finder.c - Window Finder (Strategy Pattern)
 *
 * Implements different strategies for locating the FH6 game window.
 * Each strategy encapsulates a specific search algorithm:
 * - SteamStrategy: Searches by process name and Win32 window class
 * - StoreStrategy: Searches through UWP ApplicationFrameHost windows
 * - AutoStrategy: Tries Steam first, falls back to Store
 */

#include "window_finder.h"
#include <wchar.h>
#include <tlhelp32.h>
#include <psapi.h>

/* ─── Internal Helpers ────────────────────────────────────────────── */

typedef struct {
    FindResult *result;
    GameVersion target_version;
} EnumContext;

BOOL WinFinder_GetProcessName(DWORD pid, WCHAR *name, int name_len)
{
    HANDLE hProc;
    BOOL success = FALSE;

    hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (hProc) {
        DWORD size = (DWORD)name_len;
        if (QueryFullProcessImageNameW(hProc, 0, name, &size)) {
            /* Extract just the filename */
            WCHAR *slash = wcsrchr(name, L'\\');
            if (slash) {
                wmemmove(name, slash + 1, wcslen(slash + 1) + 1);
            }
            success = TRUE;
        }
        CloseHandle(hProc);
    }
    return success;
}

static BOOL IsTargetProcess(const WCHAR *process_name)
{
    return (_wcsicmp(process_name, FH6_STEAM_PROCESS) == 0 ||
            _wcsicmp(process_name, FH6_STORE_PROCESS) == 0);
}

static void FillWindowInfo(WindowInfo *info, HWND hwnd)
{
    info->hwnd = hwnd;
    info->tid = GetWindowThreadProcessId(hwnd, &info->pid);
    info->is_visible = IsWindowVisible(hwnd);

    GetWindowTextW(hwnd, info->title, 256);
    GetClassNameW(hwnd, info->class_name, 256);
    WinFinder_GetProcessName(info->pid, info->process_name, MAX_PATH);

    /* Detect version based on window class */
    if (_wcsicmp(info->class_name, UWP_FRAME_CLASS) == 0 ||
        _wcsicmp(info->class_name, FH6_STORE_CLASS) == 0) {
        info->detected_version = GAME_VERSION_STORE;
    } else {
        info->detected_version = GAME_VERSION_STEAM;
    }
}

/* ─── Steam Strategy ──────────────────────────────────────────────── */

static BOOL CALLBACK SteamEnumProc(HWND hwnd, LPARAM lParam)
{
    EnumContext *ctx = (EnumContext *)lParam;
    FindResult *result = ctx->result;
    DWORD pid = 0;
    WCHAR proc_name[MAX_PATH] = {0};
    WCHAR class_name[256] = {0};

    if (!IsWindowVisible(hwnd)) return TRUE;

    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) return TRUE;

    GetClassNameW(hwnd, class_name, 256);

    /* Skip UWP frame windows in Steam strategy */
    if (_wcsicmp(class_name, UWP_FRAME_CLASS) == 0) return TRUE;

    if (!WinFinder_GetProcessName(pid, proc_name, MAX_PATH)) return TRUE;

    if (IsTargetProcess(proc_name)) {
        if (result->count < MAX_CANDIDATES) {
            FillWindowInfo(&result->candidates[result->count], hwnd);
            /* Prefer windows with matching title or class */
            if (result->best_match < 0) {
                result->best_match = result->count;
            }
            result->count++;
        }
    }
    return TRUE;
}

static BOOL FindSteam(FindResult *result)
{
    EnumContext ctx = { result, GAME_VERSION_STEAM };
    EnumWindows(SteamEnumProc, (LPARAM)&ctx);
    return result->count > 0;
}

/* ─── Store Strategy ──────────────────────────────────────────────── */

static BOOL CALLBACK StoreChildEnumProc(HWND hwnd, LPARAM lParam)
{
    EnumContext *ctx = (EnumContext *)lParam;
    FindResult *result = ctx->result;
    WCHAR class_name[256] = {0};

    GetClassNameW(hwnd, class_name, 256);

    if (_wcsicmp(class_name, FH6_STORE_CLASS) == 0) {
        if (result->count < MAX_CANDIDATES) {
            FillWindowInfo(&result->candidates[result->count], hwnd);
            result->candidates[result->count].detected_version = GAME_VERSION_STORE;
            if (result->best_match < 0) {
                result->best_match = result->count;
            }
            result->count++;
        }
    }
    return TRUE;
}

static BOOL CALLBACK StoreEnumProc(HWND hwnd, LPARAM lParam)
{
    EnumContext *ctx = (EnumContext *)lParam;
    WCHAR class_name[256] = {0};
    WCHAR title[256] = {0};

    if (!IsWindowVisible(hwnd)) return TRUE;

    GetClassNameW(hwnd, class_name, 256);

    if (_wcsicmp(class_name, UWP_FRAME_CLASS) == 0) {
        GetWindowTextW(hwnd, title, 256);
        /* Check if this frame hosts our game */
        if (wcsstr(title, L"Forza") || wcsstr(title, L"forza")) {
            /* Enumerate child windows to find CoreWindow */
            EnumChildWindows(hwnd, StoreChildEnumProc, (LPARAM)ctx);

            /* If no CoreWindow found, use the frame window itself */
            if (ctx->result->count == 0) {
                FindResult *result = ctx->result;
                FillWindowInfo(&result->candidates[result->count], hwnd);
                result->candidates[result->count].detected_version = GAME_VERSION_STORE;
                result->best_match = result->count;
                result->count++;
            }
        }
    }
    return TRUE;
}

static BOOL FindStore(FindResult *result)
{
    EnumContext ctx = { result, GAME_VERSION_STORE };
    EnumWindows(StoreEnumProc, (LPARAM)&ctx);
    return result->count > 0;
}

/* ─── Enum All (for GUI window list) ─────────────────────────────── */

static BOOL CALLBACK EnumAllProc(HWND hwnd, LPARAM lParam)
{
    FindResult *result = (FindResult *)lParam;
    WCHAR title[256] = {0};

    if (!IsWindowVisible(hwnd)) return TRUE;

    GetWindowTextW(hwnd, title, 256);
    if (title[0] == L'\0') return TRUE;  /* Skip untitled windows */

    /* Skip tiny/tool windows */
    LONG style = GetWindowLong(hwnd, GWL_STYLE);
    if (style & WS_POPUP && !(style & WS_CAPTION)) return TRUE;

    if (result->count < MAX_CANDIDATES) {
        FillWindowInfo(&result->candidates[result->count], hwnd);
        result->count++;
    }
    return (result->count < MAX_CANDIDATES);
}

/* ─── Public API ──────────────────────────────────────────────────── */

void WinFinder_Init(void)
{
    /* Reserved for future initialization */
}

BOOL WinFinder_Find(GameVersion version, FindResult *result)
{
    if (!result) return FALSE;

    ZeroMemory(result, sizeof(FindResult));
    result->best_match = -1;

    switch (version) {
    case GAME_VERSION_STEAM:
        return FindSteam(result);

    case GAME_VERSION_STORE:
        return FindStore(result);

    case GAME_VERSION_AUTO:
    default:
        /* Try Steam first (more common, higher success rate) */
        if (FindSteam(result)) return TRUE;
        /* Fall back to Store */
        ZeroMemory(result, sizeof(FindResult));
        result->best_match = -1;
        return FindStore(result);
    }
}

BOOL WinFinder_EnumAll(FindResult *result)
{
    if (!result) return FALSE;

    ZeroMemory(result, sizeof(FindResult));
    result->best_match = -1;

    EnumWindows(EnumAllProc, (LPARAM)result);
    return result->count > 0;
}
