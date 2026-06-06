#ifndef FOCUSKEEPER_AUTO_RACE_H
#define FOCUSKEEPER_AUTO_RACE_H

#include <windows.h>
#include "race_profile.h"
#include "input_backend.h"

/*
 * AutoRace Engine - Instance-based state machine.
 *
 * Drives the race loop on a background thread. Uses StepExecutor
 * strategies for each step and reports state changes via callback.
 * Zero global state - all context held in the opaque engine instance.
 */

typedef enum {
    AUTO_RACE_IDLE,
    AUTO_RACE_RUNNING,
    AUTO_RACE_ERROR
} AutoRaceState;

typedef struct {
    AutoRaceState   state;
    int             current_step;
    int             lap_count;
    DWORD           step_elapsed;
    DWORD           total_elapsed;
    const WCHAR    *step_name;
} AutoRaceStatus;

/* Callback signature: fired on state transitions from the engine thread */
typedef void (*AutoRaceCallback)(AutoRaceState new_state, const WCHAR *message, void *user_data);

/* Opaque engine type */
typedef struct AutoRaceEngine AutoRaceEngine;

/* ─── Lifecycle ───────────────────────────────────────────────────── */

AutoRaceEngine* AutoRace_Create(InputBackend *input, AutoRaceCallback cb, void *user_data);
void            AutoRace_Destroy(AutoRaceEngine *engine);

/* ─── Control ─────────────────────────────────────────────────────── */

BOOL            AutoRace_Start(AutoRaceEngine *engine, const RaceProfile *profile, HWND game_hwnd);
void            AutoRace_Stop(AutoRaceEngine *engine);

/* ─── Query ───────────────────────────────────────────────────────── */

void            AutoRace_GetStatus(AutoRaceEngine *engine, AutoRaceStatus *out);
BOOL            AutoRace_IsRunning(AutoRaceEngine *engine);
AutoRaceState   AutoRace_GetState(AutoRaceEngine *engine);

#endif /* FOCUSKEEPER_AUTO_RACE_H */
