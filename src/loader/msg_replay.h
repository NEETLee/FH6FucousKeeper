#ifndef FOCUSKEEPER_MSG_REPLAY_H
#define FOCUSKEEPER_MSG_REPLAY_H

#include <windows.h>

/*
 * Message Replay - Fallback Strategy for Store/UWP version
 *
 * When DLL injection is not possible (UWP sandbox), this module
 * provides an alternative: periodically sending fake activation
 * messages to keep the game thinking it's in the foreground.
 *
 * Uses a dedicated thread with configurable interval.
 */

/* ─── Public API ──────────────────────────────────────────────────── */

/* Start the message replay thread targeting the given window */
BOOL    MsgReplay_Start(HWND target_hwnd, DWORD interval_ms);

/* Stop the message replay thread */
void    MsgReplay_Stop(void);

/* Check if replay is currently active */
BOOL    MsgReplay_IsActive(void);

/* Update target window (without restart) */
void    MsgReplay_SetTarget(HWND target_hwnd);

/* Update replay interval (without restart) */
void    MsgReplay_SetInterval(DWORD interval_ms);

/* Get current replay interval */
DWORD   MsgReplay_GetInterval(void);

/* Get number of messages sent */
LONG    MsgReplay_GetCount(void);

/* Reset message counter */
void    MsgReplay_ResetCount(void);

/* Default replay interval (30ms ≈ 33 times/sec) */
#define MSG_REPLAY_DEFAULT_INTERVAL 30

#endif /* FOCUSKEEPER_MSG_REPLAY_H */
