#ifndef FOCUSKEEPER_INPUT_HOOK_BACKEND_H
#define FOCUSKEEPER_INPUT_HOOK_BACKEND_H

#include "input_backend.h"

/*
 * HookBackend - InputBackend implementation via hook.dll IAT hooking.
 *
 * Calls Hook_VKeyDown/Up through shared memory. Activates IAT hooks
 * on init, deactivates on shutdown. Works in background.
 */

InputBackend* HookBackend_Create(void);

#endif /* FOCUSKEEPER_INPUT_HOOK_BACKEND_H */
