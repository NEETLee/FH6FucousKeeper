/*
 * screen_detect.c - Game Screen Detection (Phase 2 Stub)
 *
 * Phase 1: Always returns SCREEN_UNKNOWN.
 * Phase 2 will add GDI BitBlt capture + pixel region analysis.
 */

#include "screen_detect.h"

BOOL ScreenDetect_Init(void)
{
    return TRUE;
}

void ScreenDetect_Shutdown(void)
{
}

GameScreen ScreenDetect_Analyze(HWND game_hwnd)
{
    (void)game_hwnd;
    return SCREEN_UNKNOWN;
}
