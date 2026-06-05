#ifndef FOCUSKEEPER_WINDOW_FINDER_H
#define FOCUSKEEPER_WINDOW_FINDER_H

#include <windows.h>

/*
 * Window Finder - Strategy Pattern
 *
 * Provides different strategies for locating the game window:
 * - Steam version: Find by process name + window class
 * - Store/UWP version: Find by ApplicationFrameHost + CoreWindow
 * - Manual: User-specified HWND
 */

/* Maximum number of candidate windows to track */
#define MAX_CANDIDATES 64

/* Game version type */
typedef enum {
    GAME_VERSION_AUTO,
    GAME_VERSION_STEAM,
    GAME_VERSION_STORE
} GameVersion;

/* Information about a candidate window */
typedef struct {
    HWND    hwnd;
    DWORD   pid;
    DWORD   tid;
    WCHAR   title[256];
    WCHAR   class_name[256];
    WCHAR   process_name[MAX_PATH];
    BOOL    is_visible;
    GameVersion detected_version;
} WindowInfo;

/* Result of a window search */
typedef struct {
    WindowInfo  candidates[MAX_CANDIDATES];
    int         count;
    int         best_match;  /* Index of best candidate, -1 if none */
} FindResult;

/* Strategy function pointer type */
typedef BOOL (*FindStrategy)(FindResult *result);

/* ─── Public API ──────────────────────────────────────────────────── */

/* Initialize the window finder module */
void        WinFinder_Init(void);

/* Find game windows using the specified version strategy */
BOOL        WinFinder_Find(GameVersion version, FindResult *result);

/* Find all visible windows (for the window list GUI) */
BOOL        WinFinder_EnumAll(FindResult *result);

/* Get process name from PID */
BOOL        WinFinder_GetProcessName(DWORD pid, WCHAR *name, int name_len);

/* Known process names for FH6 */
#define FH6_STEAM_PROCESS   L"ForzaHorizon6.exe"
#define FH6_STORE_PROCESS   L"ForzaHorizon6.exe"
#define FH6_WINDOW_TITLE    L"Forza Horizon 6"
#define FH6_STEAM_CLASS     L"ForzaHorizon6"
#define FH6_STORE_CLASS     L"Windows.UI.Core.CoreWindow"
#define UWP_FRAME_CLASS     L"ApplicationFrameWindow"

#endif /* FOCUSKEEPER_WINDOW_FINDER_H */
