/*
 * test_resolution.cpp - Offline multi-resolution validation harness.
 *
 * Feeds curated 2K reference frames through the REAL template_match.cpp code at
 * a sweep of resolutions (down to 1080p / small windows, up to 4K) and asserts
 * that every annotated anchor still locks above its runtime threshold, in the
 * expected screen region. This is the pre-release gate against resolution
 * regressions; a real 1080p/4K machine spot-check remains the final word.
 *
 * Fixtures live in tests/fixtures/<name>.png (clean, un-annotated 2K captures
 * curated via `make capture-frame`). Ground-truth anchors are declared inline
 * below and mirror the exact matcher / region / threshold the farm flow uses.
 *
 * Build/run: make test-resolution   (run from the repo root)
 */

#include <windows.h>
#include <stdio.h>
#include <vector>
#include <string>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

extern "C" {
#include "template_match.h"
}

#define ASSETS "assets/templates"
#define FIXTURES "tests/fixtures"

enum Matcher { M_COLOR, M_GRAY, M_GRAY_INV, M_TRANSPARENT };

struct Anchor {
    const char *tmpl;        /* template filename under ASSETS */
    Matcher     matcher;
    const char *region;      /* TM_NamedRegion name */
    double      threshold;   /* runtime threshold the flow uses */
    bool        fast;        /* fast scale mode (as the flow calls it) */
    bool        required;    /* a required violation fails the harness */
    bool        expect_found;/* true: must lock; false: must NOT lock (no false positive) */
    /* expected normalized center box; use full 0..1 to skip the location check */
    double ex0, ey0, ex1, ey1;
};

struct Fixture {
    const char *name;
    const char *file;      /* under FIXTURES */
    std::vector<Anchor> anchors;
};

static const char *MatcherName(Matcher m) {
    switch (m) {
    case M_COLOR:       return "color";
    case M_GRAY:        return "gray";
    case M_GRAY_INV:    return "gray-inv";
    case M_TRANSPARENT: return "alpha";
    }
    return "?";
}

static TMResult RunAnchor(const Anchor &a, TMRegion rg) {
    std::string tp = std::string(ASSETS) + "/" + a.tmpl;
    switch (a.matcher) {
    case M_COLOR:       return TM_FindColor(tp.c_str(), a.threshold, a.fast, rg);
    case M_GRAY:        return TM_FindGray(tp.c_str(), a.threshold, a.fast, FALSE, rg);
    case M_GRAY_INV:    return TM_FindGray(tp.c_str(), a.threshold, a.fast, TRUE, rg);
    case M_TRANSPARENT: return TM_FindTransparent(tp.c_str(), a.threshold, a.fast, rg);
    }
    TMResult z = {0}; return z;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);

    /* Resolution sweep: common real screen sizes plus small/large windowed. */
    struct { int w, h; const char *label; } targets[] = {
        { 1366,  768, "1366x768 (small win)" },
        { 1600,  900, "1600x900 (win)" },
        { 1920, 1080, "1920x1080 (1080p)" },
        { 2560, 1440, "2560x1440 (2K)" },
        { 3200, 1800, "3200x1800 (QHD+)" },
        { 3840, 2160, "3840x2160 (4K)" },
    };
    const int NTARGET = (int)(sizeof(targets) / sizeof(targets[0]));

    /* Curated fixtures + ground-truth anchors (mirror farm_flow / farm_pipeline). */
    std::vector<Fixture> fixtures = {
        {
            "vehicles_menu", "vehicles_menu.png",
            {
                /* CR icon anchor: farm_pipeline read_cr_anchored, topright, color 0.70 */
                { "cricon.png",  M_COLOR, "topright", 0.70, false, true, true,
                  0.60, 0.00, 1.00, 0.35 },
                /* "Buy New & Used Cars" tile: goto_vehicles_tab marker, left, gray 0.65 */
                { "BNandUC.png", M_GRAY,  "left",     0.65, true,  true, true,
                  0.00, 0.45, 0.55, 1.00 },
            }
        },
        {
            /* Free-roam negative fixture: no menu chrome must ever false-positive,
             * at any resolution (a stray menu lock is what caused ESC oscillation
             * and wrong-car deletions). All anchors must stay BELOW threshold. */
            "free_roam", "free_roam.png",
            {
                { "cricon.png",           M_COLOR, "topright", 0.70, false, true, false, 0,0,1,1 },
                { "BNandUC.png",          M_GRAY,  "left",     0.65, true,  true, false, 0,0,1,1 },
                { "collectionjournal.png",M_GRAY,  "left",     0.70, true,  true, false, 0,0,1,1 },
            }
        },
    };

    if (!TM_Init()) { printf("TM_Init failed\n"); return 2; }

    int total = 0, passed = 0, failed_required = 0;

    for (const Fixture &fx : fixtures) {
        std::string path = std::string(FIXTURES) + "/" + fx.file;
        cv::Mat bgr = cv::imread(path, cv::IMREAD_COLOR);
        if (bgr.empty()) {
            printf("[SKIP] fixture missing: %s\n", path.c_str());
            continue;
        }
        printf("\n=== fixture %s (native %dx%d) ===\n",
                fx.name, bgr.cols, bgr.rows);

        for (int t = 0; t < NTARGET; t++) {
            int tw = targets[t].w, th = targets[t].h;
            cv::Mat resized;
            int interp = (tw < bgr.cols) ? cv::INTER_AREA : cv::INTER_CUBIC;
            cv::resize(bgr, resized, cv::Size(tw, th), 0, 0, interp);
            cv::Mat bgra;
            cv::cvtColor(resized, bgra, cv::COLOR_BGR2BGRA);
            if (!bgra.isContinuous()) bgra = bgra.clone();
            TM_SetFrame(bgra.data, tw, th, (int)bgra.step);

            printf("  [%-22s]\n", targets[t].label);
            for (const Anchor &a : fx.anchors) {
                TMRegion rg = TM_NamedRegion(a.region);
                TMResult r = RunAnchor(a, rg);
                double nx = tw > 0 ? (double)r.cx / tw : 0;
                double ny = th > 0 ? (double)r.cy / th : 0;
                bool loc_box = !(a.ex0 <= 0 && a.ex1 >= 1 && a.ey0 <= 0 && a.ey1 >= 1);
                bool loc_ok = (nx >= a.ex0 && nx <= a.ex1 &&
                               ny >= a.ey0 && ny <= a.ey1);
                bool ok;
                if (a.expect_found)
                    ok = r.found && (!loc_box || loc_ok);
                else
                    ok = !r.found;   /* must stay below threshold: no false positive */
                total++;
                if (ok) passed++;
                else if (a.required) failed_required++;

                const char *verdict = ok ? "PASS" : (a.required ? "FAIL" : "warn");
                const char *note = "";
                if (!ok && !a.expect_found) note = " [false-positive]";
                else if (a.expect_found && r.found && loc_box && !loc_ok) note = " [loc-off]";
                printf("    %-4s %-16s %-6s %-9s s=%.3f scale=%.3f @(%.3f,%.3f)%s\n",
                        verdict, a.tmpl, MatcherName(a.matcher), a.region,
                        r.score, r.scale, nx, ny, note);
            }
        }
    }

    printf("\n=== summary: %d checks, %d pass, %d required-fail ===\n",
            total, passed, failed_required);
    TM_Shutdown();
    return failed_required == 0 ? 0 : 1;
}
