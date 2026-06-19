#include <windows.h>
#include <stdio.h>
extern "C" {
#include "screen_capture.h"
}

int wmain() {
    setvbuf(stdout, NULL, _IONBF, 0);
    wprintf(L"=== WGC Mini Test ===\n");
    HWND hwnd = FindWindowW(NULL, L"Forza Horizon 6");
    if (!hwnd) { wprintf(L"No FH6 window\n"); return 1; }
    wprintf(L"Window: %p\n", (void*)hwnd);

    wprintf(L"Init... ");
    if (!ScreenCapture_Init()) { wprintf(L"FAIL\n"); return 1; }
    wprintf(L"OK\n");

    wprintf(L"StartCapture... ");
    if (!ScreenCapture_StartCapture(hwnd)) { wprintf(L"FAIL\n"); ScreenCapture_Shutdown(); return 1; }
    wprintf(L"OK\n");

    wprintf(L"Waiting 500ms...\n");
    Sleep(500);

    CaptureFrame frame = {};
    wprintf(L"GrabFrame... ");
    if (!ScreenCapture_GrabFrame(&frame)) { wprintf(L"FAIL\n"); }
    else { wprintf(L"OK (%dx%d)\n", frame.width, frame.height); }

    ScreenCapture_StopCapture();
    ScreenCapture_Shutdown();
    wprintf(L"Done.\n");
    return 0;
}
