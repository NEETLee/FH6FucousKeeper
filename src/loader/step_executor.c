/*
 * step_executor.c - Strategy pattern step execution
 *
 * Provides a dispatch table mapping StepMode -> execution function,
 * and a common interruptible-wait framework used by all strategies.
 */

#include "step_executor.h"

/* ─── Strategy dispatch table ─────────────────────────────────────── */

static StepExecFn s_strategies[STEP_MODE_COUNT] = {
    StepExec_Wait,      /* STEP_MODE_WAIT */
    StepExec_Hold,      /* STEP_MODE_HOLD */
    StepExec_Tap,       /* STEP_MODE_TAP */
    StepExec_Sequence,  /* STEP_MODE_SEQUENCE */
};

/* ─── Common framework ────────────────────────────────────────────── */

BOOL StepExec_WaitInterruptible(StepContext *ctx, DWORD ms)
{
    if (ms == 0) {
        return WaitForSingleObject(ctx->stop_event, 0) != WAIT_TIMEOUT;
    }

    /* Wait in 200ms slices checking window alive */
    DWORD remaining = ms;
    while (remaining > 0) {
        DWORD slice = (remaining > 200) ? 200 : remaining;
        if (WaitForSingleObject(ctx->stop_event, slice) != WAIT_TIMEOUT)
            return TRUE;
        if (!IsWindow(ctx->game_hwnd))
            return TRUE;
        remaining -= slice;
    }
    return FALSE;
}

BOOL StepExec_IsAlive(StepContext *ctx)
{
    return IsWindow(ctx->game_hwnd);
}

/* ─── Dispatch ────────────────────────────────────────────────────── */

BOOL StepExec_Run(StepContext *ctx)
{
    int mode = (int)ctx->step->mode;
    if (mode < 0 || mode >= STEP_MODE_COUNT)
        return StepExec_Wait(ctx);

    return s_strategies[mode](ctx);
}

/* ─── Strategy: Wait ──────────────────────────────────────────────── */

BOOL StepExec_Wait(StepContext *ctx)
{
    return !StepExec_WaitInterruptible(ctx, ctx->step->duration_ms);
}

/* ─── Strategy: Hold ──────────────────────────────────────────────── */

BOOL StepExec_Hold(StepContext *ctx)
{
    DWORD key = ctx->step->key;
    DWORD remaining = ctx->step->duration_ms;

    ctx->input->key_down(ctx->input, key);

    /* Periodically re-send key_down to simulate continuous hold.
     * PostMessage-based backends need repeated WM_KEYDOWN messages;
     * bitmap-based backends are idempotent so this is harmless. */
    while (remaining > 0) {
        DWORD slice = (remaining > 60) ? 60 : remaining;
        if (StepExec_WaitInterruptible(ctx, slice)) {
            ctx->input->key_up(ctx->input, key);
            return FALSE;
        }
        remaining -= slice;
        if (remaining > 0)
            ctx->input->key_down(ctx->input, key);
    }

    ctx->input->key_up(ctx->input, key);
    return TRUE;
}

/* ─── Strategy: Tap ───────────────────────────────────────────────── */

BOOL StepExec_Tap(StepContext *ctx)
{
    const RaceStep *step = ctx->step;
    DWORD key = step->tap_key;
    DWORD hold = step->tap_hold_ms > 0 ? step->tap_hold_ms : 80;
    DWORD interval = step->tap_interval_ms > 100 ? step->tap_interval_ms : 100;
    DWORD elapsed = 0;
    DWORD start = GetTickCount();

    while (elapsed < step->duration_ms) {
        if (StepExec_WaitInterruptible(ctx, 0)) return FALSE;

        ctx->input->key_down(ctx->input, key);
        if (StepExec_WaitInterruptible(ctx, hold)) {
            ctx->input->key_up(ctx->input, key);
            return FALSE;
        }
        ctx->input->key_up(ctx->input, key);

        if (StepExec_WaitInterruptible(ctx, interval))
            return FALSE;

        elapsed = GetTickCount() - start;
    }
    return TRUE;
}

/* ─── Strategy: Sequence ──────────────────────────────────────────── */

BOOL StepExec_Sequence(StepContext *ctx)
{
    const RaceStep *step = ctx->step;
    DWORD start = GetTickCount();

    do {
        for (int i = 0; i < step->action_count; i++) {
            if (StepExec_WaitInterruptible(ctx, 0)) return FALSE;

            DWORD elapsed = GetTickCount() - start;
            if (elapsed >= step->duration_ms) return TRUE;

            const KeyAction *action = &step->actions[i];
            if (action->vk == 0) continue;

            DWORD hold = action->hold_ms > 0 ? action->hold_ms : 80;

            ctx->input->key_down(ctx->input, action->vk);
            if (StepExec_WaitInterruptible(ctx, hold)) {
                ctx->input->key_up(ctx->input, action->vk);
                return FALSE;
            }
            ctx->input->key_up(ctx->input, action->vk);

            if (action->delay_after_ms > 0) {
                if (StepExec_WaitInterruptible(ctx, action->delay_after_ms))
                    return FALSE;
            }
        }
    } while (step->loop_actions && (GetTickCount() - start) < step->duration_ms);

    /* If not looping, wait out remaining duration */
    if (!step->loop_actions) {
        DWORD elapsed = GetTickCount() - start;
        if (elapsed < step->duration_ms) {
            if (StepExec_WaitInterruptible(ctx, step->duration_ms - elapsed))
                return FALSE;
        }
    }

    return TRUE;
}
