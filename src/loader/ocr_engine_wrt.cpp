/*
 * ocr_engine_wrt.cpp - Windows OCR API (raw COM, no WinRT headers)
 *
 * Implements ocr_engine.h using direct COM vtable calls to avoid
 * C++/WinRT projection header dependencies. Compiles with w64devkit.
 *
 * Requires: Windows 10 1809+ with an OCR language pack installed
 * Links: -lruntimeobject -lole32
 */

#include "ocr_engine.h"

#include <initguid.h>
#include <roapi.h>
#include <winstring.h>
#include <inspectable.h>
#include <asyncinfo.h>

#include <stdlib.h>
#include <string.h>
#include <wchar.h>

/* ─── Vtable call helpers (same pattern as screen_capture_wgc.cpp) ─── */

struct VTable { void *methods[64]; };
struct ComObj { VTable *vtbl; };
#define SLOT(obj, n) (((ComObj*)(obj))->vtbl->methods[n])

template<typename Ret, typename... Args>
static inline Ret vcall(void *obj, int slot, Args... args) {
    auto fn = reinterpret_cast<Ret(STDMETHODCALLTYPE*)(void*, Args...)>(
        ((ComObj*)obj)->vtbl->methods[slot]);
    return fn(obj, args...);
}

static inline void SafeRelease(void **p) {
    if (*p) { vcall<ULONG>(*p, 2); *p = nullptr; }
}

/* ─── GUIDs ──────────────────────────────────────────────────────────── */

static const IID IID_IOcrEngineStatics =
    {0x5bffa85a, 0x3384, 0x3540, {0x99,0x40,0x69,0x91,0x20,0xd4,0x28,0xa8}};

static const IID IID_IOcrEngine =
    {0x5a14bc41, 0x5b76, 0x3140, {0xb6,0x80,0x88,0x25,0x56,0x26,0x83,0xac}};

static const IID IID_IOcrResult =
    {0x9bd235b2, 0x175b, 0x3d6a, {0x92,0xe2,0x38,0x8c,0x20,0x6e,0x2f,0x63}};

static const IID IID_IOcrLine =
    {0x0043a16f, 0xe31f, 0x3a24, {0x89,0x9c,0xd4,0x44,0xbd,0x08,0x81,0x24}};

static const IID IID_IOcrWord =
    {0x3c2a477a, 0x5cd9, 0x3525, {0xba,0x2a,0x23,0xd1,0xe0,0xa6,0x8a,0x1d}};

static const IID IID_IAsyncOcrResult =
    {0xc7d7118e, 0xae36, 0x59c0, {0xac,0x76,0x7b,0xad,0xee,0x71,0x1c,0x8b}};

static const IID IID_IVectorViewOcrLine =
    {0x60c76eac, 0x8875, 0x5ddb, {0xa1,0x9b,0x65,0xa3,0x93,0x62,0x79,0xea}};

static const IID IID_IVectorViewOcrWord =
    {0x805a60c7, 0xdf4f, 0x527c, {0x86,0xb2,0xe2,0x9e,0x43,0x9a,0x83,0xd2}};

static const IID IID_ILanguageFactory =
    {0x9b0252ac, 0x0c27, 0x44f8, {0xb7,0x92,0x97,0x93,0xfb,0x66,0xc6,0x3e}};

static const IID IID_ISoftwareBitmapFactory =
    {0xc99feb69, 0x2d62, 0x4d47, {0xa6,0xb3,0x4f,0xdb,0x6a,0x07,0xfd,0xf8}};

static const IID IID_ISoftwareBitmap =
    {0x689e0708, 0x7eef, 0x483f, {0x96,0x3f,0xda,0x93,0x88,0x18,0xe0,0x73}};

static const IID IID_IMemoryBuffer =
    {0xfbc4dd2a, 0x245b, 0x11e4, {0xaf,0x98,0x68,0x94,0x23,0x26,0x0c,0xf8}};

static const IID IID_IMemoryBufferByteAccess =
    {0x5b0d3235, 0x4dba, 0x4d44, {0x86,0x5e,0x8f,0x1d,0x0e,0x4f,0xd0,0x4d}};

/*
 * Vtable slot reference (all IInspectable-based unless noted):
 *
 * IOcrEngineStatics:
 *   [6] get_MaxImageDimension  [7] get_AvailableRecognizerLanguages
 *   [8] IsLanguageSupported  [9] TryCreateFromLanguage
 *   [10] TryCreateFromUserProfileLanguages
 *
 * IOcrEngine:
 *   [6] RecognizeAsync  [7] get_RecognizerLanguage
 *
 * IOcrResult:
 *   [6] get_Lines  [7] get_TextAngle  [8] get_Text
 *
 * IOcrLine:
 *   [6] get_Words  [7] get_Text
 *
 * IOcrWord:
 *   [6] get_BoundingRect  [7] get_Text
 *
 * IAsyncOperation<OcrResult>:
 *   [6] put_Completed  [7] get_Completed  [8] GetResults
 *
 * IVectorView<T>:
 *   [6] GetAt  [7] get_Size  [8] IndexOf  [9] GetMany
 *
 * ISoftwareBitmapFactory:
 *   [6] Create  [7] CreateWithAlpha
 *
 * ISoftwareBitmap:
 *   [15] LockBuffer
 *
 * IMemoryBuffer:
 *   [6] CreateReference
 *
 * IMemoryBufferByteAccess (IUnknown-based):
 *   [3] GetBuffer
 *
 * ILanguageFactory:
 *   [6] CreateLanguage
 *
 * IAsyncInfo (IInspectable-based):
 *   [6] get_Id  [7] get_Status  [8] get_ErrorCode  [9] Cancel  [10] Close
 */

/* WinRT Rect struct (matches ABI::Windows::Foundation::Rect) */
struct WinRTRect { float X, Y, Width, Height; };

/* BitmapPixelFormat / BitmapAlphaMode / BitmapBufferAccessMode enums */
enum { PixelFormat_Bgra8 = 87 };
enum { AlphaMode_Premultiplied = 0 };
enum { BufferAccess_ReadWrite = 1 };

/* ─── Internal State ─────────────────────────────────────────────────── */

static struct {
    void *ocr_engine;       /* IOcrEngine */
    void *bitmap_factory;   /* ISoftwareBitmapFactory */
    BOOL  initialized;
} s_ocr = {};

/* ─── Helpers ────────────────────────────────────────────────────────── */

static HSTRING MakeHString(const WCHAR *str) {
    HSTRING h = nullptr;
    WindowsCreateString(str, (UINT32)wcslen(str), &h);
    return h;
}

static BOOL WaitForAsync(void *async_op, void **result)
{
    IAsyncInfo *info = nullptr;
    HRESULT hr = ((IUnknown*)async_op)->QueryInterface(IID_IAsyncInfo, (void**)&info);
    if (FAILED(hr)) return FALSE;

    for (int i = 0; i < 5000; i++) {
        AsyncStatus status = (AsyncStatus)0;
        info->get_Status(&status);
        if (status == Completed) {
            info->Release();
            typedef HRESULT (STDMETHODCALLTYPE *FN_GetResults)(void*, void**);
            FN_GetResults fn = (FN_GetResults)(SLOT(async_op, 8));
            hr = fn(async_op, result);
            return SUCCEEDED(hr) && *result != nullptr;
        }
        if (status == Error || status == Canceled) {
            info->Release();
            return FALSE;
        }
        Sleep(1);
    }
    info->Release();
    return FALSE;
}

static void* CreateSoftwareBitmap(int width, int height)
{
    if (!s_ocr.bitmap_factory) return nullptr;

    typedef HRESULT (STDMETHODCALLTYPE *FN_CreateWithAlpha)(
        void*, INT32, INT32, INT32, INT32, void**);
    FN_CreateWithAlpha fn = (FN_CreateWithAlpha)(SLOT(s_ocr.bitmap_factory, 7));

    void *bitmap = nullptr;
    HRESULT hr = fn(s_ocr.bitmap_factory,
                    PixelFormat_Bgra8, (INT32)width, (INT32)height,
                    AlphaMode_Premultiplied, &bitmap);
    if (FAILED(hr)) return nullptr;
    return bitmap;
}

static BOOL CopyPixelsToBitmap(void *bitmap, const BYTE *pixels, int width, int height, int stride)
{
    /* LockBuffer(ReadWrite) -> IBitmapBuffer */
    typedef HRESULT (STDMETHODCALLTYPE *FN_LockBuffer)(void*, INT32, void**);
    FN_LockBuffer fnLock = (FN_LockBuffer)(SLOT(bitmap, 15));
    void *bmp_buffer = nullptr;
    HRESULT hr = fnLock(bitmap, BufferAccess_ReadWrite, &bmp_buffer);
    if (FAILED(hr) || !bmp_buffer) return FALSE;

    /* QI IBitmapBuffer -> IMemoryBuffer */
    void *mem_buf = nullptr;
    hr = ((IUnknown*)bmp_buffer)->QueryInterface(IID_IMemoryBuffer, &mem_buf);
    if (FAILED(hr) || !mem_buf) { SafeRelease(&bmp_buffer); return FALSE; }

    /* IMemoryBuffer::CreateReference -> IMemoryBufferReference */
    typedef HRESULT (STDMETHODCALLTYPE *FN_CreateRef)(void*, void**);
    FN_CreateRef fnRef = (FN_CreateRef)(SLOT(mem_buf, 6));
    void *buf_ref = nullptr;
    hr = fnRef(mem_buf, &buf_ref);
    SafeRelease(&mem_buf);
    if (FAILED(hr) || !buf_ref) { SafeRelease(&bmp_buffer); return FALSE; }

    /* QI IMemoryBufferReference -> IMemoryBufferByteAccess */
    void *byte_access = nullptr;
    hr = ((IUnknown*)buf_ref)->QueryInterface(IID_IMemoryBufferByteAccess, &byte_access);
    if (FAILED(hr) || !byte_access) {
        SafeRelease(&buf_ref);
        SafeRelease(&bmp_buffer);
        return FALSE;
    }

    /* IMemoryBufferByteAccess::GetBuffer (slot 3, IUnknown-based) */
    typedef HRESULT (STDMETHODCALLTYPE *FN_GetBuffer)(void*, BYTE**, UINT32*);
    FN_GetBuffer fnGet = (FN_GetBuffer)(SLOT(byte_access, 3));
    BYTE *dst = nullptr;
    UINT32 capacity = 0;
    hr = fnGet(byte_access, &dst, &capacity);

    BOOL ok = FALSE;
    if (SUCCEEDED(hr) && dst) {
        int row_bytes = width * 4;
        int copy_rows = height;
        if ((UINT32)(copy_rows * row_bytes) > capacity)
            copy_rows = (int)(capacity / (UINT32)row_bytes);

        for (int y = 0; y < copy_rows; y++) {
            memcpy(dst + y * row_bytes, pixels + y * stride, row_bytes);
        }
        ok = TRUE;
    }

    SafeRelease(&byte_access);
    SafeRelease(&buf_ref);
    SafeRelease(&bmp_buffer);
    return ok;
}

static void ExtractOcrResults(void *ocr_result, OcrResult *out)
{
    memset(out, 0, sizeof(*out));

    /* IOcrResult::get_Lines (slot 6) */
    typedef HRESULT (STDMETHODCALLTYPE *FN_GetLines)(void*, void**);
    FN_GetLines fnLines = (FN_GetLines)(SLOT(ocr_result, 6));
    void *lines_view = nullptr;
    HRESULT hr = fnLines(ocr_result, &lines_view);
    if (FAILED(hr) || !lines_view) return;

    /* IVectorView::get_Size (slot 7) */
    typedef HRESULT (STDMETHODCALLTYPE *FN_GetSize)(void*, UINT32*);
    FN_GetSize fnSize = (FN_GetSize)(SLOT(lines_view, 7));
    UINT32 line_count = 0;
    fnSize(lines_view, &line_count);

    WCHAR *all_text_ptr = out->all_text;
    int all_text_remaining = (int)(sizeof(out->all_text)/sizeof(WCHAR)) - 1;

    for (UINT32 i = 0; i < line_count && (int)i < OCR_MAX_LINES; i++) {
        /* IVectorView::GetAt (slot 6) */
        typedef HRESULT (STDMETHODCALLTYPE *FN_GetAt)(void*, UINT32, void**);
        FN_GetAt fnGetAt = (FN_GetAt)(SLOT(lines_view, 6));
        void *line_obj = nullptr;
        hr = fnGetAt(lines_view, i, &line_obj);
        if (FAILED(hr) || !line_obj) continue;

        OcrLine *out_line = &out->lines[out->line_count];

        /* IOcrLine::get_Text (slot 7) */
        HSTRING line_text = nullptr;
        vcall<HRESULT>(line_obj, 7, &line_text);
        if (line_text) {
            UINT32 len = 0;
            const WCHAR *raw = WindowsGetStringRawBuffer(line_text, &len);
            if (raw && len > 0) {
                int copy_len = (len < (UINT32)(OCR_MAX_TEXT_LEN * 4 - 1)) ? (int)len : (OCR_MAX_TEXT_LEN * 4 - 1);
                wcsncpy(out_line->full_text, raw, copy_len);
                out_line->full_text[copy_len] = L'\0';

                if (all_text_remaining > 0) {
                    int cat_len = (copy_len < all_text_remaining) ? copy_len : all_text_remaining;
                    wcsncpy(all_text_ptr, raw, cat_len);
                    all_text_ptr += cat_len;
                    all_text_remaining -= cat_len;
                    if (all_text_remaining > 0) {
                        *all_text_ptr++ = L'\n';
                        all_text_remaining--;
                    }
                }
            }
            WindowsDeleteString(line_text);
        }

        /* IOcrLine::get_Words (slot 6) */
        typedef HRESULT (STDMETHODCALLTYPE *FN_GetWords)(void*, void**);
        FN_GetWords fnWords = (FN_GetWords)(SLOT(line_obj, 6));
        void *words_view = nullptr;
        hr = fnWords(line_obj, &words_view);
        if (SUCCEEDED(hr) && words_view) {
            UINT32 word_count = 0;
            fnSize = (FN_GetSize)(SLOT(words_view, 7));
            fnSize(words_view, &word_count);

            for (UINT32 j = 0; j < word_count && (int)j < OCR_MAX_WORDS; j++) {
                FN_GetAt fnWordAt = (FN_GetAt)(SLOT(words_view, 6));
                void *word_obj = nullptr;
                hr = fnWordAt(words_view, j, &word_obj);
                if (FAILED(hr) || !word_obj) continue;

                OcrWord *out_word = &out_line->words[out_line->word_count];

                /* IOcrWord::get_BoundingRect (slot 6) */
                WinRTRect rect = {};
                vcall<HRESULT>(word_obj, 6, &rect);
                out_word->bounds.left   = (LONG)rect.X;
                out_word->bounds.top    = (LONG)rect.Y;
                out_word->bounds.right  = (LONG)(rect.X + rect.Width);
                out_word->bounds.bottom = (LONG)(rect.Y + rect.Height);

                /* IOcrWord::get_Text (slot 7) */
                HSTRING word_text = nullptr;
                vcall<HRESULT>(word_obj, 7, &word_text);
                if (word_text) {
                    UINT32 wlen = 0;
                    const WCHAR *wraw = WindowsGetStringRawBuffer(word_text, &wlen);
                    if (wraw && wlen > 0) {
                        int wl = (wlen < (UINT32)(OCR_MAX_TEXT_LEN - 1)) ? (int)wlen : (OCR_MAX_TEXT_LEN - 1);
                        wcsncpy(out_word->text, wraw, wl);
                        out_word->text[wl] = L'\0';
                    }
                    WindowsDeleteString(word_text);
                }

                out_word->confidence = 1.0f;
                out_line->word_count++;
                SafeRelease(&word_obj);
            }
            SafeRelease(&words_view);
        }

        out->line_count++;
        SafeRelease(&line_obj);
    }

    *all_text_ptr = L'\0';
    SafeRelease(&lines_view);
}

/* ─── Public API ─────────────────────────────────────────────────────── */

extern "C" {

BOOL OcrEngine_Init(const WCHAR *language_tag)
{
    if (s_ocr.initialized) return TRUE;

    HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(hr) && hr != (HRESULT)RPC_E_CHANGED_MODE && hr != S_FALSE)
        return FALSE;

    /* Get IOcrEngineStatics from "Windows.Media.Ocr.OcrEngine" */
    HSTRING hcls = MakeHString(L"Windows.Media.Ocr.OcrEngine");
    if (!hcls) return FALSE;

    void *factory = nullptr;
    hr = RoGetActivationFactory(hcls, IID_IInspectable, &factory);
    WindowsDeleteString(hcls);
    if (FAILED(hr) || !factory) return FALSE;

    void *statics = nullptr;
    hr = ((IUnknown*)factory)->QueryInterface(IID_IOcrEngineStatics, &statics);
    ((IUnknown*)factory)->Release();
    if (FAILED(hr) || !statics) return FALSE;

    /* Create OCR engine */
    void *engine = nullptr;
    if (language_tag && language_tag[0]) {
        /* Create Language object, then TryCreateFromLanguage */
        HSTRING hlang_cls = MakeHString(L"Windows.Globalization.Language");
        if (hlang_cls) {
            void *lang_factory_raw = nullptr;
            hr = RoGetActivationFactory(hlang_cls, IID_IInspectable, &lang_factory_raw);
            WindowsDeleteString(hlang_cls);
            if (SUCCEEDED(hr) && lang_factory_raw) {
                void *lang_factory = nullptr;
                hr = ((IUnknown*)lang_factory_raw)->QueryInterface(IID_ILanguageFactory, &lang_factory);
                ((IUnknown*)lang_factory_raw)->Release();
                if (SUCCEEDED(hr) && lang_factory) {
                    /* ILanguageFactory::CreateLanguage (slot 6) */
                    HSTRING htag = MakeHString(language_tag);
                    if (htag) {
                        typedef HRESULT (STDMETHODCALLTYPE *FN_CreateLang)(void*, HSTRING, void**);
                        FN_CreateLang fnLang = (FN_CreateLang)(SLOT(lang_factory, 6));
                        void *language = nullptr;
                        hr = fnLang(lang_factory, htag, &language);
                        WindowsDeleteString(htag);
                        if (SUCCEEDED(hr) && language) {
                            /* IOcrEngineStatics::TryCreateFromLanguage (slot 9) */
                            typedef HRESULT (STDMETHODCALLTYPE *FN_TryLang)(void*, void*, void**);
                            FN_TryLang fnTry = (FN_TryLang)(SLOT(statics, 9));
                            fnTry(statics, language, &engine);
                            SafeRelease(&language);
                        }
                    }
                    SafeRelease(&lang_factory);
                }
            }
        }
    }

    if (!engine) {
        /* Fallback: TryCreateFromUserProfileLanguages (slot 10) */
        typedef HRESULT (STDMETHODCALLTYPE *FN_TryProfile)(void*, void**);
        FN_TryProfile fnProfile = (FN_TryProfile)(SLOT(statics, 10));
        hr = fnProfile(statics, &engine);
    }

    SafeRelease(&statics);
    if (!engine) return FALSE;
    s_ocr.ocr_engine = engine;

    /* Get ISoftwareBitmapFactory */
    HSTRING hbmp_cls = MakeHString(L"Windows.Graphics.Imaging.SoftwareBitmap");
    if (hbmp_cls) {
        void *bmp_factory_raw = nullptr;
        hr = RoGetActivationFactory(hbmp_cls, IID_IInspectable, &bmp_factory_raw);
        WindowsDeleteString(hbmp_cls);
        if (SUCCEEDED(hr) && bmp_factory_raw) {
            hr = ((IUnknown*)bmp_factory_raw)->QueryInterface(IID_ISoftwareBitmapFactory, &s_ocr.bitmap_factory);
            ((IUnknown*)bmp_factory_raw)->Release();
        }
    }

    if (!s_ocr.bitmap_factory) {
        SafeRelease(&s_ocr.ocr_engine);
        return FALSE;
    }

    s_ocr.initialized = TRUE;
    return TRUE;
}

void OcrEngine_Shutdown(void)
{
    if (!s_ocr.initialized) return;
    SafeRelease(&s_ocr.ocr_engine);
    SafeRelease(&s_ocr.bitmap_factory);
    s_ocr.initialized = FALSE;
}

BOOL OcrEngine_Recognize(const BYTE *pixels, int width, int height, int stride,
                         OcrResult *result)
{
    if (!result) return FALSE;
    memset(result, 0, sizeof(*result));
    if (!s_ocr.initialized || !pixels || width <= 0 || height <= 0) return FALSE;

    void *bitmap = CreateSoftwareBitmap(width, height);
    if (!bitmap) return FALSE;

    if (!CopyPixelsToBitmap(bitmap, pixels, width, height, stride)) {
        SafeRelease(&bitmap);
        return FALSE;
    }

    /* IOcrEngine::RecognizeAsync (slot 6) */
    typedef HRESULT (STDMETHODCALLTYPE *FN_RecognizeAsync)(void*, void*, void**);
    FN_RecognizeAsync fnRec = (FN_RecognizeAsync)(SLOT(s_ocr.ocr_engine, 6));
    void *async_op = nullptr;
    HRESULT hr = fnRec(s_ocr.ocr_engine, bitmap, &async_op);
    SafeRelease(&bitmap);
    if (FAILED(hr) || !async_op) return FALSE;

    void *ocr_result = nullptr;
    BOOL ok = WaitForAsync(async_op, &ocr_result);
    SafeRelease(&async_op);
    if (!ok || !ocr_result) return FALSE;

    ExtractOcrResults(ocr_result, result);
    SafeRelease(&ocr_result);
    return result->line_count > 0 || result->all_text[0] != L'\0';
}

BOOL OcrEngine_RecognizeRegion(const BYTE *pixels, int img_width, int img_height,
                               int stride, RECT roi, OcrResult *result)
{
    if (!result) return FALSE;
    memset(result, 0, sizeof(*result));
    if (!s_ocr.initialized || !pixels) return FALSE;

    /* Clamp ROI */
    if (roi.left < 0) roi.left = 0;
    if (roi.top < 0) roi.top = 0;
    if (roi.right > img_width) roi.right = img_width;
    if (roi.bottom > img_height) roi.bottom = img_height;

    int rw = roi.right - roi.left;
    int rh = roi.bottom - roi.top;
    if (rw <= 0 || rh <= 0) return FALSE;

    /* Extract ROI pixels */
    int roi_stride = rw * 4;
    BYTE *roi_pixels = (BYTE*)malloc(roi_stride * rh);
    if (!roi_pixels) return FALSE;

    for (int y = 0; y < rh; y++) {
        const BYTE *src = pixels + (roi.top + y) * stride + roi.left * 4;
        memcpy(roi_pixels + y * roi_stride, src, roi_stride);
    }

    BOOL ok = OcrEngine_Recognize(roi_pixels, rw, rh, roi_stride, result);
    free(roi_pixels);
    return ok;
}

BOOL OcrEngine_IsReady(void)
{
    return s_ocr.initialized;
}

} /* extern "C" */
