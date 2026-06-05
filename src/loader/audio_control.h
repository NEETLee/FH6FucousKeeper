#ifndef FOCUSKEEPER_AUDIO_CONTROL_H
#define FOCUSKEEPER_AUDIO_CONTROL_H

#include <windows.h>

/*
 * Audio Control - Process-level Mute via WASAPI
 *
 * Uses Windows Core Audio Session API to mute/unmute
 * a specific process by its PID.
 */

BOOL    AudioCtrl_Init(void);
void    AudioCtrl_Shutdown(void);
BOOL    AudioCtrl_MuteProcess(DWORD pid, BOOL mute);
BOOL    AudioCtrl_IsProcessMuted(DWORD pid);

#endif /* FOCUSKEEPER_AUDIO_CONTROL_H */
