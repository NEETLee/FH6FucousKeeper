/*
 * ocr_engine.cpp - Windows OCR API Wrapper
 *
 * Uses Windows.Media.Ocr (WinRT) for text recognition on raw pixel buffers.
 * Exposes a pure C interface via ocr_engine.h.
 */

#include "ocr_engine.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>

#include <wchar.h>
#include <string.h>

namespace winrt_ocr    = winrt::Windows::Media::Ocr;
namespace winrt_img    = winrt::Windows::Graphics::Imaging;
namespace winrt_global = winrt::Windows::Globalization;

/* ─── Internal State ─────────────────────────────────────────────── */

static struct {
    winrt_ocr::OcrEngine engine{nullptr};
    bool initialized;
} s_ocr = {};

/* ─── Helper: Create SoftwareBitmap from BGRA buffer ─────────────── */

static winrt_img::SoftwareBitmap CreateBitmapFromPixels(
    const BYTE *pixels, int width, int height, int stride)
{
    auto bitmap = winrt_img::SoftwareBitmap(
        winrt_img::BitmapPixelFormat::Bgra8,
        width, height,
        winrt_img::BitmapAlphaMode::Premultiplied
    );

    auto buffer = bitmap.LockBuffer(winrt_img::BitmapBufferAccessMode::Write);
    auto ref = buffer.CreateReference();
    auto access = ref.as<::Windows::Foundation::IMemoryBufferByteAccess>();

    BYTE *dst_data = nullptr;
    UINT32 dst_capacity = 0;
    access->GetBuffer(&dst_data, &dst_capacity);

    auto plane = buffer.GetPlaneDescription(0);
    int dst_stride = plane.Stride;

    for (int y = 0; y < height; y++) {
        memcpy(dst_data + y * dst_stride,
               pixels + y * stride,
               width * 4);
    }

    ref.Close();
    buffer.Close();
    return bitmap;
}

/* ─── Helper: Extract ROI pixels ─────────────────────────────────── */

static BYTE* ExtractROI(const BYTE *pixels, int img_width, int img_height,
                        int stride, RECT roi, int *out_w, int *out_h)
{
    int rx = (roi.left < 0) ? 0 : roi.left;
    int ry = (roi.top  < 0) ? 0 : roi.top;
    int rw = roi.right - roi.left;
    int rh = roi.bottom - roi.top;

    if (rx + rw > img_width)  rw = img_width - rx;
    if (ry + rh > img_height) rh = img_height - ry;
    if (rw <= 0 || rh <= 0)   return nullptr;

    int roi_stride = rw * 4;
    BYTE *roi_pixels = (BYTE*)malloc(roi_stride * rh);
    if (!roi_pixels) return nullptr;

    for (int y = 0; y < rh; y++) {
        memcpy(roi_pixels + y * roi_stride,
               pixels + (ry + y) * stride + rx * 4,
               roi_stride);
    }

    *out_w = rw;
    *out_h = rh;
    return roi_pixels;
}

/* ─── Helper: Convert OcrResult from WinRT to C struct ───────────── */

static void ConvertOcrResult(winrt_ocr::OcrResult const &winrt_result, OcrResult *result)
{
    memset(result, 0, sizeof(*result));

    int line_idx = 0;
    WCHAR *all_ptr = result->all_text;
    int all_remaining = (int)(sizeof(result->all_text) / sizeof(WCHAR)) - 1;

    for (auto const &line : winrt_result.Lines()) {
        if (line_idx >= OCR_MAX_LINES) break;

        OcrLine *ocr_line = &result->lines[line_idx];
        int word_idx = 0;
        WCHAR *line_ptr = ocr_line->full_text;
        int line_remaining = (int)(sizeof(ocr_line->full_text) / sizeof(WCHAR)) - 1;

        for (auto const &word : line.Words()) {
            if (word_idx >= OCR_MAX_WORDS) break;

            OcrWord *ocr_word = &ocr_line->words[word_idx];
            auto text = word.Text();
            wcsncpy(ocr_word->text, text.c_str(), OCR_MAX_TEXT_LEN - 1);

            auto rect = word.BoundingRect();
            ocr_word->bounds.left   = (LONG)rect.X;
            ocr_word->bounds.top    = (LONG)rect.Y;
            ocr_word->bounds.right  = (LONG)(rect.X + rect.Width);
            ocr_word->bounds.bottom = (LONG)(rect.Y + rect.Height);
            ocr_word->confidence = 1.0f;

            int text_len = (int)wcslen(ocr_word->text);
            if (word_idx > 0 && line_remaining > 1) {
                *line_ptr++ = L' ';
                line_remaining--;
            }
            if (text_len < line_remaining) {
                wcscpy(line_ptr, ocr_word->text);
                line_ptr += text_len;
                line_remaining -= text_len;
            }

            word_idx++;
        }
        ocr_line->word_count = word_idx;

        int full_len = (int)wcslen(ocr_line->full_text);
        if (line_idx > 0 && all_remaining > 1) {
            *all_ptr++ = L'\n';
            all_remaining--;
        }
        if (full_len < all_remaining) {
            wcscpy(all_ptr, ocr_line->full_text);
            all_ptr += full_len;
            all_remaining -= full_len;
        }

        line_idx++;
    }
    result->line_count = line_idx;
}

/* ─── Public C API ───────────────────────────────────────────────── */

extern "C" {

BOOL OcrEngine_Init(const WCHAR *language_tag)
{
    if (s_ocr.initialized) return TRUE;

    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (winrt::hresult_error const &) {
        /* Already initialized - that's fine */
    }

    try {
        if (language_tag && language_tag[0]) {
            auto lang = winrt_global::Language(language_tag);
            if (winrt_ocr::OcrEngine::IsLanguageSupported(lang)) {
                s_ocr.engine = winrt_ocr::OcrEngine::TryCreateFromLanguage(lang);
            }
        }

        if (!s_ocr.engine) {
            s_ocr.engine = winrt_ocr::OcrEngine::TryCreateFromUserProfileLanguages();
        }

        if (!s_ocr.engine) {
            return FALSE;
        }
    } catch (winrt::hresult_error const &) {
        return FALSE;
    }

    s_ocr.initialized = true;
    return TRUE;
}

void OcrEngine_Shutdown(void)
{
    if (!s_ocr.initialized) return;
    s_ocr.engine = nullptr;
    s_ocr.initialized = false;
}

BOOL OcrEngine_Recognize(const BYTE *pixels, int width, int height, int stride,
                         OcrResult *result)
{
    if (!s_ocr.initialized || !pixels || !result) return FALSE;
    if (width <= 0 || height <= 0) return FALSE;

    try {
        auto bitmap = CreateBitmapFromPixels(pixels, width, height, stride);
        auto ocr_result = s_ocr.engine.RecognizeAsync(bitmap).get();
        ConvertOcrResult(ocr_result, result);
        return TRUE;
    } catch (winrt::hresult_error const &) {
        return FALSE;
    }
}

BOOL OcrEngine_RecognizeRegion(const BYTE *pixels, int img_width, int img_height,
                               int stride, RECT roi, OcrResult *result)
{
    if (!s_ocr.initialized || !pixels || !result) return FALSE;

    int roi_w = 0, roi_h = 0;
    BYTE *roi_pixels = ExtractROI(pixels, img_width, img_height, stride,
                                  roi, &roi_w, &roi_h);
    if (!roi_pixels) return FALSE;

    BOOL ok = OcrEngine_Recognize(roi_pixels, roi_w, roi_h, roi_w * 4, result);

    /* Offset bounding boxes back to original image coordinates */
    if (ok) {
        for (int i = 0; i < result->line_count; i++) {
            for (int j = 0; j < result->lines[i].word_count; j++) {
                result->lines[i].words[j].bounds.left   += roi.left;
                result->lines[i].words[j].bounds.right  += roi.left;
                result->lines[i].words[j].bounds.top    += roi.top;
                result->lines[i].words[j].bounds.bottom += roi.top;
            }
        }
    }

    free(roi_pixels);
    return ok;
}

BOOL OcrEngine_IsReady(void)
{
    return s_ocr.initialized ? TRUE : FALSE;
}

} /* extern "C" */
