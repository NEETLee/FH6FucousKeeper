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
#include <string.h>
#include "gui.h"
#include "tray.h"
#include "hook_manager.h"
#include "window_finder.h"
#include "logger.h"
#include "settings.h"
#include "audio_control.h"
#include "i18n.h"
#include "resource.h"
#include "race_controller.h"
#include "race_profile.h"
#include "version_check.h"

#ifdef USE_FARM
#include "farm_pipeline.h"
#include "screen_capture.h"
#include "template_match.h"
#include "car_profile.h"
#endif

#include <stdlib.h>

#define WM_VERSION_CHECK_DONE (WM_APP + 100)

#ifdef FK_DEBUG
/* Polls for <exedir>/debug/stop.flag so the dev build pipeline can ask a running
 * instance to exit cleanly (releasing FocusKeeper.exe for relinking). Paths are
 * resolved relative to the EXE (not CWD), because an elevated launch frequently
 * starts with CWD=System32. */
#define IDT_DEBUG_WATCH 0xDB60
static char s_debug_dir[MAX_PATH];   /* "<exedir>\debug"          */
static char s_stop_flag[MAX_PATH];   /* "<exedir>\debug\stop.flag" */
static char s_debug_cmd[MAX_PATH];   /* "<exedir>\debug\cmd.txt"    */
static char s_debug_ack[MAX_PATH];   /* "<exedir>\debug\cmd.ack"    */
static char s_debug_status[MAX_PATH];/* "<exedir>\debug\status.txt" */
#endif

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

#ifdef USE_FARM
/* ─── Auto Wheelspin Farm pipeline integration ───────────────────── */
static FarmPipeline *s_pipeline   = NULL;
static HANDLE        s_pipe_thread = NULL;
static BOOL          s_capture_ready = FALSE;
static CarProfile    s_cars[CAR_PROFILE_MAX];
static int           s_car_count = 0;
static BOOL          s_pipe_was_running = FALSE;

typedef struct { PipelineStep step; int count; BOOL full_loop; } PipeJob;

static void DoStartAutoRace(void);  /* fwd (already declared below too) */
static void DoStopAutoRace(void);
static const char *ResolveAssetsDir(void);

/* Farm engine frame grab: pump a few WGC frames then hand the latest to TM. */
static BOOL FarmGrab(void *ctx) {
    (void)ctx;
    CaptureFrame last = {0};
    BOOL got = FALSE;
    for (int i = 0; i < 8; i++) {
        CaptureFrame f = {0};
        if (ScreenCapture_GrabFrame(&f)) { last = f; got = TRUE; }
        Sleep(45);
    }
    if (!got || !last.pixels) return FALSE;
    TM_SetFrame(last.pixels, last.width, last.height, last.stride);
    return TRUE;
}

static void FarmLog(const char *msg, void *ctx) {
    (void)ctx;
    /* Detailed per-step farm logs go to the Auto Farm tab's step log box.
     * High-level milestones are emitted separately to the global Log tab. */
    WCHAR w[512];
    MultiByteToWideChar(CP_UTF8, 0, msg, -1, w, 512);
    Gui_AppendPipelineLog(w);
#ifdef FK_DEBUG
    /* DEBUG: also persist every step log to FocusKeeper.log (file sink only, so
     * it does not pollute the global GUI log tab) for offline inspection. */
    Logger_LogFileOnly(LOG_INFO, L"[farm] %s", w);
#endif
}

/* Selected car profile (or NULL when none loaded). */
static const CarProfile *SelectedCar(void) {
    int idx = Gui_GetSelectedCarProfile();
    if (idx < 0 || idx >= s_car_count) idx = 0;
    if (s_car_count <= 0) return NULL;
    return &s_cars[idx];
}

/* SP-per-lap is stored in the selected race profile's [Profile] section. */
static int RaceProfileSpPerLap(void) {
    WCHAR sel[PROFILE_NAME_LEN] = {0};
    Gui_GetSelectedProfile(sel, PROFILE_NAME_LEN);
    if (!sel[0]) return 0;
    WCHAR full[MAX_PATH];
    _snwprintf(full, MAX_PATH, L"%s%s", Profile_GetDirectory(), sel);
    return (int)GetPrivateProfileIntW(L"Profile", L"SPPerLap", 0, full);
}

/* Farming car identity for OCR-based selection, from the race profile's
 * [Profile] CarName / CarPI keys. Falls back to the selected car profile's
 * display name when CarName is absent, so existing profiles keep working. */
static void RaceProfileCarName(WCHAR *out, int out_len) {
    if (!out || out_len <= 0) return;
    out[0] = L'\0';
    WCHAR sel[PROFILE_NAME_LEN] = {0};
    Gui_GetSelectedProfile(sel, PROFILE_NAME_LEN);
    if (sel[0]) {
        WCHAR full[MAX_PATH];
        _snwprintf(full, MAX_PATH, L"%s%s", Profile_GetDirectory(), sel);
        GetPrivateProfileStringW(L"Profile", L"CarName", L"", out, out_len, full);
    }
    if (!out[0]) {
        const CarProfile *car = SelectedCar();
        if (car) wcsncpy(out, car->name, out_len - 1);
    }
}

static int RaceProfileCarPI(void) {
    WCHAR sel[PROFILE_NAME_LEN] = {0};
    Gui_GetSelectedProfile(sel, PROFILE_NAME_LEN);
    if (!sel[0]) return 0;
    WCHAR full[MAX_PATH];
    _snwprintf(full, MAX_PATH, L"%s%s", Profile_GetDirectory(), sel);
    return (int)GetPrivateProfileIntW(L"Profile", L"CarPI", 0, full);
}

/* EventLab share code for the vision race step (UTF-8, digits only).
 * Source priority: the selected race profile's [Profile] ShareCode key; if that
 * is empty, the profile file name itself (the .ini is named after the code). */
static void RaceProfileShareCode(char *out, int out_sz) {
    if (!out || out_sz <= 0) return;
    out[0] = '\0';
    WCHAR sel[PROFILE_NAME_LEN] = {0};
    Gui_GetSelectedProfile(sel, PROFILE_NAME_LEN);
    if (!sel[0]) return;

    WCHAR full[MAX_PATH];
    _snwprintf(full, MAX_PATH, L"%s%s", Profile_GetDirectory(), sel);
    WCHAR code[64] = {0};
    GetPrivateProfileStringW(L"Profile", L"ShareCode", L"", code, 64, full);

    /* Fallback: strip a trailing ".ini" from the profile file name. */
    if (!code[0]) {
        wcsncpy(code, sel, 63);
        size_t n = wcslen(code);
        if (n > 4 && _wcsicmp(code + n - 4, L".ini") == 0)
            code[n - 4] = L'\0';
    }

    /* Keep only digits so a stray label can't break UIA entry. */
    char raw[64] = {0};
    WideCharToMultiByte(CP_UTF8, 0, code, -1, raw, sizeof(raw), NULL, NULL);
    int j = 0;
    for (int i = 0; raw[i] && j < out_sz - 1; i++)
        if (raw[i] >= '0' && raw[i] <= '9') out[j++] = raw[i];
    out[j] = '\0';
}

/* Map the app UI language to an OCR BCP-47 tag for digit recognition. */
static const WCHAR *OcrLangForSettings(void) {
    switch ((Language)s_app.settings.language) {
    case LANG_ZH:    return L"zh-Hans-CN";
    case LANG_ZH_TW: return L"zh-Hant-TW";
    case LANG_EN:    return L"en-US";
    default: { /* AUTO: follow detected system language */
        Language d = I18n_DetectSystem();
        if (d == LANG_ZH)    return L"zh-Hans-CN";
        if (d == LANG_ZH_TW) return L"zh-Hant-TW";
        return L"en-US";
    }
    }
}

/* Race-step callbacks: reuse the existing timed-script RaceController. */
static BOOL PipeRaceStart(void *ctx)    { (void)ctx; DoStartAutoRace(); return RaceCtrl_IsRunning(&s_race_ctrl); }
static void PipeRaceStop(void *ctx)     { (void)ctx; DoStopAutoRace(); }
static BOOL PipeRaceRunning(void *ctx)  { (void)ctx; return RaceCtrl_IsRunning(&s_race_ctrl); }
static int  PipeRaceGetLaps(void *ctx)  {
    (void)ctx;
    AutoRaceStatus st = {0};
    RaceCtrl_GetStatus(&s_race_ctrl, &st);
    return st.lap_count;
}

/* Ensure WGC capture + template matching are initialised on the game window. */
static BOOL EnsureCaptureReady(void) {
    if (!s_app.game_hwnd) {
        DoFindGame();
        if (!s_app.game_hwnd) { LOG_E(L"Pipeline: game window not found"); return FALSE; }
    }
    if (!s_capture_ready) {
        if (!ScreenCapture_Init()) { LOG_E(L"Pipeline: capture init failed"); return FALSE; }
        TM_Init();
        s_capture_ready = TRUE;
    }
    if (!ScreenCapture_IsActive()) {
        if (!ScreenCapture_StartCapture(s_app.game_hwnd)) {
            LOG_E(L"Pipeline: start capture failed"); return FALSE;
        }
        Sleep(300);
    }
    return TRUE;
}

static void BuildPipelineConfig(PipelineConfig *cfg) {
    GuiPipelineParams p = {0};
    Gui_GetPipelineParams(&p);
    memset(cfg, 0, sizeof(*cfg));
    cfg->enable_race      = p.enable_race;
    cfg->enable_buy_car   = p.enable_buy;
    cfg->enable_wheelspin = p.enable_spin;
    cfg->enable_remove_car= p.enable_remove;
    cfg->wheelspin_mode   = 2;
    cfg->remove_mode      = 1;
    cfg->auto_count       = p.auto_count;
    cfg->buy_car_count    = p.manual_count;
    cfg->wheelspin_count  = p.manual_count;
    cfg->remove_car_count = p.manual_count;
    cfg->max_cycles       = p.cycles;
    cfg->race_target_laps = p.race_laps;
    cfg->target_sp        = p.target_sp;
    cfg->consecutive_fail_max = 5;

    /* CR/SP cost come from the car profile; SP-per-lap from the race profile. */
    const CarProfile *car = SelectedCar();
    cfg->cost_per_car = car ? car->cost_cr : 81700;
    cfg->sp_per_car   = car ? car->cost_sp : 30;
    cfg->sp_per_lap   = RaceProfileSpPerLap();
    RaceProfileShareCode(cfg->share_code, (int)sizeof(cfg->share_code));

    wcsncpy(cfg->ocr_lang, OcrLangForSettings(), 15);
    /* Legacy timed-script callbacks (the vision race step uses Farm_Race). */
    cfg->race_start   = PipeRaceStart;
    cfg->race_stop    = PipeRaceStop;
    cfg->race_get_laps= PipeRaceGetLaps;
    cfg->race_running = PipeRaceRunning;
}

static DWORD WINAPI PipeThreadProc(LPVOID arg) {
    PipeJob *job = (PipeJob*)arg;
    if (job->full_loop)
        Pipeline_Run(s_pipeline);
    else
        Pipeline_RunStep(s_pipeline, job->step, job->count);
    free(job);
    return 0;
}

static void StartPipelineJob(BOOL full_loop, PipelineStep step, int count) {
    if (s_pipeline && Pipeline_IsRunning(s_pipeline)) {
        LOG_W(L"Pipeline already running");
        return;
    }
    if (!EnsureCaptureReady()) {
        Gui_SetPipelineEcon(-1, -1, -1,
            I18n_Get(STR_PIPE_STATUS_ERROR), I18n_Get(STR_PIPE_ERR_NO_GAME));
        return;
    }

    const char *assets = ResolveAssetsDir();
    char probe[MAX_PATH];
    snprintf(probe, sizeof(probe), "%s/collectionjournal.png", assets);
    if (GetFileAttributesA(probe) == INVALID_FILE_ATTRIBUTES) {
        Gui_SetPipelineEcon(-1, -1, -1,
            I18n_Get(STR_PIPE_STATUS_ERROR), I18n_Get(STR_PIPE_ERR_NO_ASSETS));
        LOG_E(L"Pipeline: assets not found at %hs", assets);
        return;
    }

    if (s_pipe_thread) { CloseHandle(s_pipe_thread); s_pipe_thread = NULL; }
    if (!s_pipeline) s_pipeline = Pipeline_Create();

    PipelineConfig cfg;
    BuildPipelineConfig(&cfg);

    FarmConfig fc = {0};
    fc.game_hwnd  = s_app.game_hwnd;
    fc.assets_dir = assets;
    fc.log_func   = FarmLog;
    fc.grab_func  = FarmGrab;
    const CarProfile *car = SelectedCar();
    if (car) {
        fc.car_assets_dir = car->dir;
        fc.skill_count = car->skill_count;
        for (int i = 0; i < car->skill_count && i < 16; i++)
            fc.skill_dirs[i] = car->skill_dirs[i];
    }
    RaceProfileCarName(fc.car_name, 64);
    fc.car_pi = RaceProfileCarPI();
    if (!Pipeline_Init(s_pipeline, &cfg, &fc)) {
        LOG_E(L"Pipeline init failed");
        Gui_SetPipelineEcon(-1, -1, -1,
            I18n_Get(STR_PIPE_STATUS_ERROR), I18n_Get(STR_PIPE_ERR_INIT));
        return;
    }

    PipeJob *job = (PipeJob*)calloc(1, sizeof(PipeJob));
    job->full_loop = full_loop;
    job->step = step;
    job->count = count;

    LOG_I(L"%s", full_loop ? L"[farm] \u6d41\u6c34\u7ebf\u5b8c\u6574\u5faa\u73af\u542f\u52a8"
                           : L"[farm] \u6d41\u6c34\u7ebf\u5355\u6b65\u542f\u52a8");
    s_pipe_was_running = TRUE;
    Gui_SetPipelineEcon(-1, -1, -1, I18n_Get(STR_PIPE_STAGE_RUNNING), L"");
    Gui_SetPipelineRunning(TRUE);
    s_pipe_thread = CreateThread(NULL, 0, PipeThreadProc, job, 0, NULL);
    if (!s_pipe_thread) {
        LOG_E(L"Pipeline thread failed");
        free(job);
        Gui_SetPipelineRunning(FALSE);
        Gui_SetPipelineEcon(-1, -1, -1,
            I18n_Get(STR_PIPE_STATUS_ERROR), I18n_Get(STR_PIPE_ERR_THREAD));
    }
}

/* Resolve assets/templates relative to the exe dir, falling back to cwd. */
static const char *ResolveAssetsDir(void) {
    static char path[MAX_PATH];
    WCHAR exe[MAX_PATH];
    if (GetModuleFileNameW(NULL, exe, MAX_PATH)) {
        WCHAR *slash = wcsrchr(exe, L'\\');
        if (slash) *slash = 0;
        WCHAR wpath[MAX_PATH];
        _snwprintf(wpath, MAX_PATH, L"%s\\assets\\templates", exe);
        if (GetFileAttributesW(wpath) != INVALID_FILE_ATTRIBUTES) {
            WideCharToMultiByte(CP_UTF8, 0, wpath, -1, path, MAX_PATH, NULL, NULL);
            return path;
        }
    }
    return "assets/templates";
}

static int PipeManualCount(void) {
    GuiPipelineParams p = {0};
    Gui_GetPipelineParams(&p);
    return p.manual_count;
}

static void DoPipelineStop(void) {
    if (s_pipeline) Pipeline_Stop(s_pipeline);
}

static const WCHAR *PipelineStageLabel(const char *step) {
    if (!step || !step[0]) return L"-";
    if (strcmp(step, "read_econ") == 0)    return I18n_Get(STR_PIPE_STAGE_READ);
    if (strcmp(step, "buy_car") == 0)      return I18n_Get(STR_PIPE_STAGE_BUY);
    if (strcmp(step, "wheelspin") == 0)    return I18n_Get(STR_PIPE_STAGE_SPIN);
    if (strcmp(step, "remove_car") == 0)   return I18n_Get(STR_PIPE_STAGE_REMOVE);
    if (strcmp(step, "race") == 0)         return I18n_Get(STR_PIPE_STAGE_RACE);
    if (strcmp(step, "step_done") == 0)    return I18n_Get(STR_PIPE_STAGE_STEP_DONE);
    if (strcmp(step, "all_done") == 0)     return I18n_Get(STR_PIPE_STAGE_ALL_DONE);
    if (strcmp(step, "init_failed") == 0)  return I18n_Get(STR_PIPE_STAGE_INIT_FAIL);
    if (strcmp(step, "running") == 0)      return I18n_Get(STR_PIPE_STAGE_RUNNING);
    return I18n_Get(STR_PIPE_STAGE_IDLE);
}

static void UpdatePipelineDisplay(void) {
    if (!s_pipeline) return;
    PipelineStatus st = Pipeline_GetStatus(s_pipeline);
    WCHAR totals[160];
    const WCHAR *stage = PipelineStageLabel(st.current_step);
    _snwprintf(totals, 160, I18n_Get(STR_PIPE_TOTALS_FMT),
        st.total_bought, st.total_wheelspins,
        st.total_removed, st.total_races);
    Gui_SetPipelineEcon(st.last_balance, st.last_skill_points,
                        st.last_computed_count, stage, totals);

    /* re-enable buttons when the worker finished */
    if (!Pipeline_IsRunning(s_pipeline) && s_pipe_thread) {
        if (WaitForSingleObject(s_pipe_thread, 0) == WAIT_OBJECT_0) {
            CloseHandle(s_pipe_thread);
            s_pipe_thread = NULL;
            Gui_SetPipelineRunning(FALSE);
            /* Pipeline is idle now: stop WGC capture so the yellow capture
             * border around the game window disappears and the user knows it's
             * safe to take over. Next run re-arms it via EnsureCaptureReady(). */
            ScreenCapture_StopCapture();
            if (s_pipe_was_running) {
                /* Milestone summary -> global Log tab. */
                LOG_I(L"[farm] \u6d41\u6c34\u7ebf\u7ed3\u675f\uff1a\u4e70%d \u62bd%d \u5220%d \u8d5b%d",
                    st.total_bought, st.total_wheelspins,
                    st.total_removed, st.total_races);
                s_pipe_was_running = FALSE;
            }
        }
    }
}

#ifdef FK_DEBUG
/* Debug remote-control channel. The agent (or dev) writes ONE command line to
 * <exedir>\debug\cmd.txt; we run it on the GUI thread, write the outcome to
 * cmd.ack, then delete cmd.txt. This lets the pipeline be driven without
 * clicking the GUI. Supported verbs:
 *   race | read | loop | stop | quit
 *   buy [n] | spin [n] | remove [n]   (n optional; falls back to GUI count)
 * Only present in FK_DEBUG builds. */
static void DebugPollCommand(HWND hwnd) {
    if (!s_debug_cmd[0]) return;
    if (GetFileAttributesA(s_debug_cmd) == INVALID_FILE_ATTRIBUTES) return;

    char line[256] = {0};
    FILE *f = fopen(s_debug_cmd, "rb");
    if (f) { if (!fgets(line, sizeof(line), f)) line[0] = 0; fclose(f); }
    DeleteFileA(s_debug_cmd);

    size_t n = strlen(line);
    while (n > 0 && (line[n-1]=='\r'||line[n-1]=='\n'||line[n-1]==' '||line[n-1]=='\t'))
        line[--n] = 0;
    if (n == 0) return;

    char verb[32] = {0};
    int arg = -1;
    sscanf(line, "%31s %d", verb, &arg);

    const char *result = "ok";
    if      (strcmp(verb,"race")==0)   StartPipelineJob(FALSE, PIPE_STEP_RACE, 0);
    else if (strcmp(verb,"read")==0)   StartPipelineJob(FALSE, PIPE_STEP_READ_ECON, 0);
    else if (strcmp(verb,"buy")==0)    StartPipelineJob(FALSE, PIPE_STEP_BUY,    arg>0?arg:PipeManualCount());
    else if (strcmp(verb,"spin")==0)   StartPipelineJob(FALSE, PIPE_STEP_SPIN,   arg>0?arg:PipeManualCount());
    else if (strcmp(verb,"remove")==0) StartPipelineJob(FALSE, PIPE_STEP_REMOVE, arg>0?arg:PipeManualCount());
    else if (strcmp(verb,"loop")==0)   StartPipelineJob(TRUE,  PIPE_STEP_RACE, 0);
    else if (strcmp(verb,"stop")==0)   DoPipelineStop();
    else if (strcmp(verb,"quit")==0)   { result="quitting"; }
    else if (strcmp(verb,"status")==0) {
        /* Dump structured pipeline state for the regression harness to assert on
         * (deterministic, vs. parsing localized log lines). */
        int running = (s_pipeline && Pipeline_IsRunning(s_pipeline)) ? 1 : 0;
        PipelineStatus st = {0};
        if (s_pipeline) st = Pipeline_GetStatus(s_pipeline);
        if (s_debug_status[0]) {
            FILE *s = fopen(s_debug_status, "wb");
            if (s) {
                fprintf(s, "running=%d\nbought=%d\nspins=%d\nremoved=%d\nraces=%d\n"
                           "balance=%d\nsp=%d\ncomputed=%d\nstage=%s\n",
                        running, st.total_bought, st.total_wheelspins,
                        st.total_removed, st.total_races, st.last_balance,
                        st.last_skill_points, st.last_computed_count,
                        st.current_step ? st.current_step : "-");
                fclose(s);
            }
        }
    }
    else                               result="unknown";

    LOG_I(L"[FK_DEBUG] cmd '%hs' -> %hs", line, result);
    if (s_debug_ack[0]) {
        FILE *a = fopen(s_debug_ack, "wb");
        if (a) { fprintf(a, "%s | %s\n", line, result); fclose(a); }
    }
    if (strcmp(verb,"quit")==0) DestroyWindow(hwnd);
}
#endif /* FK_DEBUG */
#endif /* USE_FARM */

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
            Gui_ClearLog();
            return 0;
        case IDC_BTN_SAVE:      DoSaveSettings(); return 0;

        case IDC_BTN_RACE_START: DoStartAutoRace(); return 0;
        case IDC_BTN_RACE_STOP:  DoStopAutoRace(); return 0;

#ifdef USE_FARM
        case IDC_PIPE_BTN_RACE:   StartPipelineJob(FALSE, PIPE_STEP_RACE, 0); return 0;
        case IDC_PIPE_BTN_READ:   StartPipelineJob(FALSE, PIPE_STEP_READ_ECON, 0); return 0;
        case IDC_PIPE_BTN_BUY:    StartPipelineJob(FALSE, PIPE_STEP_BUY, PipeManualCount()); return 0;
        case IDC_PIPE_BTN_SPIN:   StartPipelineJob(FALSE, PIPE_STEP_SPIN, PipeManualCount()); return 0;
        case IDC_PIPE_BTN_REMOVE: StartPipelineJob(FALSE, PIPE_STEP_REMOVE, PipeManualCount()); return 0;
        case IDC_PIPE_BTN_LOOP:   StartPipelineJob(TRUE, PIPE_STEP_RACE, 0); return 0;
        case IDC_PIPE_BTN_STOP:   DoPipelineStop(); return 0;
#endif

        case IDC_COMBO_PROFILE:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                WCHAR sel[MAX_PATH];
                Gui_GetSelectedProfile(sel, MAX_PATH);
                if (sel[0]) {
                    WCHAR full_path[MAX_PATH];
                    _snwprintf(full_path, MAX_PATH, L"%s%s", Profile_GetDirectory(), sel);
                    WCHAR *comments = Profile_ReadComments(full_path);
                    Gui_SetProfileDescription(comments);
                    if (comments) free(comments);
                }
            }
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
#ifdef USE_FARM
        else if (wParam == HOTKEY_ID_STOP) {
            DoPipelineStop();
        } else if (wParam == HOTKEY_ID_PAUSE && s_pipeline) {
            Pipeline_HandleHotkey(s_pipeline, HOTKEY_ID_PAUSE);
        }
#endif
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
#ifdef USE_FARM
            UpdatePipelineDisplay();
#endif
        } else if (wParam == IDT_AUTO_FIND) {
            if (!s_app.hook_active && s_app.game_hwnd == NULL) {
                DoFindGame();
            }
        }
#ifdef FK_DEBUG
        else if (wParam == IDT_DEBUG_WATCH) {
#ifdef USE_FARM
            DebugPollCommand(hwnd);
#endif
            if (s_stop_flag[0] &&
                GetFileAttributesA(s_stop_flag) != INVALID_FILE_ATTRIBUTES) {
                DeleteFileA(s_stop_flag);
                LOG_I(L"[FK_DEBUG] stop.flag detected -> exiting");
                DestroyWindow(hwnd);
            }
        }
#endif
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
#ifdef FK_DEBUG
        KillTimer(hwnd, IDT_DEBUG_WATCH);
#endif
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
        LOG_E(L"DLL Hook failed: %s", HookMgr_GetLastError());
    }

    LOG_E(L"%s", I18n_Get(STR_LOG_HOOK_FAILED));
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

        /* Display profile comments in the description area */
        {
            WCHAR full_path[MAX_PATH];
            _snwprintf(full_path, MAX_PATH, L"%s%s", Profile_GetDirectory(), sel_name);
            WCHAR *comments = Profile_ReadComments(full_path);
            Gui_SetProfileDescription(comments);
            if (comments) free(comments);
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

    /* Initialize logger. Resolve the log path next to the EXE (not CWD) so an
     * elevated launch (whose CWD is often System32) still writes a findable
     * FocusKeeper.log right beside the executable. */
    WCHAR log_path[MAX_PATH];
    {
        WCHAR exe[MAX_PATH];
        DWORD n = GetModuleFileNameW(NULL, exe, MAX_PATH);
        if (n > 0 && n < MAX_PATH) {
            WCHAR *slash = wcsrchr(exe, L'\\');
            if (slash) *slash = L'\0';
            _snwprintf(log_path, MAX_PATH, L"%s\\FocusKeeper.log", exe);
            log_path[MAX_PATH - 1] = L'\0';
        } else {
            wcscpy(log_path, L"FocusKeeper.log");
        }
    }
    Logger_Init(s_app.settings.log_to_file ? log_path : NULL);
    Logger_AddCallback(OnLogEntry, NULL);

    LOG_I(L"%s", I18n_Get(STR_LOG_STARTED));
    LOG_I(L"Version: 1.0.0");

    /* Initialize audio control */
    AudioCtrl_Init();

    /* Initialize hook manager */
    if (!HookMgr_Init()) {
        LOG_E(L"Hook init failed: %s", HookMgr_GetLastError());
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

#ifdef USE_FARM
    /* Pipeline stop (F8) / pause (F9) */
    RegisterHotKey(hwnd_main, HOTKEY_ID_STOP, 0, VK_F8);
    RegisterHotKey(hwnd_main, HOTKEY_ID_PAUSE, 0, VK_F9);
#endif

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
            /* Show initial profile description */
            WCHAR full_path[MAX_PATH];
            _snwprintf(full_path, MAX_PATH, L"%s%s",
                       Profile_GetDirectory(), profile_names[0]);
            WCHAR *comments = Profile_ReadComments(full_path);
            Gui_SetProfileDescription(comments);
            if (comments) free(comments);
        }
    }

#ifdef USE_FARM
    /* Load car profiles (cost / SP / skill path / templates) into the combo. */
    {
        s_car_count = CarProfile_Scan(NULL, s_cars, CAR_PROFILE_MAX);
        if (s_car_count > 0) {
            WCHAR car_names[CAR_PROFILE_MAX][64];
            for (int i = 0; i < s_car_count; i++) {
                wcsncpy(car_names[i], s_cars[i].name, 63);
                car_names[i][63] = 0;
            }
            Gui_PopulateCarProfiles(car_names, s_car_count);
        }
    }
#endif

    /* Set up timers */
    SetTimer(hwnd_main, IDT_STATS_UPDATE, 500, NULL);

#if defined(FK_DEBUG) && defined(USE_FARM)
    /* DEBUG build: visual decision snapshots into <exedir>/debug, step logs
     * already routed to FocusKeeper.log, plus a stop.flag watcher for fast
     * relinking. Resolve paths relative to the EXE (CWD may be System32). */
    {
        char exedir[MAX_PATH];
        DWORD n = GetModuleFileNameA(NULL, exedir, MAX_PATH);
        if (n > 0 && n < MAX_PATH) {
            char *slash = strrchr(exedir, '\\');
            if (slash) *slash = '\0';
        } else {
            exedir[0] = '.'; exedir[1] = '\0';
        }
        snprintf(s_debug_dir, sizeof(s_debug_dir), "%s\\debug", exedir);
        snprintf(s_stop_flag, sizeof(s_stop_flag), "%s\\debug\\stop.flag", exedir);
        snprintf(s_debug_cmd, sizeof(s_debug_cmd), "%s\\debug\\cmd.txt", exedir);
        snprintf(s_debug_ack, sizeof(s_debug_ack), "%s\\debug\\cmd.ack", exedir);
        snprintf(s_debug_status, sizeof(s_debug_status), "%s\\debug\\status.txt", exedir);
        CreateDirectoryA(s_debug_dir, NULL);
        /* Clear any stale control files so a leftover flag/command from a prior
         * session can't immediately kill or misdrive this fresh launch. */
        DeleteFileA(s_stop_flag);
        DeleteFileA(s_debug_cmd);
        DeleteFileA(s_debug_ack);
        DeleteFileA(s_debug_status);
        TM_DebugSetDir(s_debug_dir);
        TM_DebugSetEnabled(1);
        LOG_I(L"[FK_DEBUG] debug build active: step logs -> FocusKeeper.log, snapshots -> %hs",
              s_debug_dir);
        LOG_I(L"[FK_DEBUG] command channel: write a verb to %hs\\cmd.txt (race/read/buy/spin/remove/loop/stop/quit/status)",
              s_debug_dir);
        SetTimer(hwnd_main, IDT_DEBUG_WATCH, 500, NULL);
    }
#endif

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
#ifdef USE_FARM
    if (s_pipeline) {
        Pipeline_Stop(s_pipeline);
        if (s_pipe_thread) {
            WaitForSingleObject(s_pipe_thread, 8000);
            CloseHandle(s_pipe_thread);
            s_pipe_thread = NULL;
        }
        Pipeline_Destroy(s_pipeline);
        s_pipeline = NULL;
    }
    if (s_capture_ready) {
        ScreenCapture_StopCapture();
        TM_Shutdown();
        ScreenCapture_Shutdown();
    }
#endif

    /* Shutdown race controller */
    RaceCtrl_Shutdown(&s_race_ctrl);

    if (s_app.game_muted && s_app.game_pid) {
        AudioCtrl_MuteProcess(s_app.game_pid, FALSE);
    }
    AudioCtrl_Shutdown();
    HookMgr_Shutdown();

    /* Restore normal power state */
    SetThreadExecutionState(ES_CONTINUOUS);

    Logger_Shutdown();

    if (hMutex) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }

    return (int)msg.wParam;
}
