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

/* Auto Race page: show profile notes in a modeless dialog (on-demand). */
void    Gui_ShowNotesDialog(HWND parent, const WCHAR *title, const WCHAR *text);

/* Auto Race page: set profile description text (legacy; no-op if control absent) */
void    Gui_SetProfileDescription(const WCHAR *text);

/* ─── Auto Wheelspin Farm pipeline (on the Auto Race page) ──────────── */

/* Parameters read from the pipeline controls.
 * Note: per-car CR/SP cost lives in the car profile; SP-per-lap lives in the
 * race profile - neither is a GUI field anymore. */
typedef struct {
    int  race_laps;      /* manual fallback laps (kept for compatibility) */
    int  cycles;
    int  manual_count;   /* count for single-step buttons */
    BOOL auto_count;
    int  target_sp;      /* SP target (default 999) */
    BOOL enable_race;
    BOOL enable_buy;
    BOOL enable_spin;
    BOOL enable_remove;
} GuiPipelineParams;

/* Read the current pipeline parameter inputs. */
void    Gui_GetPipelineParams(GuiPipelineParams *out);

/* Update the economy/status panel (any string may be NULL). */
void    Gui_SetPipelineEcon(int cr, int sp, int count,
                            const WCHAR *stage, const WCHAR *totals);

/* Append a line to the per-step pipeline log box (Auto Farm tab). */
void    Gui_AppendPipelineLog(const WCHAR *text);

/* Populate the car-profile combo and return the selected index (-1 none). */
void    Gui_PopulateCarProfiles(const WCHAR names[][64], int count);
int     Gui_GetSelectedCarProfile(void);

/* Enable/disable pipeline buttons for the running state. */
void    Gui_SetPipelineRunning(BOOL running);

#endif /* FOCUSKEEPER_GUI_H */
