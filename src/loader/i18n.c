/*
 * i18n.c - Internationalization String Tables
 *
 * Contains all UI strings in Chinese and English.
 * Language can be auto-detected from system or manually set.
 */

#include "i18n.h"

static Language s_current_lang = LANG_ZH;

/* ─── Chinese String Table ────────────────────────────────────────── */
static const WCHAR *s_strings_zh[STR_COUNT] = {
    /* STR_APP_TITLE */         L"FH6 FocusKeeper",

    /* STR_STATUS_IDLE */       L"\u25cf  \u72b6\u6001\uff1a\u672a\u6fc0\u6d3b",
    /* STR_STATUS_ACTIVE */     L"\u25cf  \u72b6\u6001\uff1a\u9632\u6682\u505c\u5df2\u6fc0\u6d3b",
    /* STR_STATUS_ACTIVE_MUTED */ L"\u25cf  \u72b6\u6001\uff1a\u9632\u6682\u505c\u5df2\u6fc0\u6d3b\uff08\u5df2\u9759\u97f3\uff09",
    /* STR_GAME_WINDOW */       L"\u6e38\u620f\u7a97\u53e3\uff1a",
    /* STR_GAME_VERSION */      L"\u7248\u672c\u7c7b\u578b\uff1a",
    /* STR_GAME_HWND */         L"\u7a97\u53e3\u53e5\u67c4\uff1a",
    /* STR_GAME_PID */          L"\u8fdb\u7a0b PID\uff1a",
    /* STR_NOT_DETECTED */      L"\u672a\u68c0\u6d4b\u5230",
    /* STR_STATS_HEADER */      L"\u62e6\u622a\u7edf\u8ba1\uff1a",

    /* STR_BTN_FIND */          L"\u67e5\u627e\u6e38\u620f\u7a97\u53e3",
    /* STR_BTN_ENABLE */        L"\u5f00\u542f\u9632\u6682\u505c",
    /* STR_BTN_DISABLE */       L"\u5173\u95ed\u9632\u6682\u505c",
    /* STR_BTN_MUTE_ENABLE */   L"\u9759\u97f3\u6e38\u620f",
    /* STR_BTN_MUTE_DISABLE */  L"\u53d6\u6d88\u9759\u97f3",

    /* STR_TAB_STATUS */        L"\u72b6\u6001",
    /* STR_TAB_WINDOWS */       L"\u7a97\u53e3\u5217\u8868",
    /* STR_TAB_LOG */           L"\u65e5\u5fd7",
    /* STR_TAB_SETTINGS */      L"\u8bbe\u7f6e",

    /* STR_COL_TITLE */         L"\u6807\u9898",
    /* STR_COL_CLASS */         L"\u7c7b\u540d",
    /* STR_COL_PROCESS */       L"\u8fdb\u7a0b",
    /* STR_COL_PID */           L"PID",
    /* STR_COL_HWND */          L"HWND",
    /* STR_BTN_REFRESH */       L"\u5237\u65b0\u5217\u8868",
    /* STR_BTN_SELECT */        L"\u9009\u5b9a\u4e3a\u76ee\u6807\u7a97\u53e3",

    /* STR_BTN_CLEAR_LOG */     L"\u6e05\u9664\u65e5\u5fd7",

    /* STR_SETTINGS_VERSION */  L"\u6e38\u620f\u7248\u672c\uff1a",
    /* STR_SETTINGS_AUTO */     L"\u81ea\u52a8\u68c0\u6d4b",
    /* STR_SETTINGS_STEAM */    L"Steam",
    /* STR_SETTINGS_STORE */    L"Store",
    /* STR_SETTINGS_INTERVAL */ L"\u6d88\u606f\u91cd\u653e\u95f4\u9694 (ms)\uff1a",
    /* STR_SETTINGS_OPTIONS */  L"\u9009\u9879\uff1a",
    /* STR_SETTINGS_AUTOFIND */ L"\u542f\u52a8\u65f6\u81ea\u52a8\u67e5\u627e\u6e38\u620f\u7a97\u53e3",
    /* STR_SETTINGS_TRAY */     L"\u5173\u95ed\u65f6\u6700\u5c0f\u5316\u5230\u6258\u76d8",
    /* STR_SETTINGS_LOGFILE */  L"\u8bb0\u5f55\u65e5\u5fd7\u5230\u6587\u4ef6",
    /* STR_SETTINGS_LANGUAGE */ L"\u8bed\u8a00\uff1a",
    /* STR_SETTINGS_LANG_AUTO */L"\u8ddf\u968f\u7cfb\u7edf",
    /* STR_SETTINGS_LANG_ZH */  L"\u4e2d\u6587",
    /* STR_SETTINGS_LANG_EN */  L"English",
    /* STR_BTN_SAVE */          L"\u4fdd\u5b58\u8bbe\u7f6e",
    /* STR_BTN_DEFAULT */       L"\u6062\u590d\u9ed8\u8ba4",

    /* STR_TRAY_SHOW */         L"\u663e\u793a\u4e3b\u7a97\u53e3",
    /* STR_TRAY_TOGGLE_ON */    L"\u5f00\u542f\u9632\u6682\u505c",
    /* STR_TRAY_TOGGLE_OFF */   L"\u5173\u95ed\u9632\u6682\u505c",
    /* STR_TRAY_EXIT */         L"\u9000\u51fa",
    /* STR_TRAY_TIP_IDLE */     L"FH6 FocusKeeper - \u5c31\u7eea",
    /* STR_TRAY_TIP_ACTIVE */   L"FH6 FocusKeeper - \u9632\u6682\u505c\u5df2\u6fc0\u6d3b",
    /* STR_TRAY_MINIMIZED */    L"\u7a0b\u5e8f\u5df2\u6700\u5c0f\u5316\u5230\u7cfb\u7edf\u6258\u76d8\uff0c\u53cc\u51fb\u56fe\u6807\u53ef\u91cd\u65b0\u6253\u5f00",

    /* STR_LOG_STARTED */       L"FH6 FocusKeeper \u542f\u52a8",
    /* STR_LOG_HOOK_INSTALLED */L"Hook \u5b89\u88c5\u6210\u529f\uff0c\u7a97\u53e3\u5b50\u7c7b\u5316\u5b8c\u6210",
    /* STR_LOG_HOOK_REMOVED */  L"Hook \u5df2\u5378\u8f7d",
    /* STR_LOG_GAME_FOUND */    L"\u627e\u5230\u6e38\u620f\u7a97\u53e3",
    /* STR_LOG_GAME_NOT_FOUND */L"\u672a\u627e\u5230\u6e38\u620f\u7a97\u53e3",
    /* STR_LOG_MUTED */         L"\u6e38\u620f\u5df2\u9759\u97f3",
    /* STR_LOG_UNMUTED */       L"\u6e38\u620f\u5df2\u53d6\u6d88\u9759\u97f3",
    /* STR_LOG_HOTKEY_REGISTERED */ L"\u70ed\u952e\u5df2\u6ce8\u518c: Ctrl+F12",
    /* STR_LOG_HOTKEY_FAILED */ L"\u70ed\u952e\u6ce8\u518c\u5931\u8d25 (\u53ef\u80fd\u88ab\u5176\u4ed6\u7a0b\u5e8f\u5360\u7528)",
    /* STR_LOG_INIT_DONE */     L"\u521d\u59cb\u5316\u5b8c\u6210",
    /* STR_LOG_WINDOW_CLOSED */ L"\u6e38\u620f\u7a97\u53e3\u5df2\u5173\u95ed",
    /* STR_LOG_SETTINGS_SAVED */L"\u8bbe\u7f6e\u5df2\u4fdd\u5b58",
    /* STR_LOG_REPLAY_STARTED */L"\u6d88\u606f\u91cd\u653e\u5df2\u542f\u52a8",
    /* STR_LOG_REPLAY_STOPPED */L"\u6d88\u606f\u91cd\u653e\u5df2\u505c\u6b62",
    /* STR_LOG_HOOK_FAILED */   L"Hook \u5b89\u88c5\u5931\u8d25",
    /* STR_LOG_DLL_UNAVAILABLE */ L"DLL Hook \u4e0d\u53ef\u7528\uff0c\u5c06\u4ec5\u652f\u6301\u6d88\u606f\u91cd\u653e\u6a21\u5f0f",

    /* STR_VERSION_STEAM */     L"Steam (Win32)",
    /* STR_VERSION_STORE */     L"Store (UWP)",
    /* STR_VERSION_UNKNOWN */   L"\u672a\u77e5",

    /* STR_ABOUT_BRIEF */       L"By NEETLee & Claude Opus 4.6",
    /* STR_ABOUT_AUTHOR */      L"\u4f5c\u8005\uff1aNEETLee & Claude Opus 4.6",
    /* STR_ABOUT_REPO */        L"GitHub: <a href=\"https://github.com/NEETLee/FH6FucousKeeper\">https://github.com/NEETLee/FH6FucousKeeper</a>",

    /* STR_TIP_WINDOWED */      L"\u203b \u8bf7\u5207\u6362\u7a97\u53e3\u6a21\u5f0f\u8fd0\u884c\u6e38\u620f\uff08\u6e38\u620f\u5feb\u6377\u952e Alt+Enter\uff09",

    /* STR_SETTINGS_PREVENT_SLEEP */ L"\u9632\u6682\u505c\u65f6\u963b\u6b62\u7cfb\u7edf\u4f11\u7720",

    /* STR_TAB_AUTO_RACE */     L"\u81ea\u52a8\u8d5b\u4e8b",
    /* STR_RACE_PROFILE */      L"\u8d5b\u4e8b\u914d\u7f6e\uff1a",
    /* STR_RACE_BTN_START */    L"\u5f00\u59cb\u81ea\u52a8\u8d5b\u4e8b",
    /* STR_RACE_BTN_STOP */     L"\u505c\u6b62\u81ea\u52a8\u8d5b\u4e8b",
    /* STR_RACE_STATUS_IDLE */  L"\u72b6\u6001\uff1a\u672a\u8fd0\u884c",
    /* STR_RACE_STATUS_RUNNING */ L"\u72b6\u6001\uff1a\u8fd0\u884c\u4e2d",
    /* STR_RACE_STATUS_ERROR */ L"\u72b6\u6001\uff1a\u9519\u8bef",
    /* STR_RACE_STEP */         L"\u5f53\u524d\u6b65\u9aa4\uff1a",
    /* STR_RACE_LAPS */         L"\u5df2\u5b8c\u6210\u5708\u6570\uff1a",
    /* STR_RACE_TIME */         L"\u8fd0\u884c\u65f6\u95f4\uff1a",
    /* STR_RACE_TIP */          L"\u203b \u8bf7\u5148\u5728\u6e38\u620f\u4e2d\u5f00\u542f\u81ea\u52a8\u8f6c\u5411\u8f85\u52a9\uff0c\u70ed\u952e Ctrl+F11 \u5207\u6362",
    /* STR_LOG_RACE_STARTED */  L"\u81ea\u52a8\u8d5b\u4e8b\u5df2\u542f\u52a8",
    /* STR_LOG_RACE_STOPPED */  L"\u81ea\u52a8\u8d5b\u4e8b\u5df2\u505c\u6b62",
};

/* ─── English String Table ────────────────────────────────────────── */
static const WCHAR *s_strings_en[STR_COUNT] = {
    /* STR_APP_TITLE */         L"FH6 FocusKeeper",

    /* STR_STATUS_IDLE */       L"\u25cf  Status: Inactive",
    /* STR_STATUS_ACTIVE */     L"\u25cf  Status: Anti-Pause Active",
    /* STR_STATUS_ACTIVE_MUTED */ L"\u25cf  Status: Anti-Pause Active (Muted)",
    /* STR_GAME_WINDOW */       L"Game Window: ",
    /* STR_GAME_VERSION */      L"Version: ",
    /* STR_GAME_HWND */         L"HWND: ",
    /* STR_GAME_PID */          L"PID: ",
    /* STR_NOT_DETECTED */      L"Not Detected",
    /* STR_STATS_HEADER */      L"Interception Stats:",

    /* STR_BTN_FIND */          L"Find Game Window",
    /* STR_BTN_ENABLE */        L"Enable Anti-Pause",
    /* STR_BTN_DISABLE */       L"Disable Anti-Pause",
    /* STR_BTN_MUTE_ENABLE */   L"Mute Game",
    /* STR_BTN_MUTE_DISABLE */  L"Unmute Game",

    /* STR_TAB_STATUS */        L"Status",
    /* STR_TAB_WINDOWS */       L"Windows",
    /* STR_TAB_LOG */           L"Log",
    /* STR_TAB_SETTINGS */      L"Settings",

    /* STR_COL_TITLE */         L"Title",
    /* STR_COL_CLASS */         L"Class",
    /* STR_COL_PROCESS */       L"Process",
    /* STR_COL_PID */           L"PID",
    /* STR_COL_HWND */          L"HWND",
    /* STR_BTN_REFRESH */       L"Refresh",
    /* STR_BTN_SELECT */        L"Set as Target",

    /* STR_BTN_CLEAR_LOG */     L"Clear Log",

    /* STR_SETTINGS_VERSION */  L"Game Version:",
    /* STR_SETTINGS_AUTO */     L"Auto Detect",
    /* STR_SETTINGS_STEAM */    L"Steam",
    /* STR_SETTINGS_STORE */    L"Store",
    /* STR_SETTINGS_INTERVAL */ L"Replay Interval (ms):",
    /* STR_SETTINGS_OPTIONS */  L"Options:",
    /* STR_SETTINGS_AUTOFIND */ L"Auto-find game window on start",
    /* STR_SETTINGS_TRAY */     L"Minimize to tray on close",
    /* STR_SETTINGS_LOGFILE */  L"Write log to file",
    /* STR_SETTINGS_LANGUAGE */ L"Language:",
    /* STR_SETTINGS_LANG_AUTO */L"System Default",
    /* STR_SETTINGS_LANG_ZH */  L"\u4e2d\u6587",
    /* STR_SETTINGS_LANG_EN */  L"English",
    /* STR_BTN_SAVE */          L"Save Settings",
    /* STR_BTN_DEFAULT */       L"Reset Defaults",

    /* STR_TRAY_SHOW */         L"Show Window",
    /* STR_TRAY_TOGGLE_ON */    L"Enable Anti-Pause",
    /* STR_TRAY_TOGGLE_OFF */   L"Disable Anti-Pause",
    /* STR_TRAY_EXIT */         L"Exit",
    /* STR_TRAY_TIP_IDLE */     L"FH6 FocusKeeper - Ready",
    /* STR_TRAY_TIP_ACTIVE */   L"FH6 FocusKeeper - Active",
    /* STR_TRAY_MINIMIZED */    L"Minimized to system tray. Double-click icon to restore.",

    /* STR_LOG_STARTED */       L"FH6 FocusKeeper started",
    /* STR_LOG_HOOK_INSTALLED */L"Hook installed, window subclassed",
    /* STR_LOG_HOOK_REMOVED */  L"Hook removed",
    /* STR_LOG_GAME_FOUND */    L"Game window found",
    /* STR_LOG_GAME_NOT_FOUND */L"Game window not found",
    /* STR_LOG_MUTED */         L"Game audio muted",
    /* STR_LOG_UNMUTED */       L"Game audio unmuted",
    /* STR_LOG_HOTKEY_REGISTERED */ L"Hotkey registered: Ctrl+F12",
    /* STR_LOG_HOTKEY_FAILED */ L"Hotkey registration failed (may be in use)",
    /* STR_LOG_INIT_DONE */     L"Initialization complete",
    /* STR_LOG_WINDOW_CLOSED */ L"Game window closed",
    /* STR_LOG_SETTINGS_SAVED */L"Settings saved",
    /* STR_LOG_REPLAY_STARTED */L"Message replay started",
    /* STR_LOG_REPLAY_STOPPED */L"Message replay stopped",
    /* STR_LOG_HOOK_FAILED */   L"Hook installation failed",
    /* STR_LOG_DLL_UNAVAILABLE */ L"DLL Hook unavailable, message replay only",

    /* STR_VERSION_STEAM */     L"Steam (Win32)",
    /* STR_VERSION_STORE */     L"Store (UWP)",
    /* STR_VERSION_UNKNOWN */   L"Unknown",

    /* STR_ABOUT_BRIEF */       L"By NEETLee & Claude Opus 4.6",
    /* STR_ABOUT_AUTHOR */      L"Author: NEETLee & Claude Opus 4.6",
    /* STR_ABOUT_REPO */        L"GitHub: <a href=\"https://github.com/NEETLee/FH6FucousKeeper\">https://github.com/NEETLee/FH6FucousKeeper</a>",

    /* STR_TIP_WINDOWED */      L"\u203b Please run the game in windowed mode (game hotkey Alt+Enter)",

    /* STR_SETTINGS_PREVENT_SLEEP */ L"Prevent sleep while active",

    /* STR_TAB_AUTO_RACE */     L"Auto Race",
    /* STR_RACE_PROFILE */      L"Profile:",
    /* STR_RACE_BTN_START */    L"Start Auto Race",
    /* STR_RACE_BTN_STOP */     L"Stop Auto Race",
    /* STR_RACE_STATUS_IDLE */  L"Status: Idle",
    /* STR_RACE_STATUS_RUNNING */ L"Status: Running",
    /* STR_RACE_STATUS_ERROR */ L"Status: Error",
    /* STR_RACE_STEP */         L"Current Step:",
    /* STR_RACE_LAPS */         L"Laps Completed:",
    /* STR_RACE_TIME */         L"Running Time:",
    /* STR_RACE_TIP */          L"\u203b Enable auto-steer assist in game first. Hotkey: Ctrl+F11",
    /* STR_LOG_RACE_STARTED */  L"Auto race started",
    /* STR_LOG_RACE_STOPPED */  L"Auto race stopped",
};

/* ─── Public API ──────────────────────────────────────────────────── */

Language I18n_DetectSystem(void)
{
    LANGID lang = GetUserDefaultUILanguage();
    WORD primary = PRIMARYLANGID(lang);

    if (primary == LANG_CHINESE) return LANG_ZH;
    return LANG_EN;
}

void I18n_Init(Language lang)
{
    if (lang == LANG_AUTO) {
        s_current_lang = I18n_DetectSystem();
    } else {
        s_current_lang = lang;
    }
}

void I18n_SetLanguage(Language lang)
{
    if (lang == LANG_AUTO) {
        s_current_lang = I18n_DetectSystem();
    } else {
        s_current_lang = lang;
    }
}

Language I18n_GetLanguage(void)
{
    return s_current_lang;
}

const WCHAR* I18n_Get(StringId id)
{
    if (id < 0 || id >= STR_COUNT) return L"???";

    if (s_current_lang == LANG_EN) {
        return s_strings_en[id];
    }
    return s_strings_zh[id];
}
