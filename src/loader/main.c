/*
 * main.c - FH6 FocusKeeper Application Entry Point
 *
 * Orchestrates all modules:
 * - Settings loading
 * - GUI creation
 * - Hook management
 * - Tray icon
 * - Hotkey registration
 * - Auto-find timer
 *
 * Design Pattern: Mediator
 * The main module acts as a mediator between GUI, Hook Manager,
 * Window Finder, and Message Replay modules. It routes events
 * and coordinates their interactions.
 */

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <stdio.h>
#include <wchar.h>
#include "gui.h"
#include "tray.h"
#include "hook_manager.h"
#include "msg_replay.h"
#include "window_finder.h"
#include "logger.h"
#include "settings.h"
#include "audio_control.h"
#include "i18n.h"
#include "resource.h"
#include "race_controller.h"
#include "race_profile.h"
#include "version_check.h"

#define WM_VERSION_CHECK_DONE (WM_APP + 100)

/* ─── Application State ───────────────────────────────────────────── */
static struct {
    AppSettings     settings;
    HWND            game_hwnd;
    DWORD           game_pid;
    GameVersion     detected_version;
    WCHAR           game_title[256];
    BOOL            hook_active;
    BOOL            game_muted;
    FindResult      last_find_result;
} s_app = {0};

/* ─── Forward Declarations ────────────────────────────────────────── */
static void OnHookStateChanged(HookManagerState state, const WCHAR *msg);
static void OnLogEntry(const LogEntry *entry, void *user_data);
static void DoFindGame(void);
static void DoEnableHook(void);
static void DoDisableHook(void);
static void DoToggleHook(void);
static void DoMuteToggle(void);
static void DoRefreshWindowList(void);
static void DoSelectWindow(int index);
static void DoSaveSettings(void);
static void UpdateStatsDisplay(void);
static void DoToggleAutoRace(void);
static void DoStartAutoRace(void);
static void DoStopAutoRace(void);

/* Race controller instance */
static RaceController s_race_ctrl = {0};

/* ─── Main Window Message Hook ────────────────────────────────────── */

/*
 * We subclass the main window to handle our custom messages
 * (commands from GUI buttons, tray, timers, hotkey)
 */
static WNDPROC s_orig_main_proc = NULL;

static LRESULT CALLBACK AppWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_BTN_FIND:      DoFindGame(); return 0;
        case IDC_BTN_ENABLE:    DoToggleHook(); return 0;
        case IDC_BTN_MUTE_TOGGLE: DoMuteToggle(); return 0;
        case IDC_BTN_REFRESH:   DoRefreshWindowList(); return 0;
        case IDC_BTN_SELECT: {
            DoSelectWindow(-1);
            return 0;
        }
        case IDC_BTN_CLEAR_LOG:
            SetWindowTextW(GetDlgItem(hwnd, IDC_LOG_EDIT), L"");
            return 0;
        case IDC_BTN_SAVE:      DoSaveSettings(); return 0;

        case IDC_BTN_RACE_START: DoStartAutoRace(); return 0;
        case IDC_BTN_RACE_STOP:  DoStopAutoRace(); return 0;

        case IDC_COMBO_PROFILE:
            return 0;

        case IDM_TRAY_SHOW:
            Gui_Show(TRUE);
            return 0;
        case IDM_TRAY_TOGGLE:
            DoToggleHook();
            return 0;
        case IDM_TRAY_EXIT:
            DoDisableHook();
            DestroyWindow(hwnd);
            return 0;
        }
        break;

    case WM_TRAY_CALLBACK:
        Tray_HandleMessage(hwnd, wParam, lParam);
        return 0;

    case WM_USER + 99:
        Tray_ShowBalloon(L"FH6 FocusKeeper",
            I18n_Get(STR_TRAY_MINIMIZED), NIIF_INFO);
        return 0;

    case WM_HOTKEY:
        if (wParam == IDH_TOGGLE) {
            DoToggleHook();
        } else if (wParam == IDH_AUTO_RACE) {
            DoToggleAutoRace();
        }
        return 0;

    case WM_TIMER:
        if (wParam == IDT_STATS_UPDATE) {
            UpdateStatsDisplay();
            /* Update race time display if running */
            if (RaceCtrl_IsRunning(&s_race_ctrl)) {
                AutoRaceStatus st;
                RaceCtrl_GetStatus(&s_race_ctrl, &st);
                Gui_UpdateRaceStatus(I18n_Get(STR_RACE_STATUS_RUNNING),
                    st.step_name, st.lap_count, st.total_elapsed);
            }
        } else if (wParam == IDT_AUTO_FIND) {
            if (!s_app.hook_active && s_app.game_hwnd == NULL) {
                DoFindGame();
            }
        }
        return 0;

    case WM_VERSION_CHECK_DONE:
        {
            WCHAR ver[64] = {0}, url[256] = {0};
            if (VersionCheck_GetResult(ver, 64, url, 256)) {
                Gui_ShowUpdateAvailable(ver, url);
            }
        }
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, IDT_STATS_UPDATE);
        KillTimer(hwnd, IDT_AUTO_FIND);
        UnregisterHotKey(hwnd, IDH_TOGGLE);
        UnregisterHotKey(hwnd, IDH_AUTO_RACE);
        Tray_Destroy();
        break;
    }

    return CallWindowProc(s_orig_main_proc, hwnd, msg, wParam, lParam);
}

/* ─── Logger Callback (Observer) ──────────────────────────────────── */

static void OnLogEntry(const LogEntry *entry, void *user_data)
{
    (void)user_data;
    WCHAR line[1200];

    _snwprintf(line, 1199, L"[%02d:%02d:%02d] [%s] %s",
        entry->timestamp.wHour, entry->timestamp.wMinute, entry->timestamp.wSecond,
        Logger_LevelName(entry->level), entry->message);

    Gui_AppendLog(line);
}

/* ─── Auto Race State Callback (Observer) ─────────────────────────── */

static void OnAutoRaceStateChanged(AutoRaceState state, const WCHAR *message, void *user_data)
{
    (void)user_data;
    (void)message;

    switch (state) {
    case AUTO_RACE_RUNNING:
        Gui_SetRaceRunning(TRUE);
        break;
    case AUTO_RACE_IDLE:
        Gui_SetRaceRunning(FALSE);
        Gui_UpdateRaceStatus(I18n_Get(STR_RACE_STATUS_IDLE), NULL, 0, 0);
        break;
    case AUTO_RACE_ERROR:
        Gui_SetRaceRunning(FALSE);
        Gui_UpdateRaceStatus(I18n_Get(STR_RACE_STATUS_ERROR), NULL, 0, 0);
        LOG_W(L"Auto race error: %s", message ? message : L"unknown");
        break;
    }
}

/* ─── Hook State Callback (Observer) ──────────────────────────────── */

static void OnHookStateChanged(HookManagerState state, const WCHAR *message)
{
    (void)message;

    switch (state) {
    case HOOK_STATE_ACTIVE:
        s_app.hook_active = TRUE;
        Tray_SetState(TRAY_STATE_ACTIVE);
        Tray_ShowBalloon(L"FH6 FocusKeeper", I18n_Get(STR_STATUS_ACTIVE), NIIF_INFO);
        /* Prevent system sleep if enabled */
        if (s_app.settings.prevent_sleep) {
            SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED);
        }
        break;
    case HOOK_STATE_IDLE:
        s_app.hook_active = FALSE;
        Tray_SetState(TRAY_STATE_IDLE);
        break;
    case HOOK_STATE_ERROR:
        s_app.hook_active = FALSE;
        Tray_SetState(TRAY_STATE_ERROR);
        break;
    }

    Gui_UpdateStatus(s_app.hook_active, s_app.game_title,
        NULL, s_app.game_hwnd, s_app.game_pid);
}

/* ─── Action Handlers ─────────────────────────────────────────────── */

static void DoFindGame(void)
{
    FindResult result;

    LOG_I(L"Searching for game window...");

    if (WinFinder_Find(s_app.settings.game_version, &result)) {
        if (result.best_match >= 0) {
            const WindowInfo *best = &result.candidates[result.best_match];
            s_app.game_hwnd = best->hwnd;
            s_app.game_pid = best->pid;
            s_app.detected_version = best->detected_version;
            wcsncpy(s_app.game_title, best->title, 255);

            LOG_I(L"%s: %s (PID: %lu, HWND: 0x%08X)",
                I18n_Get(STR_LOG_GAME_FOUND),
                best->title, best->pid, (unsigned)(UINT_PTR)best->hwnd);

            Gui_UpdateStatus(s_app.hook_active, s_app.game_title,
                NULL, s_app.game_hwnd, s_app.game_pid);
        }
    } else {
        LOG_W(L"%s", I18n_Get(STR_LOG_GAME_NOT_FOUND));
        s_app.game_hwnd = NULL;
        s_app.game_pid = 0;
        s_app.game_title[0] = L'\0';
        Gui_UpdateStatus(FALSE, NULL, NULL, NULL, 0);
    }
}

static void DoEnableHook(void)
{
    if (s_app.hook_active) {
        LOG_W(L"Hook already active");
        return;
    }

    if (!s_app.game_hwnd) {
        DoFindGame();
        if (!s_app.game_hwnd) {
            LOG_E(L"Cannot enable: %s", I18n_Get(STR_LOG_GAME_NOT_FOUND));
            return;
        }
    }

    /* Verify window still exists */
    if (!IsWindow(s_app.game_hwnd)) {
        LOG_W(L"%s, re-searching...", I18n_Get(STR_LOG_WINDOW_CLOSED));
        s_app.game_hwnd = NULL;
        DoFindGame();
        if (!s_app.game_hwnd) return;
    }

    /* Try DLL Hook first (works for both Steam and Store versions) */
    if (HookMgr_GetState() != HOOK_STATE_ERROR) {
        LOG_I(L"Installing DLL Hook...");
        if (HookMgr_Attach(s_app.game_hwnd)) {
            LOG_I(L"%s", I18n_Get(STR_LOG_HOOK_INSTALLED));
            return;
        }
        LOG_W(L"DLL Hook failed: %s, trying message replay...", HookMgr_GetLastError());
    }

    /* Fallback: message replay (works without DLL injection) */
    LOG_I(L"%s (interval: %lu ms)", I18n_Get(STR_LOG_REPLAY_STARTED),
        s_app.settings.replay_interval);
    if (MsgReplay_Start(s_app.game_hwnd, s_app.settings.replay_interval)) {
        s_app.hook_active = TRUE;
        OnHookStateChanged(HOOK_STATE_ACTIVE, L"");
    } else {
        LOG_E(L"%s", I18n_Get(STR_LOG_HOOK_FAILED));
    }
}

static void DoDisableHook(void)
{
    if (!s_app.hook_active) return;

    /* Stop auto race if running */
    if (RaceCtrl_IsRunning(&s_race_ctrl)) {
        RaceCtrl_Stop(&s_race_ctrl);
        Gui_SetRaceRunning(FALSE);
        LOG_I(L"%s", I18n_Get(STR_LOG_RACE_STOPPED));
    }

    if (MsgReplay_IsActive()) {
        MsgReplay_Stop();
        LOG_I(L"%s", I18n_Get(STR_LOG_REPLAY_STOPPED));
    }

    if (HookMgr_GetState() == HOOK_STATE_ACTIVE) {
        HookMgr_Detach();
        LOG_I(L"%s", I18n_Get(STR_LOG_HOOK_REMOVED));
    }

    s_app.hook_active = FALSE;
    OnHookStateChanged(HOOK_STATE_IDLE, L"");

    /* Restore normal power state */
    SetThreadExecutionState(ES_CONTINUOUS);
}

static void DoToggleHook(void)
{
    if (s_app.hook_active) {
        DoDisableHook();
    } else {
        DoEnableHook();
    }
    Gui_UpdateButtons(s_app.hook_active, s_app.game_muted);
}

static void DoMuteToggle(void)
{
    if (!s_app.game_pid) {
        DoFindGame();
        if (!s_app.game_pid) {
            LOG_W(L"Cannot mute: game not found");
            return;
        }
    }

    if (s_app.game_muted) {
        AudioCtrl_MuteProcess(s_app.game_pid, FALSE);
        s_app.game_muted = FALSE;
        LOG_I(L"%s", I18n_Get(STR_LOG_UNMUTED));
    } else {
        AudioCtrl_MuteProcess(s_app.game_pid, TRUE);
        s_app.game_muted = TRUE;
        LOG_I(L"%s", I18n_Get(STR_LOG_MUTED));
    }
    Gui_UpdateButtons(s_app.hook_active, s_app.game_muted);
}

static void DoRefreshWindowList(void)
{
    FindResult result;
    WinFinder_EnumAll(&result);
    Gui_RefreshWindowList(&result);
    s_app.last_find_result = result;
    LOG_D(L"Window list refreshed, found %d windows", result.count);
}

static void DoSelectWindow(int index)
{
    (void)index;
    LOG_W(L"Please select a window from the list");
}

static void DoSaveSettings(void)
{
    Gui_ReadSettings(&s_app.settings);

    /* Apply language change at runtime */
    I18n_SetLanguage((Language)s_app.settings.language);
    Gui_RefreshLanguage(s_app.hook_active, s_app.game_muted);

    /* Apply prevent_sleep change immediately if hook is active */
    if (s_app.hook_active) {
        if (s_app.settings.prevent_sleep) {
            SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED);
        } else {
            SetThreadExecutionState(ES_CONTINUOUS);
        }
    }

    if (Settings_Save(&s_app.settings)) {
        LOG_I(L"%s", I18n_Get(STR_LOG_SETTINGS_SAVED));
    } else {
        LOG_E(L"Settings save failed");
    }
}

static void DoStartAutoRace(void)
{
    if (!s_app.hook_active) {
        /* Auto-enable anti-pause if not active */
        DoEnableHook();
        if (!s_app.hook_active) {
            LOG_W(L"Cannot enable anti-pause, auto race aborted");
            return;
        }
    }
    if (RaceCtrl_IsRunning(&s_race_ctrl)) return;

    /* Always reload profile from disk at start (user may have edited INI) */
    {
        WCHAR sel_name[PROFILE_NAME_LEN] = {0};
        Gui_GetSelectedProfile(sel_name, PROFILE_NAME_LEN);

        if (sel_name[0] == L'\0') {
            WCHAR names[PROFILE_MAX_PROFILES][PROFILE_NAME_LEN];
            int count = Profile_Enumerate(names, PROFILE_MAX_PROFILES);
            if (count > 0)
                wcsncpy(sel_name, names[0], PROFILE_NAME_LEN - 1);
        }

        if (sel_name[0] == L'\0') {
            LOG_E(L"No profile available");
            return;
        }

        if (!RaceCtrl_LoadProfile(&s_race_ctrl, sel_name)) {
            LOG_E(L"Failed to load profile: %s", sel_name);
            return;
        }
    }

    if (RaceCtrl_Start(&s_race_ctrl, s_app.game_hwnd)) {
        Gui_SetRaceRunning(TRUE);
        LOG_I(L"%s", I18n_Get(STR_LOG_RACE_STARTED));
    } else {
        LOG_E(L"Auto race start failed");
    }
}

static void DoStopAutoRace(void)
{
    if (!RaceCtrl_IsRunning(&s_race_ctrl)) return;

    RaceCtrl_Stop(&s_race_ctrl);
    Gui_SetRaceRunning(FALSE);
    Gui_UpdateRaceStatus(I18n_Get(STR_RACE_STATUS_IDLE), NULL, 0, 0);
    LOG_I(L"%s", I18n_Get(STR_LOG_RACE_STOPPED));
}

static void DoToggleAutoRace(void)
{
    if (RaceCtrl_IsRunning(&s_race_ctrl)) {
        DoStopAutoRace();
    } else {
        DoStartAutoRace();
    }
}

static void UpdateStatsDisplay(void)
{
    LONG kf = 0, aa = 0, nc = 0, ac = 0;

    if (HookMgr_GetState() == HOOK_STATE_ACTIVE) {
        HookMgr_GetStats(&kf, &aa, &nc, &ac);
    }

    Gui_UpdateStats(kf, aa, nc, ac);

    /* Also check if the game window is still alive */
    if (s_app.game_hwnd && !IsWindow(s_app.game_hwnd)) {
        LOG_W(L"%s", I18n_Get(STR_LOG_WINDOW_CLOSED));
        DoDisableHook();
        s_app.game_hwnd = NULL;
        s_app.game_pid = 0;
        s_app.game_title[0] = L'\0';
        Gui_UpdateStatus(FALSE, NULL, NULL, NULL, 0);
    }
}

/* ─── Entry Point ─────────────────────────────────────────────────── */

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow)
{
    MSG msg;
    HWND hwnd_main;
    GuiContext gui_ctx;

    (void)hPrevInstance;
    (void)lpCmdLine;

    /* Prevent multiple instances */
    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"FH6FocusKeeper_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(NULL, L"FH6 FocusKeeper is already running.", L"Info", MB_ICONINFORMATION);
        return 0;
    }

    /* Load settings */
    Settings_Load(&s_app.settings);

    /* Initialize i18n */
    I18n_Init((Language)s_app.settings.language);

    /* Initialize logger */
    Logger_Init(s_app.settings.log_to_file ? L"FocusKeeper.log" : NULL);
    Logger_AddCallback(OnLogEntry, NULL);

    LOG_I(L"%s", I18n_Get(STR_LOG_STARTED));
    LOG_I(L"Version: 1.0.0");

    /* Initialize audio control */
    AudioCtrl_Init();

    /* Initialize hook manager */
    if (!HookMgr_Init()) {
        LOG_E(L"Hook init failed: %s", HookMgr_GetLastError());
        LOG_W(L"%s", I18n_Get(STR_LOG_DLL_UNAVAILABLE));
    }
    HookMgr_SetCallback(OnHookStateChanged);

    /* Initialize window finder */
    WinFinder_Init();

    /* Create GUI */
    gui_ctx.hInstance = hInstance;
    gui_ctx.settings = &s_app.settings;
    hwnd_main = Gui_Create(&gui_ctx);

    if (!hwnd_main) {
        LOG_E(L"GUI creation failed");
        MessageBoxW(NULL, L"GUI creation failed", L"Error", MB_ICONERROR);
        goto cleanup;
    }

    /* Subclass main window to handle our commands */
    s_orig_main_proc = (WNDPROC)SetWindowLongPtr(hwnd_main, GWLP_WNDPROC, (LONG_PTR)AppWndProc);

    /* Create tray icon */
    Tray_Create(hwnd_main, hInstance);
    Tray_SetState(TRAY_STATE_IDLE);

    /* Register global hotkey */
    if (!RegisterHotKey(hwnd_main, IDH_TOGGLE,
                        s_app.settings.hotkey_modifiers, s_app.settings.hotkey_vk)) {
        LOG_W(L"%s", I18n_Get(STR_LOG_HOTKEY_FAILED));
    } else {
        LOG_I(L"%s", I18n_Get(STR_LOG_HOTKEY_REGISTERED));
    }

    /* Register auto-race hotkey */
    RegisterHotKey(hwnd_main, IDH_AUTO_RACE,
                   s_app.settings.race_hotkey_mod, s_app.settings.race_hotkey_vk);

    /* Initialize race controller with callback */
    if (!RaceCtrl_Init(&s_race_ctrl, OnAutoRaceStateChanged, NULL)) {
        LOG_W(L"Race controller init failed");
    }

    /* Load race profiles list into combo */
    {
        WCHAR profile_names[PROFILE_MAX_PROFILES][PROFILE_NAME_LEN];
        int count = Profile_Enumerate(profile_names, PROFILE_MAX_PROFILES);
        if (count == 0) {
            /* Generate default template if no profiles exist */
            WCHAR def_path[MAX_PATH];
            _snwprintf(def_path, MAX_PATH, L"%s%s",
                       Profile_GetDirectory(), L"170516901.ini");
            Profile_WriteDefaultTemplate(def_path);
            count = Profile_Enumerate(profile_names, PROFILE_MAX_PROFILES);
        }
        if (count > 0) {
            Gui_PopulateProfiles(profile_names, count);
        }
    }

    /* Set up timers */
    SetTimer(hwnd_main, IDT_STATS_UPDATE, 500, NULL);

    /* Check for new version in background */
    VersionCheck_Start(hwnd_main, WM_VERSION_CHECK_DONE);

    if (s_app.settings.auto_find) {
        SetTimer(hwnd_main, IDT_AUTO_FIND, 3000, NULL);
    }

    /* Show window (unless start-minimized is set) */
    if (s_app.settings.start_minimized) {
        ShowWindow(hwnd_main, SW_HIDE);
    } else {
        ShowWindow(hwnd_main, nCmdShow);
        UpdateWindow(hwnd_main);
    }

    /* Auto-find game on startup */
    if (s_app.settings.auto_find) {
        DoFindGame();
    }

    LOG_I(L"%s", I18n_Get(STR_LOG_INIT_DONE));

    /* Message loop */
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

cleanup:
    /* Shutdown race controller */
    RaceCtrl_Shutdown(&s_race_ctrl);

    if (s_app.game_muted && s_app.game_pid) {
        AudioCtrl_MuteProcess(s_app.game_pid, FALSE);
    }
    AudioCtrl_Shutdown();
    HookMgr_Shutdown();
    MsgReplay_Stop();

    /* Restore normal power state */
    SetThreadExecutionState(ES_CONTINUOUS);

    Logger_Shutdown();

    if (hMutex) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }

    return (int)msg.wParam;
}
