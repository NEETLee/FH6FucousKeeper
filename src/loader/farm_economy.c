/*
 * farm_economy.c - OCR-based economy reading + buy-quantity calculation
 */

#include "farm_economy.h"
#include "ocr_engine.h"

#include <wchar.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Normalized crop rects (vw/vh). CR crop is a tight strip on the CR number
 * row only (below the player name, above FPS overlay) so username digits are
 * never included. SP crop covers the "NN 技术点数可用" tile. */
const EconRect ECON_REGION_BALANCE = { 0.580f, 0.142f, 0.400f, 0.050f };
const EconRect ECON_REGION_SKILL   = { 0.220f, 0.665f, 0.260f, 0.140f };

#define ECON_CR_MIN_DIGITS 4
#define ECON_CR_MIN  1000L
#define ECON_CR_MAX  2000000000L

static BOOL s_ready = FALSE;

BOOL FarmEconomy_Init(const WCHAR *lang) {
    if (s_ready) return TRUE;
    if (!OcrEngine_Init(lang) && !OcrEngine_Init(NULL)) return FALSE;
    s_ready = TRUE;
    return TRUE;
}

void FarmEconomy_Shutdown(void) {
    if (!s_ready) return;
    OcrEngine_Shutdown();
    s_ready = FALSE;
}

BOOL FarmEconomy_IsReady(void) { return s_ready; }

static int ParseInt(const WCHAR *text) {
    long value = 0;
    BOOL found = FALSE;
    for (const WCHAR *p = text; *p; p++) {
        if (*p >= L'0' && *p <= L'9') {
            value = value * 10 + (*p - L'0');
            found = TRUE;
            if (value > ECON_CR_MAX) return -1;
        }
    }
    return found ? (int)value : -1;
}

static int DigitCount(const WCHAR *text) {
    int n = 0;
    for (const WCHAR *p = text; *p; p++)
        if (*p >= L'0' && *p <= L'9') n++;
    return n;
}

static RECT EconRectToPixels(EconRect roi, int width, int height) {
    RECT rc;
    rc.left   = (LONG)(roi.x * width);
    rc.top    = (LONG)(roi.y * height);
    rc.right  = (LONG)((roi.x + roi.w) * width);
    rc.bottom = (LONG)((roi.y + roi.h) * height);
    if (rc.left < 0) rc.left = 0;
    if (rc.top < 0) rc.top = 0;
    if (rc.right > width) rc.right = width;
    if (rc.bottom > height) rc.bottom = height;
    return rc;
}

/* Nearest-neighbour upscale so Windows OCR sees larger HUD digits. */
static BYTE *UpscaleBgra(const BYTE *src, int w, int h, int src_stride,
                         int scale, int *out_w, int *out_h, int *out_stride) {
    if (scale < 1) scale = 1;
    int dw = w * scale;
    int dh = h * scale;
    int ds = dw * 4;
    BYTE *dst = (BYTE *)malloc((size_t)ds * (size_t)dh);
    if (!dst) return NULL;

    for (int y = 0; y < dh; y++) {
        int sy = y / scale;
        const BYTE *srow = src + sy * src_stride;
        BYTE *drow = dst + y * ds;
        for (int x = 0; x < dw; x++) {
            int sx = x / scale;
            const BYTE *sp = srow + sx * 4;
            BYTE *dp = drow + x * 4;
            dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2]; dp[3] = sp[3];
        }
    }
    *out_w = dw;
    *out_h = dh;
    *out_stride = ds;
    return dst;
}

typedef struct { LONG x; WCHAR text[64]; } SortWord;

static int cmp_sortword_x(const void *a, const void *b) {
    LONG xa = ((const SortWord *)a)->x;
    LONG xb = ((const SortWord *)b)->x;
    return (xa > xb) - (xa < xb);
}

/* Parse digits from an OCR result (words sorted left-to-right). */
static int ParseResultDigits(const OcrResult *result, int min_digits) {
    SortWord words[OCR_MAX_WORDS * OCR_MAX_LINES];
    int n = 0;

    for (int i = 0; i < result->line_count; i++) {
        const OcrLine *line = &result->lines[i];
        for (int j = 0; j < line->word_count; j++) {
            const OcrWord *w = &line->words[j];
            if (DigitCount(w->text) < 1) continue;
            if (n < (int)(sizeof(words) / sizeof(words[0]))) {
                words[n].x = w->bounds.left;
                wcsncpy(words[n].text, w->text, 63);
                words[n].text[63] = 0;
                n++;
            }
        }
    }
    if (n == 0) return -1;

    qsort(words, n, sizeof(words[0]), cmp_sortword_x);

    WCHAR joined[512];
    joined[0] = 0;
    for (int i = 0; i < n; i++) {
        wcsncat(joined, words[i].text,
                sizeof(joined) / sizeof(joined[0]) - wcslen(joined) - 1);
    }

    if (DigitCount(joined) < min_digits) return -1;
    return ParseInt(joined);
}

/*
 * Crop an explicit pixel RECT, optionally upscale, OCR only that patch.
 * Shared core for both the normalized-ROI and anchor-based read paths.
 */
static int ReadNumberRect(const BYTE *pixels, int width, int height, int stride,
                          RECT rc, int upscale, int min_digits) {
    if (!s_ready || !pixels || width <= 0 || height <= 0) return -1;

    if (rc.left < 0) rc.left = 0;
    if (rc.top  < 0) rc.top  = 0;
    if (rc.right  > width)  rc.right  = width;
    if (rc.bottom > height) rc.bottom = height;

    int rw = rc.right - rc.left;
    int rh = rc.bottom - rc.top;
    if (rw < 8 || rh < 4) return -1;

    static OcrResult result;

    /* Pass 1: direct region OCR (no upscale). */
    if (OcrEngine_RecognizeRegion(pixels, width, height, stride, rc, &result)) {
        int val = ParseResultDigits(&result, min_digits);
        if (val >= (int)ECON_CR_MIN && val <= (int)ECON_CR_MAX) return val;
        if (min_digits <= 1 && val >= 0) return val;
    }

    /* Pass 2: crop + upscale + OCR (better for small white digits). */
    int crop_stride = rw * 4;
    BYTE *crop = (BYTE *)malloc((size_t)crop_stride * (size_t)rh);
    if (!crop) return -1;

    for (int y = 0; y < rh; y++) {
        const BYTE *src = pixels + (rc.top + y) * stride + rc.left * 4;
        memcpy(crop + y * crop_stride, src, (size_t)crop_stride);
    }

    if (upscale < 2) upscale = 2;
    int uw, uh, ustride;
    BYTE *up = UpscaleBgra(crop, rw, rh, crop_stride, upscale, &uw, &uh, &ustride);
    free(crop);
    if (!up) return -1;

    memset(&result, 0, sizeof(result));
    if (OcrEngine_Recognize(up, uw, uh, ustride, &result)) {
        int val = ParseResultDigits(&result, min_digits);
        free(up);
        if (val >= (int)ECON_CR_MIN && val <= (int)ECON_CR_MAX) return val;
        if (min_digits <= 1 && val >= 0) return val;
    } else {
        free(up);
    }

    return -1;
}

/* Normalized-ROI wrapper over the pixel-RECT core. */
static int ReadCroppedNumber(const BYTE *pixels, int width, int height, int stride,
                             EconRect roi, int upscale, int min_digits) {
    RECT rc = EconRectToPixels(roi, width, height);
    return ReadNumberRect(pixels, width, height, stride, rc, upscale, min_digits);
}

int FarmEconomy_ReadNumberRectPx(const BYTE *pixels, int width, int height, int stride,
                                 RECT rc, int upscale, int min_digits) {
    return ReadNumberRect(pixels, width, height, stride, rc, upscale, min_digits);
}

int FarmEconomy_ReadNumber(const BYTE *pixels, int width, int height,
                           int stride, EconRect roi) {
    return ReadCroppedNumber(pixels, width, height, stride, roi, 2, 1);
}

int FarmEconomy_ReadBalance(const BYTE *pixels, int width, int height, int stride) {
    /* 3x upscale on the CR strip; require >= 4 digits. */
    return ReadCroppedNumber(pixels, width, height, stride,
                             ECON_REGION_BALANCE, 3, ECON_CR_MIN_DIGITS);
}

int FarmEconomy_ReadSkillPoints(const BYTE *pixels, int width, int height, int stride) {
    return ReadCroppedNumber(pixels, width, height, stride,
                             ECON_REGION_SKILL, 2, 1);
}

int FarmEconomy_ComputeCount(long balance, long skill_points,
                             long cost_per_car, long sp_per_car) {
    if (cost_per_car <= 0) cost_per_car = 81700;
    if (sp_per_car   <= 0) sp_per_car   = 30;

    BOOL cr_ok = (balance >= ECON_CR_MIN && balance <= ECON_CR_MAX);
    long by_cr = cr_ok ? (balance / cost_per_car) : -1;
    long by_sp = (skill_points >= 0) ? (skill_points / sp_per_car)   : -1;

    long n;
    if (by_cr < 0 && by_sp < 0)      n = 0;
    else if (by_cr < 0)              n = by_sp;
    else if (by_sp < 0)              n = by_cr;
    else                             n = (by_cr < by_sp) ? by_cr : by_sp;

    if (n < 0) n = 0;
    return (int)n;
}
