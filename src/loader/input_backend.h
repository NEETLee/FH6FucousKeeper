#ifndef FOCUSKEEPER_INPUT_BACKEND_H
#define FOCUSKEEPER_INPUT_BACKEND_H

#include <windows.h>

/*
 * InputBackend - Abstract interface for keyboard input injection.
 *
 * Decouples the auto-race engine from any specific injection mechanism.
 * Implementations may use IAT hooking (background), SendInput (foreground),
 * or a test stub.
 *
 * Usage:
 *   InputBackend *backend = HookBackend_Create();
 *   backend->init(backend);
 *   backend->key_down(backend, 'W');
 *   ...
 *   backend->shutdown(backend);
 *   backend->destroy(backend);
 */

typedef struct InputBackend {
    BOOL (*init)(struct InputBackend *self, HWND game_hwnd);
    void (*shutdown)(struct InputBackend *self);
    void (*key_down)(struct InputBackend *self, DWORD vk);
    void (*key_up)(struct InputBackend *self, DWORD vk);
    void (*release_all)(struct InputBackend *self);
    BOOL (*is_ready)(struct InputBackend *self);
    void (*destroy)(struct InputBackend *self);
} InputBackend;

#endif /* FOCUSKEEPER_INPUT_BACKEND_H */
