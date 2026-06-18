#ifndef FOCUSKEEPER_OCR_ENGINE_H
#define FOCUSKEEPER_OCR_ENGINE_H

#include <windows.h>

/*
 * OCR Engine - Windows OCR API Wrapper
 *
 * Wraps the Windows.Media.Ocr WinRT API for text recognition.
 * Operates on raw BGRA pixel buffers (from screen capture).
 * Requires Windows 10 1809+ with an OCR language pack installed.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define OCR_MAX_TEXT_LEN   512
#define OCR_MAX_LINES      32
#define OCR_MAX_WORDS      64

typedef struct {
    WCHAR  text[OCR_MAX_TEXT_LEN];
    RECT   bounds;       /* bounding box in pixel coordinates */
    float  confidence;   /* 0.0 - 1.0 (not always available from WinOCR) */
} OcrWord;

typedef struct {
    OcrWord words[OCR_MAX_WORDS];
    int     word_count;
    WCHAR   full_text[OCR_MAX_TEXT_LEN * 4];  /* concatenated line text */
} OcrLine;

typedef struct {
    OcrLine lines[OCR_MAX_LINES];
    int     line_count;
    WCHAR   all_text[OCR_MAX_TEXT_LEN * 8];   /* all text concatenated */
} OcrResult;

/* Initialize OCR engine with specified language (e.g. L"en-US", L"zh-Hans-CN"). */
BOOL OcrEngine_Init(const WCHAR *language_tag);

/* Shutdown OCR engine. */
void OcrEngine_Shutdown(void);

/*
 * Recognize text in a BGRA pixel buffer.
 * `pixels` - raw BGRA data
 * `width`, `height` - image dimensions
 * `stride` - bytes per row
 * `result` - output OCR result (caller provides buffer)
 * Returns TRUE on success.
 */
BOOL OcrEngine_Recognize(const BYTE *pixels, int width, int height, int stride,
                         OcrResult *result);

/*
 * Recognize text in a sub-region (ROI) of a BGRA pixel buffer.
 * More efficient than full-image OCR when you know where text is.
 */
BOOL OcrEngine_RecognizeRegion(const BYTE *pixels, int img_width, int img_height,
                               int stride, RECT roi, OcrResult *result);

/* Check if OCR engine is ready. */
BOOL OcrEngine_IsReady(void);

#ifdef __cplusplus
}
#endif

#endif /* FOCUSKEEPER_OCR_ENGINE_H */
