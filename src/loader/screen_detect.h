#ifndef FOCUSKEEPER_SCREEN_DETECT_H
#define FOCUSKEEPER_SCREEN_DETECT_H

#include <windows.h>

/*
 * Screen Detect - Game Screen State Detection (Phase 2)
 *
 * Currently a stub. Phase 2 will implement GDI-based screenshot
 * capture and pixel analysis to detect which screen the game is on.
 */

typedef enum {
    SCREEN_UNKNOWN,     /* Cannot determine (Phase 1 always returns this) */
    SCREEN_GAMEPLAY,    /* In-race (HUD visible) */
    SCREEN_RESULTS,     /* Results/menu screen */
    SCREEN_LOADING,     /* Loading screen */
} GameScreen;

/* Initialize screen detection (Phase 2) */
BOOL        ScreenDetect_Init(void);

/* Shutdown */
void        ScreenDetect_Shutdown(void);

/* Analyze current game screen. Returns SCREEN_UNKNOWN in Phase 1. */
GameScreen  ScreenDetect_Analyze(HWND game_hwnd);

#endif /* FOCUSKEEPER_SCREEN_DETECT_H */
