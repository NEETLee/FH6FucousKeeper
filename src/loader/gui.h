#ifndef FOCUSKEEPER_GUI_H
#define FOCUSKEEPER_GUI_H

#include <windows.h>
#include <commctrl.h>
#include "settings.h"
#include "window_finder.h"

/*
 * GUI Module - Main Application Interface
 *
 * Manages the main dialog window with tabbed interface:
 * - Status page: Hook state and statistics
 * - Window list: Candidate window selection
 * - Log page: Real-time event logging
 * - Settings: Configuration panel
 */

/* GUI initialization context */
typedef struct {
    HINSTANCE   hInstance;
    AppSettings *settings;
} GuiContext;

/* ─── Public API ──────────────────────────────────────────────────── */

/* Create and show the main GUI window */
HWND    Gui_Create(const GuiContext *ctx);

/* Update status display */
void    Gui_UpdateStatus(BOOL hook_active, const WCHAR *game_title,
                         const WCHAR *version_str, HWND game_hwnd, DWORD game_pid);

/* Update statistics display */
void    Gui_UpdateStats(LONG killfocus, LONG activateapp, LONG ncactivate, LONG activate);

/* Append a log entry to the log page */
void    Gui_AppendLog(const WCHAR *text);

/* Clear all log entries */
void    Gui_ClearLog(void);

/* Refresh the window list */
void    Gui_RefreshWindowList(const FindResult *result);

/* Get the main dialog handle */
HWND    Gui_GetMainWindow(void);

/* Show/hide the main window */
void    Gui_Show(BOOL show);

/* Check if GUI is minimized */
BOOL    Gui_IsMinimized(void);

/* Update button text based on hook/mute state */
void    Gui_UpdateButtons(BOOL hook_active, BOOL muted);

/* Read settings page values back into the settings struct */
void    Gui_ReadSettings(AppSettings *settings);

/* Refresh all control text after language change */
void    Gui_RefreshLanguage(BOOL hook_active, BOOL muted);

/* Auto Race page: update status display */
void    Gui_UpdateRaceStatus(const WCHAR *status, const WCHAR *step,
                             int laps, DWORD elapsed_ms);

/* Auto Race page: set running state (enable/disable buttons) */
void    Gui_SetRaceRunning(BOOL running);

/* Auto Race page: populate profile combo box */
void    Gui_PopulateProfiles(const WCHAR names[][64], int count);

/* Auto Race page: get currently selected profile filename */
void    Gui_GetSelectedProfile(WCHAR *out, int max_len);

/* Show update available notification next to repo link */
void    Gui_ShowUpdateAvailable(const WCHAR *version, const WCHAR *url);

/* Auto Race page: set profile description text */
void    Gui_SetProfileDescription(const WCHAR *text);

#endif /* FOCUSKEEPER_GUI_H */
