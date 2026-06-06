/*
 * auto_race.c - Instance-based Auto Race Engine
 *
 * Manages the race loop lifecycle: thread creation, step iteration,
 * state transitions, and cleanup. Delegates step execution to
 * StepExecutor strategies via the InputBackend interface.
 */

#include "auto_race.h"
#include "step_executor.h"
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static const WCHAR *s_mode_labels[] = {
    L"Wait", L"Hold", L"Tap", L"Sequence"
};

/* ─── Engine Instance ─────────────────────────────────────────────── */

struct AutoRaceEngine {
    /* Dependencies (injected) */
    InputBackend       *input;
    AutoRaceCallback    callback;
    void               *cb_user_data;

    /* Thread control */
    HANDLE              thread;
    HANDLE              stop_event;
    volatile BOOL       external_stop;

    /* Configuration */
    RaceProfile         profile;
    HWND                game_hwnd;

    /* Runtime state */
    volatile AutoRaceState state;
    volatile int        current_step;
    volatile int        lap_count;
    volatile DWORD      step_start_tick;
    volatile DWORD      total_start_tick;

    /* Display buffer for step_name */
    WCHAR               display_name[64];
};

/* ─── Internal ────────────────────────────────────────────────────── */

static void NotifyState(AutoRaceEngine *e, AutoRaceState state, const WCHAR *msg)
{
    e->state = state;
    if (e->callback) {
        e->callback(state, msg, e->cb_user_data);
    }
}

static DWORD WINAPI RaceThreadProc(LPVOID param)
{
    AutoRaceEngine *e = (AutoRaceEngine *)param;

    e->current_step = 0;
    e->lap_count = 0;
    e->total_start_tick = GetTickCount();

    NotifyState(e, AUTO_RACE_RUNNING, L"Auto race started");

    while (WaitForSingleObject(e->stop_event, 0) == WAIT_TIMEOUT) {
        if (!IsWindow(e->game_hwnd)) {
            NotifyState(e, AUTO_RACE_ERROR, L"Game window closed");
            break;
        }

        RaceStep *step = &e->profile.steps[e->current_step];

        /* Optional delay before step */
        if (step->delay_before_ms > 0) {
            StepContext delay_ctx = {
                .step = step,
                .input = e->input,
                .stop_event = e->stop_event,
                .game_hwnd = e->game_hwnd
            };
            if (StepExec_WaitInterruptible(&delay_ctx, step->delay_before_ms))
                break;
            if (!IsWindow(e->game_hwnd)) {
                NotifyState(e, AUTO_RACE_ERROR, L"Game window closed");
                break;
            }
        }

        e->step_start_tick = GetTickCount();

        /* Execute step via strategy */
        StepContext ctx = {
            .step = step,
            .input = e->input,
            .stop_event = e->stop_event,
            .game_hwnd = e->game_hwnd
        };

        BOOL completed = StepExec_Run(&ctx);

        if (!completed) break;
        if (!IsWindow(e->game_hwnd)) {
            NotifyState(e, AUTO_RACE_ERROR, L"Game window closed");
            break;
        }

        /* Advance to next step */
        e->current_step++;
        if (e->current_step >= e->profile.step_count) {
            e->current_step = 0;
            e->lap_count++;
        }
    }

    e->input->release_all(e->input);

    /* Only fire callback for unexpected exits (error, window closed).
     * External stop (via AutoRace_Stop) handles UI updates itself
     * to avoid deadlock between race thread and GUI thread. */
    if (e->state == AUTO_RACE_RUNNING && !e->external_stop) {
        NotifyState(e, AUTO_RACE_IDLE, L"Auto race stopped");
    } else if (e->state == AUTO_RACE_RUNNING) {
        e->state = AUTO_RACE_IDLE;
    }

    return 0;
}

/* ─── Public API ──────────────────────────────────────────────────── */

AutoRaceEngine* AutoRace_Create(InputBackend *input, AutoRaceCallback cb, void *user_data)
{
    if (!input) return NULL;

    AutoRaceEngine *e = (AutoRaceEngine *)calloc(1, sizeof(AutoRaceEngine));
    if (!e) return NULL;

    e->input = input;
    e->callback = cb;
    e->cb_user_data = user_data;
    e->state = AUTO_RACE_IDLE;

    return e;
}

void AutoRace_Destroy(AutoRaceEngine *engine)
{
    if (!engine) return;
    AutoRace_Stop(engine);
    free(engine);
}

BOOL AutoRace_Start(AutoRaceEngine *engine, const RaceProfile *profile, HWND game_hwnd)
{
    if (!engine || !profile || profile->step_count <= 0) return FALSE;
    if (engine->state == AUTO_RACE_RUNNING) return FALSE;
    if (!IsWindow(game_hwnd)) return FALSE;

    if (!engine->input->init(engine->input, game_hwnd)) {
        NotifyState(engine, AUTO_RACE_ERROR, L"Input backend init failed");
        return FALSE;
    }

    memcpy(&engine->profile, profile, sizeof(RaceProfile));
    engine->game_hwnd = game_hwnd;
    engine->external_stop = FALSE;

    engine->stop_event = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!engine->stop_event) return FALSE;

    engine->thread = CreateThread(NULL, 0, RaceThreadProc, engine, 0, NULL);
    if (!engine->thread) {
        CloseHandle(engine->stop_event);
        engine->stop_event = NULL;
        return FALSE;
    }

    return TRUE;
}

void AutoRace_Stop(AutoRaceEngine *engine)
{
    if (!engine) return;
    if (engine->state != AUTO_RACE_RUNNING && engine->state != AUTO_RACE_ERROR)
        return;

    /* Mark as external stop to suppress callback (avoids deadlock) */
    engine->external_stop = TRUE;

    if (engine->stop_event)
        SetEvent(engine->stop_event);

    if (engine->thread) {
        WaitForSingleObject(engine->thread, 3000);
        CloseHandle(engine->thread);
        engine->thread = NULL;
    }

    if (engine->stop_event) {
        CloseHandle(engine->stop_event);
        engine->stop_event = NULL;
    }

    engine->input->release_all(engine->input);
    engine->input->shutdown(engine->input);
    engine->state = AUTO_RACE_IDLE;
}

void AutoRace_GetStatus(AutoRaceEngine *engine, AutoRaceStatus *out)
{
    if (!engine || !out) return;

    out->state = engine->state;
    out->current_step = engine->current_step;
    out->lap_count = engine->lap_count;

    if (engine->state == AUTO_RACE_RUNNING) {
        out->step_elapsed = GetTickCount() - engine->step_start_tick;
        out->total_elapsed = GetTickCount() - engine->total_start_tick;
    } else {
        out->step_elapsed = 0;
        out->total_elapsed = 0;
    }

    int mode = -1;
    const RaceStep *step = NULL;
    if (engine->current_step >= 0 && engine->current_step < engine->profile.step_count) {
        step = &engine->profile.steps[engine->current_step];
        mode = (int)step->mode;
    }

    if (step && step->step_name[0]) {
        const WCHAR *mlabel = (mode >= 0 && mode < STEP_MODE_COUNT)
            ? s_mode_labels[mode] : L"";
        _snwprintf(engine->display_name, 64, L"%s %s", step->step_name, mlabel);
        out->step_name = engine->display_name;
    } else if (mode >= 0 && mode < STEP_MODE_COUNT) {
        out->step_name = s_mode_labels[mode];
    } else {
        out->step_name = L"Idle";
    }
}

BOOL AutoRace_IsRunning(AutoRaceEngine *engine)
{
    return engine && engine->state == AUTO_RACE_RUNNING;
}

AutoRaceState AutoRace_GetState(AutoRaceEngine *engine)
{
    return engine ? engine->state : AUTO_RACE_IDLE;
}
