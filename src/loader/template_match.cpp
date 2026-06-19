/*
 * template_match.cpp - OpenCV multi-scale template matching
 *
 * Faithful port of FH6Auto's matching pipeline:
 *   - region cropping (search only within a sub-rect, offset result back)
 *   - scale system based on full frame width / 2560
 *   - color / grayscale / transparent(alpha-mask) variants
 *   - INTER_AREA downscaling, TM_CCOEFF_NORMED scoring
 */

#include "template_match.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <cmath>
#include <cstdio>
#include <set>
#include <algorithm>
#include <utility>

/* ─── Internal state ─────────────────────────────────────────────────── */

static cv::Mat g_screen_bgr;   /* full frame, 3-channel BGR */
static cv::Mat g_screen_gray;  /* full frame, grayscale */
static int g_screen_w = 0, g_screen_h = 0;
static std::mutex g_mtx;

struct CachedTemplate {
    cv::Mat bgr;    /* 3-channel */
    cv::Mat gray;   /* 1-channel */
    cv::Mat alpha;  /* 1-channel mask (empty if no alpha) */
    bool has_alpha;
};
static std::unordered_map<std::string, CachedTemplate> g_cache;

/* ─── Scale generation (mirrors FH6Auto get_scales_to_try) ───────────── */

static std::vector<double> GetScales(bool fast) {
    std::vector<double> scales;
    auto add = [&](double s) {
        s = std::round(s * 1000.0) / 1000.0;
        /* Lower floor covers small windows (0.30*2560 ~= 768px wide); higher
         * ceiling (2.6) covers up to ~6K for 2560-based templates AND lets the
         * 1024-based start-button template upscale enough at 2K windows.
         * Without the floor, shrinking below ~1150px dropped the only valid
         * scale and nothing matched. */
        if (s < 0.30 || s > 2.6) return;
        for (double e : scales) if (std::fabs(e - s) < 1e-6) return;
        scales.push_back(s);
    };
    double curr_w = (g_screen_w > 0) ? (double)g_screen_w : 2560.0;
    double primary = curr_w / 2560.0;
    add(primary);
    add(primary * 0.98); add(primary * 1.02);
    add(primary * 0.95); add(primary * 1.05);
    add(primary * 0.92); add(primary * 1.08);
    for (double bw : {1920.0, 1600.0}) {
        double s = curr_w / bw;
        add(s); add(s * 0.98); add(s * 1.02);
    }
    for (double s : {1.0, 0.95, 1.05, 0.9, 1.1, 0.85, 1.15, 0.8, 0.75, 0.7})
        add(s);
    if (fast && scales.size() > 8) scales.resize(8);
    return scales;
}

/* ─── Template cache ─────────────────────────────────────────────────── */

static CachedTemplate* LoadTemplate(const char *path) {
    auto it = g_cache.find(path);
    if (it != g_cache.end()) return &it->second;

    cv::Mat raw = cv::imread(path, cv::IMREAD_UNCHANGED);
    if (raw.empty()) return nullptr;

    CachedTemplate ct;
    ct.has_alpha = (raw.channels() == 4);
    if (ct.has_alpha) {
        cv::Mat ch[4];
        cv::split(raw, ch);
        ct.alpha = ch[3];
        cv::cvtColor(raw, ct.bgr, cv::COLOR_BGRA2BGR);
    } else if (raw.channels() == 3) {
        ct.bgr = raw;
    } else {
        cv::cvtColor(raw, ct.bgr, cv::COLOR_GRAY2BGR);
    }
    cv::cvtColor(ct.bgr, ct.gray, cv::COLOR_BGR2GRAY);

    g_cache[path] = ct;
    return &g_cache[path];
}

/* ─── Region helper ──────────────────────────────────────────────────── */

static cv::Rect ClampRegion(TMRegion r) {
    if (r.w <= 0 || r.h <= 0) return cv::Rect(0, 0, g_screen_w, g_screen_h);
    int x = std::max(0, r.x);
    int y = std::max(0, r.y);
    int w = std::min(r.w, g_screen_w - x);
    int h = std::min(r.h, g_screen_h - y);
    if (w <= 0 || h <= 0) return cv::Rect(0, 0, g_screen_w, g_screen_h);
    return cv::Rect(x, y, w, h);
}

/* ─── Core matching ──────────────────────────────────────────────────── */

enum MatchMode { MODE_COLOR, MODE_GRAY, MODE_TRANSPARENT };

static TMResult DoMatch(const char *path, double threshold, bool fast,
                        bool invert, MatchMode mode, TMRegion region) {
    TMResult r = {};
    CachedTemplate *ct = LoadTemplate(path);
    if (!ct) return r;

    cv::Rect roi_rect = ClampRegion(region);
    int ox = roi_rect.x, oy = roi_rect.y;

    cv::Mat src;
    cv::Mat tmpl_base;   /* the template at scale 1.0 */
    if (mode == MODE_COLOR || mode == MODE_TRANSPARENT) {
        src = g_screen_bgr(roi_rect);
        tmpl_base = ct->bgr;
    } else { /* GRAY matches on grayscale */
        src = g_screen_gray(roi_rect);
        tmpl_base = ct->gray;
    }
    if (src.empty() || tmpl_base.empty()) return r;

    std::vector<double> scales = GetScales(fast);

    for (double s : scales) {
        cv::Mat tmpl, mask;
        if (s == 1.0) {
            tmpl = tmpl_base;
            if (mode == MODE_TRANSPARENT && ct->has_alpha) mask = ct->alpha;
        } else {
            cv::resize(tmpl_base, tmpl, cv::Size(), s, s, cv::INTER_AREA);
            if (mode == MODE_TRANSPARENT && ct->has_alpha)
                cv::resize(ct->alpha, mask, cv::Size(), s, s, cv::INTER_AREA);
        }
        int h = tmpl.rows, w = tmpl.cols;
        if (h < 5 || w < 5 || h > src.rows || w > src.cols) continue;

        cv::Mat work_tmpl = tmpl;
        if (invert) work_tmpl = 255 - tmpl;

        cv::Mat res;
        if (mode == MODE_TRANSPARENT && !mask.empty())
            cv::matchTemplate(src, work_tmpl, res, cv::TM_CCOEFF_NORMED, mask);
        else
            cv::matchTemplate(src, work_tmpl, res, cv::TM_CCOEFF_NORMED);

        double maxVal; cv::Point maxLoc;
        cv::minMaxLoc(res, nullptr, &maxVal, nullptr, &maxLoc);

        /* TM_CCOEFF_NORMED with mask can produce inf/nan; guard it. */
        if (!(maxVal == maxVal) || maxVal > 1.0e6) continue;

        if (maxVal > r.score) {
            r.score = maxVal;
            r.scale = s;
            r.x = maxLoc.x + ox;
            r.y = maxLoc.y + oy;
            r.w = w; r.h = h;
            r.cx = r.x + w / 2;
            r.cy = r.y + h / 2;
        }
        if (maxVal >= threshold) { r.found = TRUE; return r; }
    }
    r.found = (r.score >= threshold);
    return r;
}

/* ─── Public API ─────────────────────────────────────────────────────── */

extern "C" {

BOOL TM_Init(void) { return TRUE; }

void TM_Shutdown(void) {
    std::lock_guard<std::mutex> lk(g_mtx);
    g_cache.clear();
    g_screen_bgr.release();
    g_screen_gray.release();
}

void TM_SetFrame(const BYTE *pixels, int width, int height, int stride) {
    std::lock_guard<std::mutex> lk(g_mtx);
    g_screen_w = width;
    g_screen_h = height;
    cv::Mat bgra(height, width, CV_8UC4, (void*)pixels, stride);
    cv::cvtColor(bgra, g_screen_bgr, cv::COLOR_BGRA2BGR);
    cv::cvtColor(g_screen_bgr, g_screen_gray, cv::COLOR_BGR2GRAY);
}

void TM_GetFrameSize(int *width, int *height) {
    if (width) *width = g_screen_w;
    if (height) *height = g_screen_h;
}

/* ─── Debug snapshots ────────────────────────────────────────────────── */

static bool        g_dbg_enabled = false;
static std::string g_dbg_base    = "debug";
static std::string g_dbg_session;   /* resolved lazily on first snap */
static int         g_dbg_seq      = 0;

void TM_DebugSetEnabled(int enabled) { g_dbg_enabled = (enabled != 0); }
int  TM_DebugIsEnabled(void)         { return g_dbg_enabled ? 1 : 0; }
void TM_DebugSetDir(const char *dir) { if (dir && *dir) g_dbg_base = dir; }

static void DbgEnsureSession() {
    if (!g_dbg_session.empty()) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    char sub[64];
    snprintf(sub, sizeof(sub), "run_%04d%02d%02d_%02d%02d%02d",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    CreateDirectoryA(g_dbg_base.c_str(), NULL);
    g_dbg_session = g_dbg_base + "/" + sub;
    CreateDirectoryA(g_dbg_session.c_str(), NULL);
}

void TM_DebugSnap(const char *tag, double score, int x, int y, int w, int h) {
    if (!g_dbg_enabled) return;
    std::lock_guard<std::mutex> lk(g_mtx);
    if (g_screen_bgr.empty()) return;
    DbgEnsureSession();

    cv::Mat img = g_screen_bgr.clone();
    if (w > 0 && h > 0) {
        cv::rectangle(img, cv::Rect(x, y, w, h), cv::Scalar(0, 255, 0), 2);
    }
    char label[160];
    snprintf(label, sizeof(label), "%s  %.3f", tag ? tag : "", score);
    cv::putText(img, label, cv::Point(12, 34), cv::FONT_HERSHEY_SIMPLEX,
                0.8, cv::Scalar(0, 255, 255), 2);

    char fname[600];
    snprintf(fname, sizeof(fname), "%s/%04d_%s_%.2f.png",
             g_dbg_session.c_str(), g_dbg_seq++, tag ? tag : "snap", score);
    cv::imwrite(fname, img);
}

BOOL TM_LoadFrameFromFile(const char *path) {
    std::lock_guard<std::mutex> lk(g_mtx);
    cv::Mat raw = cv::imread(path, cv::IMREAD_COLOR);
    if (raw.empty()) return FALSE;
    g_screen_bgr = raw;
    g_screen_w = raw.cols;
    g_screen_h = raw.rows;
    cv::cvtColor(g_screen_bgr, g_screen_gray, cv::COLOR_BGR2GRAY);
    return TRUE;
}

TMRegion TM_NamedRegion(const char *name) {
    int w = g_screen_w, h = g_screen_h;
    TMRegion r = { 0, 0, w, h };
    if (!name) return r;
    std::string n = name;
    if (n == "full")            r = { 0, 0, w, h };
    else if (n == "left")       r = { 0, 0, w/2, h };
    else if (n == "right")      r = { w/2, 0, w/2, h };
    else if (n == "top")        r = { 0, 0, w, h/2 };
    else if (n == "bottom")     r = { 0, h/2, w, h/2 };
    else if (n == "topleft")    r = { 0, 0, w/2, h/2 };
    else if (n == "topright")   r = { w/2, 0, w/2, h/2 };
    else if (n == "bottomleft") r = { 0, h/2, w/2, h/2 };
    else if (n == "bottomright")r = { w/2, h/2, w/2, h/2 };
    else if (n == "center")     r = { w/4, h/4, w/2, h/2 };
    return r;
}

TMResult TM_FindColor(const char *path, double threshold, BOOL fast, TMRegion region) {
    std::lock_guard<std::mutex> lk(g_mtx);
    return DoMatch(path, threshold, fast, false, MODE_COLOR, region);
}

TMResult TM_FindGray(const char *path, double threshold, BOOL fast, BOOL invert, TMRegion region) {
    std::lock_guard<std::mutex> lk(g_mtx);
    return DoMatch(path, threshold, fast, invert != FALSE, MODE_GRAY, region);
}

TMResult TM_FindTransparent(const char *path, double threshold, BOOL fast, TMRegion region) {
    std::lock_guard<std::mutex> lk(g_mtx);
    return DoMatch(path, threshold, fast, false, MODE_TRANSPARENT, region);
}

TMResult TM_FindWithElement(const char *main_path, const char *sub_path,
                            double main_threshold, double sub_threshold,
                            BOOL fast, TMRegion region) {
    std::lock_guard<std::mutex> lk(g_mtx);
    TMResult r = {};
    CachedTemplate *mt = LoadTemplate(main_path);
    CachedTemplate *st = LoadTemplate(sub_path);
    if (!mt || !st || g_screen_bgr.empty()) return r;

    cv::Rect roi = ClampRegion(region);
    int ox = roi.x, oy = roi.y;
    cv::Mat src = g_screen_bgr(roi);
    if (src.empty()) return r;

    std::vector<double> scales = GetScales(fast != FALSE);
    for (double s : scales) {
        cv::Mat mtpl, stpl;
        if (s == 1.0) { mtpl = mt->bgr; stpl = st->bgr; }
        else {
            cv::resize(mt->bgr, mtpl, cv::Size(), s, s, cv::INTER_AREA);
            cv::resize(st->bgr, stpl, cv::Size(), s, s, cv::INTER_AREA);
        }
        int h = mtpl.rows, w = mtpl.cols;
        if (h < 5 || w < 5 || h > src.rows || w > src.cols) continue;

        cv::Mat res;
        cv::matchTemplate(src, mtpl, res, cv::TM_CCOEFF_NORMED);
        double maxVal; cv::Point maxLoc;
        cv::minMaxLoc(res, nullptr, &maxVal, nullptr, &maxLoc);
        if (!(maxVal == maxVal)) continue;
        if (maxVal < main_threshold) continue;

        /* Verify the sub template within a padded ROI around the candidate. */
        int pad = 6;
        int x0 = std::max(0, maxLoc.x - pad);
        int y0 = std::max(0, maxLoc.y - pad);
        int x1 = std::min(src.cols, maxLoc.x + w + pad);
        int y1 = std::min(src.rows, maxLoc.y + h + pad);
        cv::Mat sub_roi = src(cv::Rect(x0, y0, x1 - x0, y1 - y0));
        if (sub_roi.rows < stpl.rows || sub_roi.cols < stpl.cols) continue;

        cv::Mat sres;
        cv::matchTemplate(sub_roi, stpl, sres, cv::TM_CCOEFF_NORMED);
        double sVal; cv::minMaxLoc(sres, nullptr, &sVal, nullptr, nullptr);
        if (!(sVal == sVal)) continue;

        if (sVal >= sub_threshold) {
            r.found = TRUE;
            r.score = maxVal;
            r.scale = s;
            r.x = maxLoc.x + ox; r.y = maxLoc.y + oy;
            r.w = w; r.h = h;
            r.cx = r.x + w / 2;
            r.cy = r.y + h / 2;
            return r;
        }
    }
    return r;
}

TMResult TM_FindUltimateSafe(const char *main_path, const char *anti_path,
                             double main_threshold, double anti_threshold,
                             BOOL fast, TMRegion region) {
    std::lock_guard<std::mutex> lk(g_mtx);
    TMResult r = {};
    CachedTemplate *mt = LoadTemplate(main_path);
    CachedTemplate *at = anti_path ? LoadTemplate(anti_path) : nullptr;
    if (!mt || g_screen_bgr.empty()) return r;
    if (anti_path && !at) return r;

    cv::Rect roi = ClampRegion(region);
    int ox = roi.x, oy = roi.y;
    cv::Mat src = g_screen_bgr(roi);
    cv::Mat src_gray = g_screen_gray(roi);
    if (src.empty()) return r;

    const int pad_slide = 5;
    std::vector<double> scales = GetScales(fast != FALSE);
    for (double s : scales) {
        cv::Mat mtpl, mtpl_gray, atpl;
        if (s == 1.0) {
            mtpl = mt->bgr; mtpl_gray = mt->gray;
            if (at) atpl = at->bgr;
        } else {
            cv::resize(mt->bgr,  mtpl,      cv::Size(), s, s, cv::INTER_AREA);
            cv::resize(mt->gray, mtpl_gray, cv::Size(), s, s, cv::INTER_AREA);
            if (at) cv::resize(at->bgr, atpl, cv::Size(), s, s, cv::INTER_AREA);
        }
        int h_m = mtpl.rows, w_m = mtpl.cols;
        if (h_m < 10 || w_m < 10 || h_m > src.rows || w_m > src.cols) continue;

        cv::Mat res_main;
        cv::matchTemplate(src, mtpl, res_main, cv::TM_CCOEFF_NORMED);

        /* Collect candidate points >= main_threshold. */
        std::vector<cv::Point> pts;
        for (int yy = 0; yy < res_main.rows; yy++) {
            const float *prow = res_main.ptr<float>(yy);
            for (int xx = 0; xx < res_main.cols; xx++)
                if (prow[xx] >= main_threshold) pts.push_back(cv::Point(xx, yy));
        }
        if (pts.empty()) continue;

        /* Force left-to-right column order (50px buckets), then top-down. */
        std::sort(pts.begin(), pts.end(), [](const cv::Point &a, const cv::Point &b) {
            int ca = a.x / 50, cb = b.x / 50;
            if (ca != cb) return ca < cb;
            return a.y < b.y;
        });

        std::set<std::pair<int,int>> checked;
        for (const cv::Point &pt : pts) {
            int x = pt.x, y = pt.y;
            std::pair<int,int> key(x / 10, y / 10);
            if (checked.count(key)) continue;
            checked.insert(key);

            double base_score = res_main.at<float>(y, x);
            cv::Mat roi_bgr  = src(cv::Rect(x, y, w_m, h_m));
            cv::Mat roi_gray = src_gray(cv::Rect(x, y, w_m, h_m));

            /* Defense 1: anti-template (reject if NEW tag nearby). */
            if (at && !atpl.empty()) {
                int h_a = atpl.rows, w_a = atpl.cols;
                int pad = 10;
                int ax1 = std::max(0, x - pad),       ay1 = std::max(0, y - pad);
                int ax2 = std::min(src.cols, x + w_m + pad), ay2 = std::min(src.rows, y + h_m + pad);
                cv::Mat anti_roi = src(cv::Rect(ax1, ay1, ax2 - ax1, ay2 - ay1));
                if (anti_roi.rows >= h_a && anti_roi.cols >= w_a) {
                    cv::Mat res_anti; double anti_score = 0;
                    cv::matchTemplate(anti_roi, atpl, res_anti, cv::TM_CCOEFF_NORMED);
                    cv::minMaxLoc(res_anti, nullptr, &anti_score, nullptr, nullptr);
                    if (anti_score >= anti_threshold) continue;  /* rejected */
                }
            }

            /* Defense 2: top 25% (car name text) on grayscale. */
            double score_top = 0.0;
            int top_h = (int)(h_m * 0.25);
            if (top_h > pad_slide * 2 && w_m > pad_slide * 2) {
                cv::Mat tpl_top_core = mtpl_gray(cv::Rect(pad_slide, pad_slide,
                                                          w_m - 2 * pad_slide,
                                                          top_h - 2 * pad_slide));
                int sh = std::min((int)(h_m * 0.35), roi_gray.rows);
                cv::Mat search_top = roi_gray(cv::Rect(0, 0, w_m, sh));
                if (search_top.rows >= tpl_top_core.rows && search_top.cols >= tpl_top_core.cols) {
                    cv::Mat res_top;
                    cv::matchTemplate(search_top, tpl_top_core, res_top, cv::TM_CCOEFF_NORMED);
                    cv::minMaxLoc(res_top, nullptr, &score_top, nullptr, nullptr);
                }
            }

            /* Defense 3: bottom-right 25%h x 35%w (PI / tuning box) on color. */
            double score_bot = 0.0;
            int bottom_h = (int)(h_m * 0.25);
            int right_w  = (int)(w_m * 0.35);
            if (bottom_h > pad_slide * 2 && right_w > pad_slide * 2) {
                cv::Mat tpl_pi_box = mtpl(cv::Rect(w_m - right_w, h_m - bottom_h, right_w, bottom_h));
                cv::Mat tpl_pi_core = tpl_pi_box(cv::Rect(pad_slide, pad_slide,
                                                          right_w - 2 * pad_slide,
                                                          bottom_h - 2 * pad_slide));
                int sy1 = std::max(0, h_m - (int)(h_m * 0.35));
                int sx1 = std::max(0, w_m - (int)(w_m * 0.45));
                cv::Mat search_bot = roi_bgr(cv::Rect(sx1, sy1, w_m - sx1, h_m - sy1));
                if (search_bot.rows >= tpl_pi_core.rows && search_bot.cols >= tpl_pi_core.cols) {
                    cv::Mat res_bot;
                    cv::matchTemplate(search_bot, tpl_pi_core, res_bot, cv::TM_CCOEFF_NORMED);
                    cv::minMaxLoc(res_bot, nullptr, &score_bot, nullptr, nullptr);
                }
            }

            if (base_score >= 0.76 && score_top >= 0.75 && score_bot >= 0.85) {
                r.found = TRUE;
                r.score = base_score;
                r.scale = s;
                r.x = x + ox; r.y = y + oy;
                r.w = w_m;    r.h = h_m;
                r.cx = r.x + w_m / 2;
                r.cy = r.y + h_m / 2;
                return r;
            }
        }
    }
    return r;
}

TMResult TM_FindFocusRing(TMRegion region) {
    std::lock_guard<std::mutex> lk(g_mtx);
    TMResult r = {};
    if (g_screen_bgr.empty()) return r;

    cv::Rect roi = ClampRegion(region);
    int ox = roi.x, oy = roi.y;
    cv::Mat sub = g_screen_bgr(roi);

    cv::Mat hsv;
    cv::cvtColor(sub, hsv, cv::COLOR_BGR2HSV);

    /* Bright lime/chartreuse focus ring: H~35-70, high S, high V. */
    cv::Mat mask;
    cv::inRange(hsv, cv::Scalar(35, 120, 180), cv::Scalar(70, 255, 255), mask);

    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(9, 9));
    cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, kernel);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    int minW = g_screen_w / 20, minH = g_screen_h / 20;
    cv::Rect best;
    double bestArea = 0;
    for (auto &c : contours) {
        cv::Rect b = cv::boundingRect(c);
        if (b.width < minW || b.height < minH) continue;
        double area = (double)b.width * b.height;
        if (area > bestArea) { bestArea = area; best = b; }
    }
    if (bestArea > 0) {
        r.found = TRUE;
        r.score = 1.0;
        r.x = best.x + ox; r.y = best.y + oy;
        r.w = best.width;  r.h = best.height;
        r.cx = r.x + r.w / 2;
        r.cy = r.y + r.h / 2;
    }
    return r;
}

/* Legacy full-frame wrappers */
TMResult TM_FindImage(const char *path, double threshold) {
    TMRegion full = { 0, 0, 0, 0 };
    return TM_FindColor(path, threshold, TRUE, full);
}
TMResult TM_FindImageGray(const char *path, double threshold, BOOL invert) {
    TMRegion full = { 0, 0, 0, 0 };
    return TM_FindGray(path, threshold, TRUE, invert, full);
}
TMResult TM_FindImageTransparent(const char *path, double threshold) {
    TMRegion full = { 0, 0, 0, 0 };
    return TM_FindTransparent(path, threshold, TRUE, full);
}

} /* extern "C" */
