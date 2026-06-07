#ifndef FOCUSKEEPER_RACE_PROFILE_H
#define FOCUSKEEPER_RACE_PROFILE_H

#include <windows.h>

/*
 * Race Profile - Configurable Race Loop Definitions
 *
 * A Profile defines a sequence of steps that loops continuously.
 * Each step has a unified set of fields controlling its behavior.
 *
 * Step Modes:
 *   hold     - Hold a key for the entire step duration
 *   tap      - Tap a key repeatedly at a fixed interval
 *   sequence - Execute a series of key actions in order
 *   wait     - Do nothing (pure delay)
 */

/* Maximum limits */
#define PROFILE_MAX_STEPS       16
#define PROFILE_MAX_ACTIONS     16
#define PROFILE_NAME_LEN        64
#define PROFILE_MAX_PROFILES    16
#define STEP_NAME_LEN           32

/* Step mode (how input is handled during this step) */
typedef enum {
    STEP_MODE_WAIT,         /* Do nothing, just wait */
    STEP_MODE_HOLD,         /* Hold a key down for the duration */
    STEP_MODE_TAP,          /* Tap a key repeatedly at interval */
    STEP_MODE_SEQUENCE,     /* Execute a list of key actions */
    STEP_MODE_COUNT
} StepMode;

/* Single key action within a SEQUENCE step */
typedef struct {
    DWORD   vk;             /* Virtual key code */
    DWORD   hold_ms;        /* How long to hold (0 = brief tap) */
    DWORD   delay_after_ms; /* Wait after this action before next */
} KeyAction;

/* One step in the race loop */
typedef struct {
    StepMode    mode;           /* What this step does */
    WCHAR       step_name[STEP_NAME_LEN]; /* Display name (optional, shown in UI) */
    DWORD       duration_ms;    /* Total time for this step */
    DWORD       delay_before_ms;/* Delay before step starts (0 = immediate) */

    /* For HOLD mode */
    DWORD       key;            /* Virtual key code to hold */

    /* For TAP mode */
    DWORD       tap_key;        /* Virtual key code to tap */
    DWORD       tap_hold_ms;    /* How long each tap lasts (default 80) */
    DWORD       tap_interval_ms;/* Time between taps */

    /* For SEQUENCE mode */
    int         action_count;
    KeyAction   actions[PROFILE_MAX_ACTIONS];
    BOOL        loop_actions;   /* Loop the sequence until duration expires */
} RaceStep;

/* Complete race profile */
typedef struct {
    WCHAR       name[PROFILE_NAME_LEN];
    WCHAR       filename[PROFILE_NAME_LEN];
    int         step_count;
    RaceStep    steps[PROFILE_MAX_STEPS];
} RaceProfile;

/* ─── Public API ──────────────────────────────────────────────────── */

/* Load a profile from an INI file. Returns TRUE on success. */
BOOL    Profile_Load(const WCHAR *ini_path, RaceProfile *out);

/* Save a profile to an INI file */
BOOL    Profile_Save(const WCHAR *ini_path, const RaceProfile *profile);

/* Get the profiles directory path (exe_dir/profiles/) */
const WCHAR* Profile_GetDirectory(void);

/* Enumerate available profiles. Returns count (up to max_count). */
int     Profile_Enumerate(WCHAR names[][PROFILE_NAME_LEN], int max_count);

/* Load the built-in default profile (skill grind) - REMOVED: use Profile_WriteDefaultTemplate + Profile_Load instead */

/* Write the default profile with full Chinese documentation */
BOOL    Profile_WriteDefaultTemplate(const WCHAR *path);

/* Read comment lines from a profile INI file for display (caller frees with free()) */
WCHAR*  Profile_ReadComments(const WCHAR *ini_path);

#endif /* FOCUSKEEPER_RACE_PROFILE_H */
