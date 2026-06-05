/*
 * msg_replay.c - Message Replay Fallback (Active Object Pattern)
 *
 * Implements a background thread that continuously sends fake activation
 * messages to the target window. Used as a fallback when DLL injection
 * is not possible (Store/UWP version).
 *
 * The thread uses an event-based sleep to allow responsive shutdown
 * without busy-waiting or risking thread termination issues.
 */

#include "msg_replay.h"

/* ─── Module State ────────────────────────────────────────────────── */
static struct {
    HANDLE  thread;
    HANDLE  stop_event;
    HWND    target_hwnd;
    DWORD   interval_ms;
    BOOL    active;
    volatile LONG msg_count;
} s_replay = {0};

/* ─── Replay Thread ───────────────────────────────────────────────── */

static DWORD WINAPI ReplayThreadProc(LPVOID lpParam)
{
    (void)lpParam;

    while (WaitForSingleObject(s_replay.stop_event, s_replay.interval_ms) == WAIT_TIMEOUT) {
        HWND hwnd = s_replay.target_hwnd;

        if (!hwnd || !IsWindow(hwnd)) continue;

        /*
         * Send activation messages to keep the game thinking it's active.
         * Use PostMessage for non-blocking delivery to avoid deadlocks
         * with the game's message pump.
         */
        PostMessage(hwnd, WM_ACTIVATEAPP, (WPARAM)TRUE, 0);
        PostMessage(hwnd, WM_NCACTIVATE, (WPARAM)TRUE, (LPARAM)-1);
        PostMessage(hwnd, WM_ACTIVATE, MAKEWPARAM(WA_ACTIVE, 0), 0);

        InterlockedIncrement(&s_replay.msg_count);
    }

    return 0;
}

/* ─── Public API ──────────────────────────────────────────────────── */

BOOL MsgReplay_Start(HWND target_hwnd, DWORD interval_ms)
{
    if (s_replay.active) return FALSE;
    if (!IsWindow(target_hwnd)) return FALSE;

    s_replay.target_hwnd = target_hwnd;
    s_replay.interval_ms = (interval_ms > 0) ? interval_ms : MSG_REPLAY_DEFAULT_INTERVAL;
    s_replay.msg_count = 0;

    s_replay.stop_event = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!s_replay.stop_event) return FALSE;

    s_replay.thread = CreateThread(NULL, 0, ReplayThreadProc, NULL, 0, NULL);
    if (!s_replay.thread) {
        CloseHandle(s_replay.stop_event);
        s_replay.stop_event = NULL;
        return FALSE;
    }

    s_replay.active = TRUE;
    return TRUE;
}

void MsgReplay_Stop(void)
{
    if (!s_replay.active) return;

    /* Signal the thread to stop */
    SetEvent(s_replay.stop_event);

    /* Wait for thread to finish (with timeout to avoid hangs) */
    if (s_replay.thread) {
        WaitForSingleObject(s_replay.thread, 2000);
        CloseHandle(s_replay.thread);
        s_replay.thread = NULL;
    }

    if (s_replay.stop_event) {
        CloseHandle(s_replay.stop_event);
        s_replay.stop_event = NULL;
    }

    s_replay.active = FALSE;
    s_replay.target_hwnd = NULL;
}

BOOL MsgReplay_IsActive(void)
{
    return s_replay.active;
}

void MsgReplay_SetTarget(HWND target_hwnd)
{
    s_replay.target_hwnd = target_hwnd;
}

void MsgReplay_SetInterval(DWORD interval_ms)
{
    s_replay.interval_ms = (interval_ms > 0) ? interval_ms : MSG_REPLAY_DEFAULT_INTERVAL;
}

DWORD MsgReplay_GetInterval(void)
{
    return s_replay.interval_ms;
}

LONG MsgReplay_GetCount(void)
{
    return InterlockedCompareExchange(&s_replay.msg_count, 0, 0);
}

void MsgReplay_ResetCount(void)
{
    InterlockedExchange(&s_replay.msg_count, 0);
}
