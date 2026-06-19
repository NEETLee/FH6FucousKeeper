/*
 * farm_pipeline.h - Orchestration pipeline for the auto wheelspin farm
 *
 * One cycle: race (timed script, N laps) -> read CR/SP via OCR -> compute how
 * many 22B cars to process -> buy n -> wheelspin n (mode 2) -> remove n.
 *
 * Supports both single-step execution (GUI step buttons) and a full loop.
 * The race step reuses the existing timed auto-race script through host-
 * provided callbacks (so the GUI's profile selection + RaceController are
 * reused unchanged); the buy/spin/remove steps use farm_flow + game_input.
 */

#ifndef FOCUSKEEPER_FARM_PIPELINE_H
#define FOCUSKEEPER_FARM_PIPELINE_H

#include <windows.h>
#include "farm_flow.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FarmPipeline FarmPipeline;

/* Individual pipeline steps (for single-step execution). */
typedef enum {
    PIPE_STEP_RACE = 1,
    PIPE_STEP_READ_ECON,
    PIPE_STEP_BUY,
    PIPE_STEP_SPIN,
    PIPE_STEP_REMOVE
} PipelineStep;

/* Pipeline configuration */
typedef struct {
    /* Per-step enable + manual counts (used when auto_count is FALSE) */
    BOOL   enable_buy_car;
    int    buy_car_count;
    BOOL   enable_wheelspin;
    int    wheelspin_count;
    int    wheelspin_mode;       /* default 2 (from Design & Paint) */
    BOOL   enable_remove_car;
    int    remove_car_count;
    int    remove_mode;          /* default 1 (image-recognition / 22B) */
    BOOL   enable_race;

    int    max_cycles;           /* 0 = infinite loop */
    int    consecutive_fail_max; /* stop after N consecutive failures */

    /* Auto-count economy: n = min(CR/cost_per_car, SP/sp_per_car) */
    BOOL   auto_count;
    long   cost_per_car;         /* default 81700 */
    long   sp_per_car;           /* default 30 */
    int    max_cars_per_cycle;   /* upper cap on n per cycle (0 = uncapped) */
    WCHAR  ocr_lang[16];         /* default L"en-US" */

    /* Race-by-laps: drive the existing timed script via host callbacks.
     * race_get_laps returns the current lap_count; the pipeline stops the race
     * once it reaches the target lap count. */
    int    race_target_laps;     /* manual laps (fallback when sp_per_lap<=0) */

    /* SP-target auto laps: when sp_per_lap > 0, the race step reads current SP
     * first and runs ceil((target_sp - SP)/sp_per_lap) laps (SP cap is 999). */
    int    target_sp;            /* default 999 */
    int    sp_per_lap;           /* SP earned per lap; <=0 = use race_target_laps */

    /* EventLab share code for the vision-based race step (Farm_Race). When set,
     * the race step navigates to EventLab, submits this code via UIA, then runs
     * the computed number of races (each ends with an X-restart). */
    char   share_code[32];

    /* Legacy timed-script callbacks (unused by the vision race step; kept so the
     * old RaceController can still be driven elsewhere). */
    BOOL  (*race_start)(void *ctx);
    void  (*race_stop)(void *ctx);
    int   (*race_get_laps)(void *ctx);
    BOOL  (*race_running)(void *ctx);
    void  *race_ctx;
} PipelineConfig;

/* Status/stats for GUI display */
typedef struct {
    int    current_cycle;
    int    total_bought;
    int    total_wheelspins;
    int    total_removed;
    int    total_races;
    int    consecutive_fails;
    BOOL   running;
    BOOL   paused;

    /* Latest economy snapshot */
    int    last_balance;        /* CR, -1 if unread */
    int    last_skill_points;   /* SP, -1 if unread */
    int    last_computed_count; /* n derived this cycle */

    const char *current_step;   /* human-readable current action */
} PipelineStatus;

/* Create/destroy */
FarmPipeline* Pipeline_Create(void);
void          Pipeline_Destroy(FarmPipeline *pp);

/* Initialize with config and the same dependencies as FarmEngine.
 * Also initializes the OCR economy backend. */
BOOL Pipeline_Init(FarmPipeline *pp, const PipelineConfig *cfg,
                   const FarmConfig *farm_cfg);

/* Run the full loop (blocks - caller should spawn a thread). */
void Pipeline_Run(FarmPipeline *pp);

/* Run a single step once (blocks - caller should spawn a thread).
 * count <= 0 uses the configured/auto count for that step. */
void Pipeline_RunStep(FarmPipeline *pp, PipelineStep step, int count);

/* Read CR/SP now and recompute the count (updates status; returns count). */
int  Pipeline_ReadEconomy(FarmPipeline *pp);

/* Foundation read: navigate + capture + OCR, returning the raw CR/SP values
 * (either may be -1 if unreadable). Updates the status snapshot. Used by the
 * read step, the SP-target race lap calc, and the GUI "refresh CR/SP" button. */
BOOL Pipeline_ReadEconomyValues(FarmPipeline *pp, int *out_cr, int *out_sp);

/* Control (thread-safe) */
void Pipeline_Stop(FarmPipeline *pp);
void Pipeline_Pause(FarmPipeline *pp);
void Pipeline_Resume(FarmPipeline *pp);
BOOL Pipeline_IsPaused(FarmPipeline *pp);
BOOL Pipeline_IsRunning(FarmPipeline *pp);

/* Get status snapshot */
PipelineStatus Pipeline_GetStatus(FarmPipeline *pp);

/* Register global hotkeys (F8=stop, F9=pause/resume) on a window */
void Pipeline_RegisterHotkeys(FarmPipeline *pp, HWND owner_hwnd);
void Pipeline_UnregisterHotkeys(FarmPipeline *pp, HWND owner_hwnd);
void Pipeline_HandleHotkey(FarmPipeline *pp, int hotkey_id);

#define HOTKEY_ID_STOP  0x8001
#define HOTKEY_ID_PAUSE 0x8002

#ifdef __cplusplus
}
#endif

#endif /* FOCUSKEEPER_FARM_PIPELINE_H */
