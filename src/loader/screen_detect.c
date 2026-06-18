/*
 * screen_detect.c - Game Screen State Detection
 *
 * Orchestrates screen capture and OCR to determine which screen
 * the game is currently on. Uses a two-pass approach:
 *   1. Quick pixel sampling for fast state classification
 *   2. Targeted OCR on specific regions for detailed info (WGC build only)
 */

#include "screen_detect.h"
#include "screen_capture.h"
#ifdef USE_WGC_CAPTURE
#include "ocr_engine.h"
#endif
#include "logger.h"

#include <wchar.h>
#include <stdlib.h>
#include <string.h>

/* Suppress unused warnings for items used only in WGC build */
#ifndef USE_WGC_CAPTURE
#define UNUSED_ATTR __attribute__((unused))
#else
#define UNUSED_ATTR
#endif

/* ─── Region Definitions (normalized 0.0-1.0 coordinates) ─────── */
/* These define where key UI elements appear in FH6 at any resolution */

typedef struct {
    float x, y, w, h;
} NormRect;

/* Skill points display: top-right area */
static const NormRect REGION_SKILL_POINTS UNUSED_ATTR = {0.85f, 0.02f, 0.14f, 0.05f};

/* Car name in skill tree: top-center */
static const NormRect REGION_CAR_NAME UNUSED_ATTR = {0.25f, 0.03f, 0.50f, 0.06f};

/* Center prompt area (for confirmations) */
static const NormRect REGION_CENTER_PROMPT UNUSED_ATTR = {0.30f, 0.40f, 0.40f, 0.20f};

/* ─── Pixel Sampling Points (normalized coordinates) ──────────── */
/* Key positions to sample for fast screen classification */

typedef struct {
    float x, y;
    BYTE  r_min, g_min, b_min;
    BYTE  r_max, g_max, b_max;
} PixelCheck;

/*
 * Skill tree background has a distinctive dark blue-purple tone.
 * Loading screens tend to be mostly black.
 * Gameplay has the HUD with specific color indicators.
 *
 * NOTE: These values need calibration with actual FH6 screenshots.
 * They are initial estimates based on FH5 UI patterns.
 */

static const PixelCheck SKILL_TREE_CHECKS[] = {
    /* Dark background in skill tree corners */
    {0.05f, 0.50f, 0, 0, 20, 40, 30, 80},
    /* Orange/gold highlight of active node area */
    {0.50f, 0.50f, 180, 100, 0, 255, 180, 60},
};

static const PixelCheck LOADING_CHECKS[] = {
    /* Nearly black center during loading */
    {0.50f, 0.50f, 0, 0, 0, 30, 30, 30},
    {0.25f, 0.50f, 0, 0, 0, 30, 30, 30},
    {0.75f, 0.50f, 0, 0, 0, 30, 30, 30},
};

/* ─── Internal State ─────────────────────────────────────────────── */

static struct {
    HWND  game_hwnd;
    BOOL  session_active;
    BOOL  ocr_ready;
} s_detect = {0};

/* ─── Helper: Get pixel at normalized coordinate ─────────────────── */

static BOOL GetPixelAt(const CaptureFrame *frame, float nx, float ny,
                       BYTE *r, BYTE *g, BYTE *b)
{
    int x = (int)(nx * frame->width);
    int y = (int)(ny * frame->height);

    if (x < 0 || x >= frame->width || y < 0 || y >= frame->height)
        return FALSE;

    int offset = y * frame->stride + x * 4;
    *b = frame->pixels[offset + 0];
    *g = frame->pixels[offset + 1];
    *r = frame->pixels[offset + 2];
    return TRUE;
}

/* ─── Helper: Check if pixel matches range ───────────────────────── */

static BOOL PixelInRange(const CaptureFrame *frame, const PixelCheck *check)
{
    BYTE r, g, b;
    if (!GetPixelAt(frame, check->x, check->y, &r, &g, &b))
        return FALSE;

    return (r >= check->r_min && r <= check->r_max &&
            g >= check->g_min && g <= check->g_max &&
            b >= check->b_min && b <= check->b_max);
}

/* ─── Helper: Normalized rect to pixel RECT ──────────────────────── */

#ifdef USE_WGC_CAPTURE
static RECT NormToPixelRect(const NormRect *norm, int width, int height)
{
    RECT rc;
    rc.left   = (LONG)(norm->x * width);
    rc.top    = (LONG)(norm->y * height);
    rc.right  = (LONG)((norm->x + norm->w) * width);
    rc.bottom = (LONG)((norm->y + norm->h) * height);
    return rc;
}
#endif

/* ─── Helper: Parse skill points from OCR text ───────────────────── */

#ifdef USE_WGC_CAPTURE
static int ParseSkillPoints(const WCHAR *text)
{
    int value = 0;
    BOOL found_digit = FALSE;

    for (const WCHAR *p = text; *p; p++) {
        if (*p >= L'0' && *p <= L'9') {
            value = value * 10 + (*p - L'0');
            found_digit = TRUE;
        } else if (*p == L',' || *p == L'.' || *p == L' ') {
            /* Skip thousand separators */
            continue;
        } else if (found_digit) {
            break;
        }
    }

    return found_digit ? value : -1;
}
#endif

/* ─── Quick Check (pixel sampling only) ──────────────────────────── */

static GameScreen DoQuickCheck(const CaptureFrame *frame)
{
    /* Check for loading screen (mostly black) */
    int loading_matches = 0;
    for (int i = 0; i < 3; i++) {
        if (PixelInRange(frame, &LOADING_CHECKS[i]))
            loading_matches++;
    }
    if (loading_matches >= 3)
        return SCREEN_LOADING;

    /* Check for skill tree (distinctive background) */
    int skill_matches = 0;
    for (int i = 0; i < 2; i++) {
        if (PixelInRange(frame, &SKILL_TREE_CHECKS[i]))
            skill_matches++;
    }
    if (skill_matches >= 2)
        return SCREEN_SKILL_TREE;

    /*
     * TODO: Add more checks for other screens.
     * For now, default to GAMEPLAY if nothing else matches.
     * This will be refined with actual game screenshots.
     */
    return SCREEN_UNKNOWN;
}

/* ─── Public API ─────────────────────────────────────────────────── */

BOOL ScreenDetect_Init(void)
{
    if (!ScreenCapture_Init()) {
        Logger_Log(LOG_ERROR, L"[ScreenDetect] Failed to init screen capture");
        return FALSE;
    }

#ifdef USE_WGC_CAPTURE
    s_detect.ocr_ready = OcrEngine_Init(L"en-US");
    if (!s_detect.ocr_ready) {
        Logger_Log(LOG_WARN, L"[ScreenDetect] OCR init failed, text recognition unavailable");
    }
#else
    s_detect.ocr_ready = FALSE;
#endif

    return TRUE;
}

void ScreenDetect_Shutdown(void)
{
    ScreenDetect_StopSession();
#ifdef USE_WGC_CAPTURE
    OcrEngine_Shutdown();
#endif
    ScreenCapture_Shutdown();
}

BOOL ScreenDetect_StartSession(HWND game_hwnd)
{
    if (!game_hwnd) return FALSE;

    if (s_detect.session_active)
        ScreenDetect_StopSession();

    if (!ScreenCapture_StartCapture(game_hwnd)) {
        Logger_Log(LOG_ERROR, L"[ScreenDetect] Failed to start capture for window");
        return FALSE;
    }

    s_detect.game_hwnd = game_hwnd;
    s_detect.session_active = TRUE;
    Logger_Log(LOG_INFO, L"[ScreenDetect] Session started");
    return TRUE;
}

void ScreenDetect_StopSession(void)
{
    if (!s_detect.session_active) return;

    ScreenCapture_StopCapture();
    s_detect.game_hwnd = NULL;
    s_detect.session_active = FALSE;
    Logger_Log(LOG_INFO, L"[ScreenDetect] Session stopped");
}

GameScreen ScreenDetect_QuickCheck(void)
{
    if (!s_detect.session_active) return SCREEN_UNKNOWN;

    CaptureFrame frame;
    if (!ScreenCapture_GrabFrame(&frame)) return SCREEN_UNKNOWN;

    return DoQuickCheck(&frame);
}

BOOL ScreenDetect_FullAnalyze(ScreenState *state)
{
    if (!state) return FALSE;
    memset(state, 0, sizeof(*state));
    state->skill_points = -1;
    state->confidence = 0.0f;

    if (!s_detect.session_active) return FALSE;

    CaptureFrame frame;
    if (!ScreenCapture_GrabFrame(&frame)) return FALSE;

    /* Pass 1: Quick pixel-based classification */
    state->screen = DoQuickCheck(&frame);

    /* Pass 2: OCR for detailed info (only on relevant screens) */
#ifdef USE_WGC_CAPTURE
    if (s_detect.ocr_ready && state->screen == SCREEN_SKILL_TREE) {
        OcrResult ocr_result;

        /* Read skill points */
        RECT sp_rect = NormToPixelRect(&REGION_SKILL_POINTS, frame.width, frame.height);
        if (OcrEngine_RecognizeRegion(frame.pixels, frame.width, frame.height,
                                      frame.stride, sp_rect, &ocr_result)) {
            state->skill_points = ParseSkillPoints(ocr_result.all_text);
        }

        /* Read car name */
        RECT name_rect = NormToPixelRect(&REGION_CAR_NAME, frame.width, frame.height);
        if (OcrEngine_RecognizeRegion(frame.pixels, frame.width, frame.height,
                                      frame.stride, name_rect, &ocr_result)) {
            wcsncpy(state->car_name, ocr_result.all_text, 127);
        }

        state->confidence = 0.8f;
    } else if (state->screen == SCREEN_LOADING) {
#else
    if (state->screen == SCREEN_LOADING) {
#endif
        state->confidence = 0.9f;
    } else {
        state->confidence = 0.5f;
    }

    return TRUE;
}

/* Legacy API: backward compatible */
GameScreen ScreenDetect_Analyze(HWND game_hwnd)
{
    if (!s_detect.session_active) {
        if (game_hwnd && game_hwnd != s_detect.game_hwnd) {
            ScreenDetect_StartSession(game_hwnd);
        }
    }
    return ScreenDetect_QuickCheck();
}

BOOL ScreenDetect_IsActive(void)
{
    return s_detect.session_active;
}
