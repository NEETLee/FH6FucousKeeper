#ifndef FOCUSKEEPER_SCREEN_CAPTURE_H
#define FOCUSKEEPER_SCREEN_CAPTURE_H

#include <windows.h>

/*
 * Screen Capture - Background Window Capture via Windows Graphics Capture API
 *
 * Captures game window contents without requiring it to be in the foreground.
 * Requires Windows 10 1903+ (build 18362).
 * Implementation uses WinRT/COM in C++ but exposes a pure C interface.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    BYTE   *pixels;       /* BGRA pixel data (caller must NOT free directly) */
    int     width;
    int     height;
    int     stride;       /* bytes per row (may include padding) */
} CaptureFrame;

/* Initialize the capture system. Call once at startup. */
BOOL ScreenCapture_Init(void);

/* Shutdown and release all resources. */
void ScreenCapture_Shutdown(void);

/*
 * Start capturing a specific window.
 * Returns TRUE if capture session started successfully.
 * The window must be in windowed mode (not exclusive fullscreen).
 */
BOOL ScreenCapture_StartCapture(HWND target_hwnd);

/* Stop the current capture session. */
void ScreenCapture_StopCapture(void);

/*
 * Grab the latest frame from the capture session.
 * Returns TRUE and fills `frame` if a frame is available.
 * The frame data is valid until the next call to GrabFrame or StopCapture.
 */
BOOL ScreenCapture_GrabFrame(CaptureFrame *frame);

/* Check if capture is currently active. */
BOOL ScreenCapture_IsActive(void);

#ifdef __cplusplus
}
#endif

#endif /* FOCUSKEEPER_SCREEN_CAPTURE_H */
