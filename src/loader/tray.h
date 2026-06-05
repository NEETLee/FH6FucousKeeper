#ifndef FOCUSKEEPER_TRAY_H
#define FOCUSKEEPER_TRAY_H

#include <windows.h>

/*
 * System Tray Icon Management
 *
 * Handles the notification area (system tray) icon,
 * including tooltip, balloon notifications, and context menu.
 */

/* Tray icon states */
typedef enum {
    TRAY_STATE_IDLE,
    TRAY_STATE_ACTIVE,
    TRAY_STATE_ERROR
} TrayState;

/* ─── Public API ──────────────────────────────────────────────────── */

/* Create and show the tray icon */
BOOL    Tray_Create(HWND owner_hwnd, HINSTANCE hInstance);

/* Remove the tray icon */
void    Tray_Destroy(void);

/* Update tray icon state (changes icon and tooltip) */
void    Tray_SetState(TrayState state);

/* Show a balloon notification */
void    Tray_ShowBalloon(const WCHAR *title, const WCHAR *message, DWORD icon_type);

/* Handle tray callback message (call from WndProc) */
void    Tray_HandleMessage(HWND hwnd, WPARAM wParam, LPARAM lParam);

#endif /* FOCUSKEEPER_TRAY_H */
