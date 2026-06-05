#ifndef FOCUSKEEPER_SETTINGS_H
#define FOCUSKEEPER_SETTINGS_H

#include <windows.h>
#include "window_finder.h"

/*
 * Settings - Configuration Management
 *
 * Reads/writes application settings from/to an INI file.
 * Stored in the same directory as the executable.
 */

/* Application settings structure */
typedef struct {
    GameVersion game_version;       /* Auto/Steam/Store */
    DWORD       replay_interval;    /* Message replay interval (ms) */
    DWORD       hotkey_modifiers;   /* MOD_CONTROL | MOD_ALT etc. */
    DWORD       hotkey_vk;          /* Virtual key code */
    BOOL        auto_find;          /* Auto-find game window on start */
    BOOL        minimize_to_tray;   /* Minimize to tray on close */
    BOOL        auto_start;         /* Start with Windows */
    BOOL        log_to_file;        /* Enable file logging */
    BOOL        start_minimized;    /* Start minimized to tray */
    int         language;           /* 0=Auto, 1=Chinese, 2=English */
} AppSettings;

/* ─── Public API ──────────────────────────────────────────────────── */

/* Load settings from INI file (or create defaults) */
BOOL    Settings_Load(AppSettings *settings);

/* Save current settings to INI file */
BOOL    Settings_Save(const AppSettings *settings);

/* Reset to default values */
void    Settings_Default(AppSettings *settings);

/* Get path to the INI file */
const WCHAR* Settings_GetPath(void);

/* Default hotkey: Ctrl+F12 */
#define DEFAULT_HOTKEY_MOD  MOD_CONTROL
#define DEFAULT_HOTKEY_VK   VK_F12

#endif /* FOCUSKEEPER_SETTINGS_H */
