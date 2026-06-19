/*
 * farm_recovery.h - Error recovery and obstacle handling
 *
 * Handles: popup obstacles scanning, attempt_recovery,
 * restart_game, and VRAM-full detection.
 */

#ifndef FOCUSKEEPER_FARM_RECOVERY_H
#define FOCUSKEEPER_FARM_RECOVERY_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward decl - uses FarmEngine internals */
typedef struct FarmEngine FarmEngine;

/*
 * Scan for obstacle popups (images/obstacles/*.png).
 * If found, clicks them to dismiss.
 * Returns TRUE if an obstacle was found and handled.
 */
BOOL Recovery_ScanObstacles(HWND game_hwnd, const char *obstacles_dir);

/*
 * Advanced enter_menu with obstacle scanning.
 * Tries ESC + scans for known popups on each iteration.
 * Stronger version of Farm_EnterMenu for recovery scenarios.
 */
BOOL Recovery_AdvancedEnterMenu(HWND game_hwnd, const char *assets_dir,
                                const char *obstacles_dir);

/*
 * Attempt recovery from an unknown state.
 * Strategy: ESC multiple times, scan obstacles, try to reach menu.
 * Returns TRUE if successfully returned to a known state.
 */
BOOL Recovery_AttemptRecovery(HWND game_hwnd, const char *assets_dir,
                              const char *obstacles_dir);

/*
 * Check for VRAM-full notification (VRAMNE.png).
 * Returns TRUE if VRAM warning detected.
 */
BOOL Recovery_CheckVRAM(void);

/*
 * Restart the game via command.
 * restart_cmd: e.g. "start steam://run/2483190"
 * Returns TRUE if restart was initiated.
 */
BOOL Recovery_RestartGame(const char *restart_cmd);

#ifdef __cplusplus
}
#endif

#endif /* FOCUSKEEPER_FARM_RECOVERY_H */
