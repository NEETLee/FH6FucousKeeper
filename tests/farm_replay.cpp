/*
 * farm_replay.cpp - Offline vision replay tool (dev only)
 *
 * Runs the same template detectors + start-menu OCR that the farm pipeline
 * uses, but against a SAVED frame PNG instead of the live game. Pair it with
 * the FK_DEBUG snapshots dumped under debug/<session>/ to tune thresholds and
 * templates without needing the game running.
 *
 * Build: make farm-replay
 * Run:   build/farm_replay.exe <frame.png> [lang] [templates_dir]
 *          lang           default zh-Hans-CN
 *          templates_dir  default assets/templates
 */

#include <cstdio>
#include <cstring>
#include <string>
#include <algorithm>
#include <cwctype>
#include <windows.h>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

#include "template_match.h"
#include "ocr_engine.h"

static std::string g_tdir = "assets/templates";
static std::string tp(const char *name) { return g_tdir + "/" + name; }

static double best_score(const char *name, const char *region) {
    TMRegion rg = TM_NamedRegion(region);
    TMResult g = TM_FindGray(tp(name).c_str(), 0.0, FALSE, FALSE, rg);
    TMResult c = TM_FindColor(tp(name).c_str(), 0.0, FALSE, rg);
    TMResult t = TM_FindTransparent(tp(name).c_str(), 0.0, FALSE, rg);
    double b = g.score;
    if (c.score > b) b = c.score;
    if (t.score > b) b = t.score;
    return b;
}

static void report(const char *name, const char *region) {
    TMRegion rg = TM_NamedRegion(region);
    TMResult g = TM_FindGray(tp(name).c_str(), 0.0, FALSE, FALSE, rg);
    TMResult c = TM_FindColor(tp(name).c_str(), 0.0, FALSE, rg);
    TMResult t = TM_FindTransparent(tp(name).c_str(), 0.0, FALSE, rg);
    TMResult best = g;
    if (c.score > best.score) best = c;
    if (t.score > best.score) best = t;
    printf("  %-20s [%-9s] best=%.3f (gray=%.3f color=%.3f tr=%.3f) "
           "@ (%d,%d %dx%d) scale=%.3f\n",
           name, region, best.score, g.score, c.score, t.score,
           best.x, best.y, best.w, best.h, best.scale);
}

int wmain(int argc, wchar_t **argv) {
    SetConsoleOutputCP(CP_UTF8);
    if (argc < 2) {
        printf("usage: farm_replay <frame.png> [lang] [templates_dir]\n");
        return 1;
    }

    char frame[1024];
    WideCharToMultiByte(CP_UTF8, 0, argv[1], -1, frame, sizeof(frame), NULL, NULL);
    std::wstring lang = (argc >= 3) ? argv[2] : L"zh-Hans-CN";
    if (argc >= 4) {
        char d[1024];
        WideCharToMultiByte(CP_UTF8, 0, argv[3], -1, d, sizeof(d), NULL, NULL);
        g_tdir = d;
    }

    TM_Init();
    if (!TM_LoadFrameFromFile(frame)) {
        printf("ERROR: cannot load frame '%s'\n", frame);
        return 2;
    }
    int w = 0, h = 0;
    TM_GetFrameSize(&w, &h);
    printf("frame: %s (%dx%d)\n", frame, w, h);
    printf("templates: %s\n\n", g_tdir.c_str());

    printf("[template scores]\n");
    report("startw.png",       "left");
    report("start.png",        "left");
    report("eventlab.png",     "full");
    report("playenent.png",    "full");
    report("VEI.png",          "full");
    report("restart.png",      "full");
    report("cricon.png",       "topright");

    printf("\n[OCR start-menu region]\n");
    bool ocr_match = false;
    cv::Mat bgr = cv::imread(frame, cv::IMREAD_COLOR);
    if (!bgr.empty() && OcrEngine_Init(lang.c_str())) {
        cv::Mat bgra;
        cv::cvtColor(bgr, bgra, cv::COLOR_BGR2BGRA);
        RECT roi;
        roi.left   = 0;
        roi.top    = (h * 35) / 100;
        roi.right  = (w * 55) / 100;
        roi.bottom = h;
        static OcrResult r;  /* ~2 MB: keep off the stack */
        memset(&r, 0, sizeof(r));
        if (OcrEngine_RecognizeRegion(bgra.data, bgra.cols, bgra.rows,
                                      (int)bgra.step, roi, &r)) {
            char u8[4096];
            WideCharToMultiByte(CP_UTF8, 0, r.all_text, -1, u8, sizeof(u8), NULL, NULL);
            printf("  text: %s\n", u8);
            /* Mirror start_menu_ocr(): strip whitespace, then keyword match. */
            std::wstring packed;
            for (const wchar_t *p = r.all_text; *p; ++p)
                if (!iswspace((wint_t)*p)) packed.push_back(*p);
            if (packed.find(L"\u5f00\u59cb") != std::wstring::npos ||  /* 开始 */
                packed.find(L"\u7ade\u8d5b") != std::wstring::npos ||  /* 竞赛 */
                packed.find(L"\u8d5b\u4e8b") != std::wstring::npos)    /* 赛事 */
                ocr_match = true;
        } else {
            printf("  (recognize failed)\n");
        }
        OcrEngine_Shutdown();
    } else {
        printf("  (OCR unavailable for lang)\n");
    }

    /* Pipeline decision mirror: this is exactly what the farm start loop uses to
     * decide it is on the "start race" menu (ocr keyword OR template >= 0.80). */
    double tbest = std::max(best_score("startw.png", "left"),
                            best_score("start.png", "left"));
    bool detected = ocr_match || tbest >= 0.80;
    printf("\n[pipeline decision]\n");
    printf("  start menu: ocr=%-5s  template_best=%.3f  =>  %s  (press_th=0.80)\n",
           ocr_match ? "MATCH" : "no", tbest,
           detected ? "DETECTED" : "not detected");

    TM_Shutdown();
    return 0;
}
