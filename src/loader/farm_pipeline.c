/*
 * farm_pipeline.c - Auto wheelspin farm orchestration
 *
 * Sequences: race (timed script) -> read economy (OCR) -> compute count ->
 * buy -> wheelspin (mode 2) -> remove, as single steps or a full loop.
 */

#include "farm_pipeline.h"
#include "farm_flow.h"
#include "farm_economy.h"
#include "screen_capture.h"
#include "template_match.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct FarmPipeline {
    PipelineConfig cfg;
    FarmConfig     farm_cfg;
    FarmEngine    *engine;
    BOOL           ocr_ready;
    volatile BOOL  running;
    volatile BOOL  paused;
    volatile BOOL  stop_requested;
    PipelineStatus status;
    CRITICAL_SECTION cs;
};

/* ─── Helpers ────────────────────────────────────────────────────────── */

static void update_step(FarmPipeline *pp, const char *step) {
    EnterCriticalSection(&pp->cs);
    pp->status.current_step = step;
    pp->status.running = pp->running;
    pp->status.paused = pp->paused;
    LeaveCriticalSection(&pp->cs);
}

/* Per-unit progress from farm_flow (1 buy / 1 spin / 1 remove / 1 race). */
static void pipe_unit_done(FarmUnitKind unit, void *ctx) {
    FarmPipeline *pp = (FarmPipeline *)ctx;
    if (!pp) return;
    EnterCriticalSection(&pp->cs);
    switch (unit) {
    case FARM_UNIT_BUY:    pp->status.total_bought++; break;
    case FARM_UNIT_SPIN:   pp->status.total_wheelspins++; break;
    case FARM_UNIT_REMOVE: pp->status.total_removed++; break;
    case FARM_UNIT_RACE:   pp->status.total_races++; break;
    }
    LeaveCriticalSection(&pp->cs);
}

static void check_pause(FarmPipeline *pp) {
    while (pp->paused && !pp->stop_requested) {
        Sleep(200);
    }
}

static void farm_log(FarmPipeline *pp, const char *msg) {
    if (pp->farm_cfg.log_func)
        pp->farm_cfg.log_func(msg, pp->farm_cfg.log_ctx);
}

static FarmEngine *ensure_engine(FarmPipeline *pp) {
    if (!pp->engine)
        pp->engine = Farm_Create();
    if (!pp->engine)
        return NULL;
    if (!Farm_Refresh(pp->engine, &pp->farm_cfg)) {
        Farm_Destroy(pp->engine);
        pp->engine = NULL;
    }
    return pp->engine;
}

static void prepare_run(FarmPipeline *pp) {
    FarmEngine *fe = ensure_engine(pp);
    if (fe) Farm_ClearStop(fe);
}

/* Grab a fresh frame (pump a few to flush WGC) into `out`. */
static BOOL grab_fresh(CaptureFrame *out) {
    BOOL got = FALSE;
    for (int i = 0; i < 6; i++) {
        if (ScreenCapture_GrabFrame(out)) got = TRUE;
        Sleep(45);
    }
    return got;
}

/* ─── Economy ────────────────────────────────────────────────────────── */

/* Read CR by anchoring on the CR coin icon (resolution/aspect independent):
 * multi-scale match the icon in the top-right, then OCR the strip to its right.
 * Falls back to the fixed normalized region when the icon isn't found. */
static int read_cr_anchored(FarmPipeline *pp, const CaptureFrame *f) {
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/cricon.png",
             pp->farm_cfg.assets_dir ? pp->farm_cfg.assets_dir : "assets/templates");

    if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) {
        TM_SetFrame(f->pixels, f->width, f->height, f->stride);
        TMResult icon = TM_FindColor(path, 0.70, FALSE, TM_NamedRegion("topright"));
        if (icon.found) {
            int pad = icon.h / 2; if (pad < 3) pad = 3;
            RECT rc;
            /* Start a bit BEFORE the icon's right edge so the first digit is
             * never clipped (clipping the 8's left loops reads it as 3). The
             * icon glyph/border to the left is non-digit and gets filtered. */
            rc.left   = icon.x + (icon.w * 9) / 10;
            rc.top    = icon.y - pad;
            rc.right  = rc.left + icon.w * 12;   /* room for "2,000,000,000" */
            rc.bottom = icon.y + icon.h + pad;
            int val = FarmEconomy_ReadNumberRectPx(f->pixels, f->width, f->height,
                                                   f->stride, rc, 4, 4);
#ifdef FK_DEBUG
            /* Snapshot the exact CR strip we OCR'd, so a misread can be
             * diagnosed from the saved frame (box drawn on the strip). */
            TM_DebugSnap("econ_cr", val, rc.left, rc.top,
                         rc.right - rc.left, rc.bottom - rc.top);
#endif
            if (val >= 1000) {
                char msg[96];
                snprintf(msg, sizeof(msg),
                         "[econ] CR=%d (icon anchor s=%.2f @%d,%d)",
                         val, icon.scale, icon.cx, icon.cy);
                farm_log(pp, msg);
                return val;
            }
            farm_log(pp, "[econ] CR icon found but digits unreadable, fallback");
        } else {
            farm_log(pp, "[econ] CR icon not matched, fallback to ratio region");
        }
    }
    return FarmEconomy_ReadBalance(f->pixels, f->width, f->height, f->stride);
}

/* Foundation read: navigate to the 车辆 tab, grab a frame, OCR CR + SP.
 * Updates the status snapshot. Either value may be -1 if unreadable. */
BOOL Pipeline_ReadEconomyValues(FarmPipeline *pp, int *out_cr, int *out_sp) {
    if (out_cr) *out_cr = -1;
    if (out_sp) *out_sp = -1;
    if (!pp) return FALSE;
    prepare_run(pp);
    FarmEngine *fe = pp->engine;
    if (!fe) return FALSE;

    update_step(pp, "read_econ");

    /* Roaming -> ESC into menu; then switch to 车辆 tab only when needed. */
    if (!Farm_GotoVehiclesTab(fe)) {
        farm_log(pp, "[econ] could not reach 车辆 tab, reading CR only");
        Farm_EnterMenu(fe);
    }

    CaptureFrame f = {0};
    int cr = -1, sp = -1;
    /* Sample a few frames and reconcile. HUD OCR can flicker (white text on a
     * bright tile) and occasionally drop digits (observed: 944 -> 4). The SP
     * crop contains only the SP number, and a misread only ever drops digits
     * (yielding a SMALLER value), so taking the max across samples recovers the
     * true value. CR keeps the first plausible read. A short settle up front
     * also avoids reading a transient post-race/animation frame. */
    for (int s = 0; s < 3 && !pp->stop_requested; s++) {
        Sleep(s == 0 ? 450 : 250);
        if (!grab_fresh(&f) || !f.pixels) {
            farm_log(pp, "[econ] screen capture failed");
            continue;
        }
        int c = read_cr_anchored(pp, &f);
        int p = FarmEconomy_ReadSkillPoints(f.pixels, f.width, f.height, f.stride);
        if (c >= 0 && cr < 0) cr = c;          /* first plausible CR */
        if (p > sp && p <= 99999) sp = p;      /* max plausible SP */
        if (cr >= 0 && sp > 0 && s >= 1) break; /* have both after >=2 samples */
    }

    if (cr < 0) farm_log(pp, "[econ] CR unreadable");
    if (sp < 0) farm_log(pp, "[econ] SP unreadable (need 车辆 tab)");

#ifdef FK_DEBUG
    /* Diagnostic snapshots: the full econ frame plus the SP crop box, so a
     * misread (wrong region vs. bad OCR) can be told apart from the saved PNG.
     * (The CR strip box is snapped inside read_cr_anchored.) */
    if (f.pixels) {
        TM_SetFrame(f.pixels, f.width, f.height, f.stride);
        RECT sprc;
        sprc.left   = (LONG)(ECON_REGION_SKILL.x * f.width);
        sprc.top    = (LONG)(ECON_REGION_SKILL.y * f.height);
        sprc.right  = (LONG)((ECON_REGION_SKILL.x + ECON_REGION_SKILL.w) * f.width);
        sprc.bottom = (LONG)((ECON_REGION_SKILL.y + ECON_REGION_SKILL.h) * f.height);
        TM_DebugSnap("econ_sp", sp, sprc.left, sprc.top,
                     sprc.right - sprc.left, sprc.bottom - sprc.top);
        TM_DebugSnap("econ_full", cr, 0, 0, 0, 0);
    }
#endif

    if (cr < 0 || cr > 999999999) cr = -1;  /* FH6 caps CR at 999,999,999 */

    EnterCriticalSection(&pp->cs);
    pp->status.last_balance = cr;
    pp->status.last_skill_points = sp;
    LeaveCriticalSection(&pp->cs);

    if (out_cr) *out_cr = cr;
    if (out_sp) *out_sp = sp;
    return (cr >= 0 || sp >= 0);
}

int Pipeline_ReadEconomy(FarmPipeline *pp) {
    if (!pp) return 0;

    int cr = -1, sp = -1;
    Pipeline_ReadEconomyValues(pp, &cr, &sp);

    long cost = pp->cfg.cost_per_car > 0 ? pp->cfg.cost_per_car : 81700;
    long spc  = pp->cfg.sp_per_car   > 0 ? pp->cfg.sp_per_car   : 30;
    int n = FarmEconomy_ComputeCount(cr, sp, cost, spc);
    if (pp->cfg.max_cars_per_cycle > 0 && n > pp->cfg.max_cars_per_cycle)
        n = pp->cfg.max_cars_per_cycle;

    EnterCriticalSection(&pp->cs);
    pp->status.last_computed_count = n;
    LeaveCriticalSection(&pp->cs);

    char msg[160];
    if (cr >= 0)
        snprintf(msg, sizeof(msg),
                 "[econ] CR=%d SP=%d -> count=%d (cost=%ld sp/car=%ld)",
                 cr, sp, n, cost, spc);
    else
        snprintf(msg, sizeof(msg),
                 "[econ] CR=? SP=%d -> count=%d (cost=%ld sp/car=%ld)",
                 sp, n, cost, spc);
    farm_log(pp, msg);

    /* Explain a zero count so a skipped cycle is never mistaken for a freeze. */
    if (n == 0) {
        long by_cr = (cr >= 0) ? (cr / cost) : -1;
        long by_sp = (sp >= 0) ? (sp / spc)  : -1;
        if (cr < 0 && sp < 0)
            farm_log(pp, "[econ] count=0: CR/SP both unreadable -> buy/spin/remove skipped this cycle");
        else if (by_sp == 0)
            farm_log(pp, "[econ] count=0: SP below sp/car -> buy/spin/remove skipped (need more skill points)");
        else if (by_cr == 0)
            farm_log(pp, "[econ] count=0: CR below cost/car -> buy/spin/remove skipped (not enough credits)");
    }
    return n;
}

/* ─── Race (timed script via host callbacks) ─────────────────────────── */

/* Vision-based race step: navigate to EventLab, submit share code (UIA or Steam type),
 * then run races (each finishes with an X-restart).
 *
 * SP-target mode (sp_per_lap > 0 && target_sp > 0): CLOSED LOOP. Race a batch
 * sized to the current SP deficit, then re-read SP and repeat until the target
 * is met (or a safety cap). This corrects per-lap SP variance and any points
 * lost on restart, instead of trusting a single open-loop estimate -- which is
 * why a session could finish short of the target ("doesn't reach 999").
 * Otherwise: open-loop, race the manual race_target_laps. */
static int run_race(FarmPipeline *pp) {
    if (!pp->cfg.share_code[0]) {
        farm_log(pp, "[race] no share code configured, skipping race");
        return 0;
    }
    FarmEngine *fe = ensure_engine(pp);
    if (!fe) {
        farm_log(pp, "[race] engine init failed");
        return 0;
    }
    update_step(pp, "race");

    char msg[128];

    /* Manual / no-SP-target mode: fixed open-loop lap count. */
    if (pp->cfg.sp_per_lap <= 0 || pp->cfg.target_sp <= 0) {
        int target = pp->cfg.race_target_laps > 0 ? pp->cfg.race_target_laps : 3;
        int done = Farm_Race(fe, pp->cfg.share_code, target);
        snprintf(msg, sizeof(msg), "[race] completed %d/%d races", done, target);
        farm_log(pp, msg);
        return done;
    }

    /* SP-target closed loop. */
    const int MAX_BATCHES    = 4;
    const int MAX_TOTAL_LAPS = 30;   /* hard cap: a bad SP read can't run away */
    int total_done = 0;
    for (int batch = 0; batch < MAX_BATCHES && !pp->stop_requested; batch++) {
        int cr = -1, sp = -1;
        Pipeline_ReadEconomyValues(pp, &cr, &sp);

        if (sp < 0) {
            if (total_done > 0) {
                farm_log(pp, "[race] SP unreadable, stopping race batches");
                break;
            }
            int target = pp->cfg.race_target_laps > 0 ? pp->cfg.race_target_laps : 3;
            farm_log(pp, "[race] SP unreadable, using manual lap count");
            total_done += Farm_Race(fe, pp->cfg.share_code, target);
            break;
        }

        int deficit = pp->cfg.target_sp - sp;
        if (deficit <= 0) {
            farm_log(pp, total_done == 0 ? "[race] SP already at target, skipping race"
                                         : "[race] SP target reached");
            break;
        }

        int laps = (deficit + pp->cfg.sp_per_lap - 1) / pp->cfg.sp_per_lap;
        if (total_done + laps > MAX_TOTAL_LAPS) laps = MAX_TOTAL_LAPS - total_done;
        if (laps <= 0) { farm_log(pp, "[race] lap cap reached"); break; }

        snprintf(msg, sizeof(msg),
                 "[race] SP=%d target=%d /lap=%d -> %d laps (batch %d)",
                 sp, pp->cfg.target_sp, pp->cfg.sp_per_lap, laps, batch + 1);
        farm_log(pp, msg);

        int done = Farm_Race(fe, pp->cfg.share_code, laps);
        total_done += done;
        if (done <= 0) { farm_log(pp, "[race] race made no progress, stopping"); break; }
    }

    snprintf(msg, sizeof(msg), "[race] total %d races this step", total_done);
    farm_log(pp, msg);
    return total_done;
}

/* ─── Single step ────────────────────────────────────────────────────── */

void Pipeline_RunStep(FarmPipeline *pp, PipelineStep step, int count) {
    if (!pp) return;
    pp->running = TRUE;
    pp->stop_requested = FALSE;
    pp->paused = FALSE;
    pp->status.running = TRUE;

    prepare_run(pp);
    FarmEngine *fe = pp->engine;
    if (!fe) {
        update_step(pp, "init_failed");
        pp->running = FALSE;
        return;
    }

    int wmode = pp->cfg.wheelspin_mode == 1 ? 1 : 2;
    int rmode = pp->cfg.remove_mode   == 2 ? 2 : 1;

    switch (step) {
    case PIPE_STEP_RACE:
        run_race(pp);  /* totals updated per lap via on_unit_done */
        break;
    case PIPE_STEP_READ_ECON:
        Pipeline_ReadEconomy(pp);
        break;
    case PIPE_STEP_BUY: {
        int n = count > 0 ? count : (pp->cfg.auto_count ? Pipeline_ReadEconomy(pp)
                                                        : pp->cfg.buy_car_count);
        if (n <= 0) n = pp->cfg.buy_car_count > 0 ? pp->cfg.buy_car_count : 1;
        update_step(pp, "buy_car");
        Farm_BuyCar(fe, n);
        break;
    }
    case PIPE_STEP_SPIN: {
        int n = count > 0 ? count : (pp->cfg.auto_count ? pp->status.last_computed_count
                                                        : pp->cfg.wheelspin_count);
        if (n <= 0) n = pp->cfg.wheelspin_count > 0 ? pp->cfg.wheelspin_count : 1;
        update_step(pp, "wheelspin");
        Farm_SuperWheelspinMode(fe, n, wmode);
        break;
    }
    case PIPE_STEP_REMOVE: {
        int n = count > 0 ? count : (pp->cfg.auto_count ? pp->status.last_computed_count
                                                        : pp->cfg.remove_car_count);
        if (n <= 0) n = pp->cfg.remove_car_count > 0 ? pp->cfg.remove_car_count : 1;
        update_step(pp, "remove_car");
        Farm_RemoveCarMode(fe, n, rmode);
        break;
    }
    default:
        break;
    }

    pp->running = FALSE;
    update_step(pp, "step_done");
}

/* ─── Full loop ──────────────────────────────────────────────────────── */

void Pipeline_Run(FarmPipeline *pp) {
    if (!pp) return;
    pp->running = TRUE;
    pp->stop_requested = FALSE;
    pp->paused = FALSE;
    memset(&pp->status, 0, sizeof(pp->status));
    pp->status.running = TRUE;
    pp->status.last_balance = -1;
    pp->status.last_skill_points = -1;

    prepare_run(pp);
    FarmEngine *fe = pp->engine;
    if (!fe) {
        update_step(pp, "init_failed");
        pp->running = FALSE;
        return;
    }

    int wmode = pp->cfg.wheelspin_mode == 1 ? 1 : 2;
    int rmode = pp->cfg.remove_mode   == 2 ? 2 : 1;
    int max_cycles = pp->cfg.max_cycles > 0 ? pp->cfg.max_cycles : 999999;

    for (int cycle = 0; cycle < max_cycles && !pp->stop_requested; cycle++) {
        pp->status.current_cycle = cycle + 1;
        check_pause(pp);
        if (pp->stop_requested) break;

        BOOL cycle_success = FALSE;

        /* 1. Race (bank skill points) */
        if (pp->cfg.enable_race && !pp->stop_requested) {
            int rd = run_race(pp);
            if (rd > 0) cycle_success = TRUE;
        }

        /* 2. Read economy -> compute n */
        int n = 0;
        if (pp->cfg.auto_count && !pp->stop_requested) {
            n = Pipeline_ReadEconomy(pp);
        } else {
            n = pp->cfg.buy_car_count;
        }

        /* 3a. Buy n */
        if (pp->cfg.enable_buy_car && n > 0 && !pp->stop_requested) {
            update_step(pp, "buy_car");
            check_pause(pp);
            int bought = Farm_BuyCar(fe, n);
            if (bought > 0) cycle_success = TRUE;
        }

        /* 3b. Wheelspin n (mode 2) */
        if (pp->cfg.enable_wheelspin && n > 0 && !pp->stop_requested) {
            update_step(pp, "wheelspin");
            check_pause(pp);
            int spins = Farm_SuperWheelspinMode(fe,
                pp->cfg.auto_count ? n : pp->cfg.wheelspin_count, wmode);
            if (spins > 0) cycle_success = TRUE;
        }

        /* 4. Remove (mode 1, image-recognition).
         *
         * The remove count is deliberately decoupled from the SP-derived buy
         * quantity `n`. Only in a coupled auto buy/spin cycle does removing the
         * just-processed batch (n) make sense. When remove runs on its own
         * (buy/spin disabled this run), honor the manual count field so a
         * "remove-only" loop deletes what the user asked for (e.g. 99) instead
         * of the SP-based n, which can be as low as 1. */
        int remove_n = ((pp->cfg.enable_buy_car || pp->cfg.enable_wheelspin) &&
                        pp->cfg.auto_count)
                     ? n : pp->cfg.remove_car_count;
        if (pp->cfg.enable_remove_car && remove_n > 0 && !pp->stop_requested) {
            update_step(pp, "remove_car");
            check_pause(pp);
            int removed = Farm_RemoveCarMode(fe, remove_n, rmode);
            if (removed > 0) cycle_success = TRUE;
        }

        if (cycle_success) {
            pp->status.consecutive_fails = 0;
        } else {
            pp->status.consecutive_fails++;
            if (pp->status.consecutive_fails >= pp->cfg.consecutive_fail_max) {
                update_step(pp, "STOPPED: too many failures");
                break;
            }
        }
    }

    pp->running = FALSE;
    update_step(pp, "all_done");
}

/* ─── Lifecycle / control ────────────────────────────────────────────── */

FarmPipeline* Pipeline_Create(void) {
    FarmPipeline *pp = (FarmPipeline*)calloc(1, sizeof(FarmPipeline));
    if (pp) InitializeCriticalSection(&pp->cs);
    return pp;
}

void Pipeline_Destroy(FarmPipeline *pp) {
    if (!pp) return;
    if (pp->engine) Farm_Destroy(pp->engine);
    if (pp->ocr_ready) FarmEconomy_Shutdown();
    DeleteCriticalSection(&pp->cs);
    free(pp);
}

BOOL Pipeline_Init(FarmPipeline *pp, const PipelineConfig *cfg,
                   const FarmConfig *farm_cfg) {
    if (!pp || !cfg || !farm_cfg) return FALSE;
    pp->cfg = *cfg;
    pp->farm_cfg = *farm_cfg;
    /* Live cumulative totals: farm_flow reports each completed unit. */
    pp->farm_cfg.on_unit_done = pipe_unit_done;
    pp->farm_cfg.on_unit_done_ctx = pp;
    if (pp->cfg.consecutive_fail_max <= 0) pp->cfg.consecutive_fail_max = 5;
    if (pp->cfg.cost_per_car <= 0) pp->cfg.cost_per_car = 81700;
    if (pp->cfg.sp_per_car <= 0)   pp->cfg.sp_per_car = 30;
    if (pp->cfg.wheelspin_mode != 1) pp->cfg.wheelspin_mode = 2;
    if (pp->cfg.remove_mode != 2)    pp->cfg.remove_mode = 1;
    if (pp->cfg.target_sp <= 0)      pp->cfg.target_sp = 999;
    pp->status.last_balance = -1;
    pp->status.last_skill_points = -1;

    const WCHAR *lang = pp->cfg.ocr_lang[0] ? pp->cfg.ocr_lang : L"en-US";
    pp->ocr_ready = FarmEconomy_Init(lang);
    if (!pp->ocr_ready) farm_log(pp, "[pipeline] OCR init failed (economy disabled)");
    return TRUE;
}

void Pipeline_Stop(FarmPipeline *pp) {
    if (!pp) return;
    pp->stop_requested = TRUE;
    pp->paused = FALSE;
    if (pp->engine) Farm_RequestStop(pp->engine);
    if (pp->cfg.race_stop) pp->cfg.race_stop(pp->cfg.race_ctx);
}

void Pipeline_Pause(FarmPipeline *pp)   { if (pp) pp->paused = TRUE; }
void Pipeline_Resume(FarmPipeline *pp)  { if (pp) pp->paused = FALSE; }
BOOL Pipeline_IsPaused(FarmPipeline *pp){ return pp ? pp->paused : FALSE; }
BOOL Pipeline_IsRunning(FarmPipeline *pp){ return pp ? pp->running : FALSE; }

PipelineStatus Pipeline_GetStatus(FarmPipeline *pp) {
    PipelineStatus s = {0};
    if (!pp) return s;
    EnterCriticalSection(&pp->cs);
    s = pp->status;
    LeaveCriticalSection(&pp->cs);
    return s;
}

void Pipeline_RegisterHotkeys(FarmPipeline *pp, HWND owner_hwnd) {
    (void)pp;
    RegisterHotKey(owner_hwnd, HOTKEY_ID_STOP, 0, VK_F8);
    RegisterHotKey(owner_hwnd, HOTKEY_ID_PAUSE, 0, VK_F9);
}

void Pipeline_UnregisterHotkeys(FarmPipeline *pp, HWND owner_hwnd) {
    (void)pp;
    UnregisterHotKey(owner_hwnd, HOTKEY_ID_STOP);
    UnregisterHotKey(owner_hwnd, HOTKEY_ID_PAUSE);
}

void Pipeline_HandleHotkey(FarmPipeline *pp, int hotkey_id) {
    if (!pp) return;
    if (hotkey_id == HOTKEY_ID_STOP) {
        Pipeline_Stop(pp);
    } else if (hotkey_id == HOTKEY_ID_PAUSE) {
        if (pp->paused) Pipeline_Resume(pp);
        else Pipeline_Pause(pp);
    }
}
