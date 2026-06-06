#ifndef FOCUSKEEPER_STEP_EXECUTOR_H
#define FOCUSKEEPER_STEP_EXECUTOR_H

#include <windows.h>
#include "race_profile.h"
#include "input_backend.h"

/*
 * StepExecutor - Strategy pattern for race step execution.
 *
 * Each step mode (wait/hold/tap/sequence) is a strategy function.
 * The common interruptible-wait framework handles cancellation
 * and window-alive checks uniformly.
 *
 * Returns TRUE if step completed normally, FALSE if interrupted.
 */

typedef struct {
    const RaceStep *step;
    InputBackend   *input;
    HANDLE          stop_event;
    HWND            game_hwnd;
} StepContext;

/* Strategy function type */
typedef BOOL (*StepExecFn)(StepContext *ctx);

/* ─── Public API ──────────────────────────────────────────────────── */

/* Execute a step using the appropriate strategy. Returns TRUE on normal completion. */
BOOL StepExec_Run(StepContext *ctx);

/* Common interruptible wait. Returns TRUE if interrupted (should stop). */
BOOL StepExec_WaitInterruptible(StepContext *ctx, DWORD ms);

/* Check if game window is still alive */
BOOL StepExec_IsAlive(StepContext *ctx);

/* Individual strategies (exposed for testing) */
BOOL StepExec_Wait(StepContext *ctx);
BOOL StepExec_Hold(StepContext *ctx);
BOOL StepExec_Tap(StepContext *ctx);
BOOL StepExec_Sequence(StepContext *ctx);

#endif /* FOCUSKEEPER_STEP_EXECUTOR_H */
