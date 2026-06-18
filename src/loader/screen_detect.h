#ifndef FOCUSKEEPER_SCREEN_DETECT_H
#define FOCUSKEEPER_SCREEN_DETECT_H

#include <windows.h>

/*
 * Screen Detect - Game Screen State Detection
 *
 * Phase 1: Stub (always returns SCREEN_UNKNOWN)
 * Phase 2: Background capture + pixel sampling + OCR
 *
 * Combines Windows Graphics Capture (background screenshot) with
 * pixel analysis and Windows OCR to determine game state.
 */

typedef enum {
    SCREEN_UNKNOWN,       /* Cannot determine */
    SCREEN_GAMEPLAY,      /* In-race (HUD visible) */
    SCREEN_RESULTS,       /* Race results screen */
    SCREEN_LOADING,       /* Loading screen */
    SCREEN_GARAGE,        /* Garage / car collection */
    SCREEN_SKILL_TREE,   /* Skill tree open */
    SCREEN_SKILL_CONFIRM,/* Skill purchase confirmation */
    SCREEN_MAP,          /* Map / route selection */
    SCREEN_MENU,         /* Main menu / pause menu */
} GameScreen;

typedef struct {
    GameScreen  screen;
    int         skill_points;          /* Detected skill points (-1 if unknown) */
    WCHAR       car_name[128];         /* Detected car name (empty if unknown) */
    BOOL        skill_nodes[8];        /* Which skill nodes are available */
    int         skill_node_count;      /* Number of detected nodes */
    float       confidence;            /* Detection confidence 0.0 - 1.0 */
} ScreenState;

/* Initialize screen detection system (capture + OCR). */
BOOL ScreenDetect_Init(void);

/* Shutdown and release all resources. */
void ScreenDetect_Shutdown(void);

/*
 * Start detection session for a game window.
 * Must be called before Analyze.
 */
BOOL ScreenDetect_StartSession(HWND game_hwnd);

/* Stop detection session. */
void ScreenDetect_StopSession(void);

/*
 * Quick screen state check using pixel sampling only.
 * Fast (~1ms) but lower accuracy.
 */
GameScreen ScreenDetect_QuickCheck(void);

/*
 * Full analysis with OCR. Fills ScreenState struct.
 * Slower (~50-100ms) but provides text recognition results.
 */
BOOL ScreenDetect_FullAnalyze(ScreenState *state);

/* Legacy API (backward compatible) */
GameScreen ScreenDetect_Analyze(HWND game_hwnd);

/* Check if detection session is active. */
BOOL ScreenDetect_IsActive(void);

#endif /* FOCUSKEEPER_SCREEN_DETECT_H */
