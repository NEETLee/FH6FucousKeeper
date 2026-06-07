#ifndef FOCUSKEEPER_I18N_H
#define FOCUSKEEPER_I18N_H

#include <windows.h>

/*
 * Internationalization - String Table
 *
 * Lightweight i18n system using compile-time string tables.
 * Supports Chinese (zh-CN) and English (en).
 * Auto-detects system language, with manual override via settings.
 */

typedef enum {
    LANG_AUTO,
    LANG_ZH,
    LANG_EN,
    LANG_ZH_TW
} Language;

typedef enum {
    /* Window title */
    STR_APP_TITLE,

    /* Status page */
    STR_STATUS_IDLE,
    STR_STATUS_ACTIVE,
    STR_STATUS_ACTIVE_MUTED,
    STR_GAME_WINDOW,
    STR_GAME_VERSION,
    STR_GAME_HWND,
    STR_GAME_PID,
    STR_NOT_DETECTED,
    STR_STATS_HEADER,

    /* Buttons */
    STR_BTN_FIND,
    STR_BTN_ENABLE,
    STR_BTN_DISABLE,
    STR_BTN_MUTE_ENABLE,
    STR_BTN_MUTE_DISABLE,

    /* Tabs */
    STR_TAB_STATUS,
    STR_TAB_WINDOWS,
    STR_TAB_LOG,
    STR_TAB_SETTINGS,

    /* Window list page */
    STR_COL_TITLE,
    STR_COL_CLASS,
    STR_COL_PROCESS,
    STR_COL_PID,
    STR_COL_HWND,
    STR_BTN_REFRESH,
    STR_BTN_SELECT,

    /* Log page */
    STR_BTN_CLEAR_LOG,

    /* Settings page */
    STR_SETTINGS_VERSION,
    STR_SETTINGS_AUTO,
    STR_SETTINGS_STEAM,
    STR_SETTINGS_STORE,
    STR_SETTINGS_OPTIONS,
    STR_SETTINGS_AUTOFIND,
    STR_SETTINGS_TRAY,
    STR_SETTINGS_LOGFILE,
    STR_SETTINGS_LANGUAGE,
    STR_SETTINGS_LANG_AUTO,
    STR_SETTINGS_LANG_ZH,
    STR_SETTINGS_LANG_ZH_TW,
    STR_SETTINGS_LANG_EN,
    STR_BTN_SAVE,
    STR_BTN_DEFAULT,

    /* Tray */
    STR_TRAY_SHOW,
    STR_TRAY_TOGGLE_ON,
    STR_TRAY_TOGGLE_OFF,
    STR_TRAY_EXIT,
    STR_TRAY_TIP_IDLE,
    STR_TRAY_TIP_ACTIVE,
    STR_TRAY_MINIMIZED,

    /* Log messages */
    STR_LOG_STARTED,
    STR_LOG_HOOK_INSTALLED,
    STR_LOG_HOOK_REMOVED,
    STR_LOG_GAME_FOUND,
    STR_LOG_GAME_NOT_FOUND,
    STR_LOG_MUTED,
    STR_LOG_UNMUTED,
    STR_LOG_HOTKEY_REGISTERED,
    STR_LOG_HOTKEY_FAILED,
    STR_LOG_INIT_DONE,
    STR_LOG_WINDOW_CLOSED,
    STR_LOG_SETTINGS_SAVED,
    STR_LOG_HOOK_FAILED,

    /* Version info */
    STR_VERSION_STEAM,
    STR_VERSION_STORE,
    STR_VERSION_UNKNOWN,

    /* About / Credits */
    STR_ABOUT_BRIEF,
    STR_ABOUT_AUTHOR,
    STR_ABOUT_REPO,

    /* Tip */
    STR_TIP_WINDOWED,

    /* Settings - Prevent Sleep */
    STR_SETTINGS_PREVENT_SLEEP,

    /* Auto Race page */
    STR_TAB_AUTO_RACE,
    STR_RACE_PROFILE,
    STR_RACE_BTN_START,
    STR_RACE_BTN_STOP,
    STR_RACE_STATUS_IDLE,
    STR_RACE_STATUS_RUNNING,
    STR_RACE_STATUS_ERROR,
    STR_RACE_STEP,
    STR_RACE_LAPS,
    STR_RACE_TIME,
    STR_RACE_TIP,
    STR_LOG_RACE_STARTED,
    STR_LOG_RACE_STOPPED,

    STR_COUNT
} StringId;

/* ─── Public API ──────────────────────────────────────────────────── */

/* Initialize i18n with given language (LANG_AUTO detects system) */
void            I18n_Init(Language lang);

/* Change language at runtime */
void            I18n_SetLanguage(Language lang);

/* Get current effective language */
Language        I18n_GetLanguage(void);

/* Detect system UI language */
Language        I18n_DetectSystem(void);

/* Get localized string by ID */
const WCHAR*    I18n_Get(StringId id);

#endif /* FOCUSKEEPER_I18N_H */
