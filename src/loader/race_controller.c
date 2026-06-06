/*
 * race_controller.c - Facade for auto-race subsystem
 *
 * Coordinates InputBackend creation, Profile loading, and
 * AutoRaceEngine lifecycle. Single entry point for main.c.
 */

#include "race_controller.h"
#include "input_hook_backend.h"
#include <wchar.h>

BOOL RaceCtrl_Init(RaceController *rc, AutoRaceCallback cb, void *user_data)
{
    if (!rc) return FALSE;

    ZeroMemory(rc, sizeof(RaceController));

    rc->input = HookBackend_Create();
    if (!rc->input) return FALSE;

    rc->engine = AutoRace_Create(rc->input, cb, user_data);
    if (!rc->engine) {
        rc->input->destroy(rc->input);
        rc->input = NULL;
        return FALSE;
    }

    return TRUE;
}

void RaceCtrl_Shutdown(RaceController *rc)
{
    if (!rc) return;

    if (rc->engine) {
        AutoRace_Destroy(rc->engine);
        rc->engine = NULL;
    }

    if (rc->input) {
        rc->input->destroy(rc->input);
        rc->input = NULL;
    }

    rc->profile_loaded = FALSE;
}

BOOL RaceCtrl_LoadProfile(RaceController *rc, const WCHAR *filename)
{
    if (!rc || !filename) return FALSE;

    WCHAR full_path[MAX_PATH];
    _snwprintf(full_path, MAX_PATH, L"%s%s", Profile_GetDirectory(), filename);

    if (!Profile_Load(full_path, &rc->profile))
        return FALSE;

    rc->profile_loaded = TRUE;
    return TRUE;
}

BOOL RaceCtrl_Start(RaceController *rc, HWND game_hwnd)
{
    if (!rc || !rc->engine) return FALSE;
    if (!rc->profile_loaded) return FALSE;

    return AutoRace_Start(rc->engine, &rc->profile, game_hwnd);
}

void RaceCtrl_Stop(RaceController *rc)
{
    if (!rc || !rc->engine) return;
    AutoRace_Stop(rc->engine);
}

void RaceCtrl_GetStatus(RaceController *rc, AutoRaceStatus *out)
{
    if (!rc || !rc->engine || !out) return;
    AutoRace_GetStatus(rc->engine, out);
}

BOOL RaceCtrl_IsRunning(RaceController *rc)
{
    if (!rc || !rc->engine) return FALSE;
    return AutoRace_IsRunning(rc->engine);
}
