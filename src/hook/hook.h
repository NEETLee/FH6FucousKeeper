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

/* ─── Virtual Input (shared between loader and game process) ─────── */

/* Bitmap: 256 virtual keys, 1 bit each = 32 bytes */
#define VK_BITMAP_SIZE 32

/* DLL exported functions */
HOOK_API BOOL    Hook_Install(HWND target_hwnd);
HOOK_API void    Hook_Uninstall(void);
HOOK_API BOOL    Hook_IsActive(void);
HOOK_API void    Hook_GetStats(HookStats *out);
HOOK_API void    Hook_ResetStats(void);
HOOK_API HWND    Hook_GetTarget(void);

/* Virtual input API (operates on shared memory bitmap) */
HOOK_API void    Hook_VKeyDown(int vk);
HOOK_API void    Hook_VKeyUp(int vk);
HOOK_API void    Hook_VKeyReleaseAll(void);
HOOK_API BOOL    Hook_VKeyIsDown(int vk);
HOOK_API BOOL    Hook_IsInputHooked(void);

/* Enable/disable IAT hooks for input (call only when auto-race needs it) */
HOOK_API BOOL    Hook_EnableInputHooks(void);
HOOK_API void    Hook_DisableInputHooks(void);

/* Custom message for triggering subclass from within the target process */
#define WM_HOOK_SUBCLASS      (WM_USER + 0x100)
#define WM_HOOK_UNSUBCLASS    (WM_USER + 0x101)
#define WM_HOOK_ENABLE_INPUT  (WM_USER + 0x102)
#define WM_HOOK_DISABLE_INPUT (WM_USER + 0x103)

#endif /* FOCUSKEEPER_HOOK_H */
