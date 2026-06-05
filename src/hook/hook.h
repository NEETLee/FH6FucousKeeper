#ifndef FOCUSKEEPER_HOOK_H
#define FOCUSKEEPER_HOOK_H

#include <windows.h>

/*
 * Shared header between hook.dll and loader.exe.
 * Defines the DLL export interface and shared data structures.
 */

#ifdef HOOK_EXPORTS
#define HOOK_API __declspec(dllexport)
#else
#define HOOK_API __declspec(dllimport)
#endif

/* Statistics counters for intercepted messages */
typedef struct {
    volatile LONG killfocus_count;
    volatile LONG activateapp_count;
    volatile LONG ncactivate_count;
    volatile LONG activate_count;
} HookStats;

/* Hook state flags */
typedef struct {
    HWND  target_hwnd;
    BOOL  active;
    DWORD target_thread_id;
    DWORD target_pid;
} HookState;

/* DLL exported functions */
HOOK_API BOOL    Hook_Install(HWND target_hwnd);
HOOK_API void    Hook_Uninstall(void);
HOOK_API BOOL    Hook_IsActive(void);
HOOK_API void    Hook_GetStats(HookStats *out);
HOOK_API void    Hook_ResetStats(void);
HOOK_API HWND    Hook_GetTarget(void);

/* Custom message for triggering subclass from within the target process */
#define WM_HOOK_SUBCLASS  (WM_USER + 0x100)
#define WM_HOOK_UNSUBCLASS (WM_USER + 0x101)

#endif /* FOCUSKEEPER_HOOK_H */
