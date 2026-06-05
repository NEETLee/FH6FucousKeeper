#ifndef FOCUSKEEPER_HOOK_MANAGER_H
#define FOCUSKEEPER_HOOK_MANAGER_H

#include <windows.h>
#include "window_finder.h"

/*
 * Hook Manager - Facade Pattern
 *
 * Encapsulates the complexity of loading/unloading the hook DLL,
 * managing the hook lifecycle, and providing a clean interface
 * to the rest of the application.
 */

/* Hook manager state */
typedef enum {
    HOOK_STATE_IDLE,
    HOOK_STATE_ACTIVE,
    HOOK_STATE_ERROR
} HookManagerState;

/* Callback for state change notifications (Observer pattern) */
typedef void (*HookStateCallback)(HookManagerState new_state, const WCHAR *message);

/* ─── Public API ──────────────────────────────────────────────────── */

/* Initialize the hook manager, load hook.dll */
BOOL    HookMgr_Init(void);

/* Cleanup and release resources */
void    HookMgr_Shutdown(void);

/* Install hook on target window */
BOOL    HookMgr_Attach(HWND target_hwnd);

/* Remove hook from target window */
void    HookMgr_Detach(void);

/* Query current state */
HookManagerState HookMgr_GetState(void);

/* Get last error description */
const WCHAR* HookMgr_GetLastError(void);

/* Register state change callback */
void    HookMgr_SetCallback(HookStateCallback callback);

/* Get intercepted message statistics */
void    HookMgr_GetStats(LONG *killfocus, LONG *activateapp,
                         LONG *ncactivate, LONG *activate);

/* Reset statistics counters */
void    HookMgr_ResetStats(void);

#endif /* FOCUSKEEPER_HOOK_MANAGER_H */
