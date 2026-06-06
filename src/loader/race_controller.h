#ifndef FOCUSKEEPER_RACE_CONTROLLER_H
#define FOCUSKEEPER_RACE_CONTROLLER_H

#include <windows.h>
#include "auto_race.h"
#include "race_profile.h"
#include "input_backend.h"

/*
 * RaceController - Facade coordinating profile, engine, and input.
 *
 * Manages the lifecycle: select profile -> start engine -> relay
 * state changes via callback. Owns the InputBackend and Engine.
 */

typedef struct {
    AutoRaceEngine *engine;
    InputBackend   *input;
    RaceProfile     profile;
    BOOL            profile_loaded;
} RaceController;

/* Initialize controller (creates backend and engine internally) */
BOOL RaceCtrl_Init(RaceController *rc, AutoRaceCallback cb, void *user_data);

/* Shutdown and release all resources */
void RaceCtrl_Shutdown(RaceController *rc);

/* Load a profile by filename (looked up in profiles directory) */
BOOL RaceCtrl_LoadProfile(RaceController *rc, const WCHAR *filename);

/* Start the race loop (requires profile loaded + valid game window) */
BOOL RaceCtrl_Start(RaceController *rc, HWND game_hwnd);

/* Stop the race loop */
void RaceCtrl_Stop(RaceController *rc);

/* Get current engine status */
void RaceCtrl_GetStatus(RaceController *rc, AutoRaceStatus *out);

/* Check if engine is running */
BOOL RaceCtrl_IsRunning(RaceController *rc);

#endif /* FOCUSKEEPER_RACE_CONTROLLER_H */
