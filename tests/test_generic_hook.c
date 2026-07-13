/*
 * Generic-window anti-pause handshake test.
 *
 * Creates a non-FH6 Win32 target on another thread, attaches hook.dll, verifies
 * the target-process subclass acknowledgement and one intercepted focus event.
 */
#include <windows.h>
#include <stdio.h>
#include "hook_manager.h"

static volatile HWND g_target = NULL;
static const WCHAR *CLASS_NAME = L"FocusKeeperGenericHookTest";

static LRESULT CALLBACK TargetProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_CLOSE) {
        DestroyWindow(hwnd);
        return 0;
    }
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static DWORD WINAPI TargetThread(LPVOID unused) {
    (void)unused;
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = TargetProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = CLASS_NAME;
    RegisterClassW(&wc);
    g_target = CreateWindowExW(0, CLASS_NAME, L"Generic hook test target",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 640, 360,
        NULL, NULL, wc.hInstance, NULL);
    if (!g_target) return 1;

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}

int main(void) {
    HANDLE thread = CreateThread(NULL, 0, TargetThread, NULL, 0, NULL);
    if (!thread) return 2;
    for (int i = 0; i < 100 && !g_target; i++) Sleep(20);
    if (!g_target) return 3;

    if (!HookMgr_Init()) {
        wprintf(L"HookMgr_Init failed: %ls\n", HookMgr_GetLastError());
        return 4;
    }
    if (!HookMgr_Attach((HWND)g_target)) {
        wprintf(L"HookMgr_Attach failed: %ls\n", HookMgr_GetLastError());
        return 5;
    }

    SendMessageW((HWND)g_target, WM_KILLFOCUS, 0, 0);
    Sleep(50);
    LONG killfocus = 0;
    HookMgr_GetStats(&killfocus, NULL, NULL, NULL);
    printf("generic hook acknowledged; killfocus=%ld\n", killfocus);

    HookMgr_Detach();
    HookMgr_Shutdown();
    PostMessageW((HWND)g_target, WM_CLOSE, 0, 0);
    WaitForSingleObject(thread, 2000);
    CloseHandle(thread);

    if (killfocus < 1) {
        puts("generic hook test FAILED");
        return 1;
    }
    puts("generic hook test PASSED");
    return 0;
}
