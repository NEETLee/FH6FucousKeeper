/*
 * template_match.h - OpenCV template matching for FH6 UI elements
 *
 * Faithful port of FH6Auto's matching: region-restricted, multi-scale
 * (base 2560), color / grayscale / transparent variants.
 * Region cropping is essential to avoid false positives.
 * All coordinates are in WGC capture pixel space (== client coords).
 */

#ifndef FOCUSKEEPER_TEMPLATE_MATCH_H
#define FOCUSKEEPER_TEMPLATE_MATCH_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    BOOL   found;
    double score;
    int    cx, cy;       /* center of match (frame coords) */
    int    x, y, w, h;  /* bounding box of match (frame coords) */
    double scale;
} TMResult;

/* A search region in frame pixel coords. rw<=0 || rh<=0 means full frame. */
typedef struct { int x, y, w, h; } TMRegion;

/* Initialize the template matching system (call once). */
BOOL TM_Init(void);
void TM_Shutdown(void);

/* Set the current screen frame (BGRA from WGC). */
void TM_SetFrame(const BYTE *pixels, int width, int height, int stride);
void TM_GetFrameSize(int *width, int *height);

/*
 * Named region helper (mirrors FH6Auto self.regions).
 * Valid names: "full","left","right","top","bottom",
 *              "topleft","topright","bottomleft","bottomright","center".
 * Returns a region computed from the current frame size.
 */
TMRegion TM_NamedRegion(const char *name);

/* ─── Region-aware matchers (primary API, mirrors FH6Auto) ───────────── */

/* find_image: color BGR multi-scale match within region. */
TMResult TM_FindColor(const char *template_path, double threshold,
                      BOOL fast, TMRegion region);

/* find_image_gray: grayscale match (optional invert) within region. */
TMResult TM_FindGray(const char *template_path, double threshold,
                     BOOL fast, BOOL invert, TMRegion region);

/* find_image_transparent: alpha-mask match within region. */
TMResult TM_FindTransparent(const char *template_path, double threshold,
                            BOOL fast, TMRegion region);

/*
 * Two-stage match (mirrors FH6Auto find_image_with_element_multi, simplified):
 * locate `main_path` by color match, then verify `sub_path` exists within a
 * padded ROI around the candidate. Used to confirm a "new car" tile by the
 * presence of the NEW tag. Returns the main match center on success.
 */
TMResult TM_FindWithElement(const char *main_path, const char *sub_path,
                            double main_threshold, double sub_threshold,
                            BOOL fast, TMRegion region);

/*
 * Ultimate-safe match (mirrors FH6Auto find_image_ultimate_safe): used for
 * destructive car removal so it never deletes the wrong car. 4 defenses:
 *   0. base color score >= main_threshold (and >= 0.76)
 *   1. reject if anti_path (e.g. NEW tag) matches >= anti_threshold nearby
 *   2. top 25% (car name) grayscale score >= 0.75
 *   3. bottom-right (PI/tuning box) color score >= 0.85
 * Candidates are scanned left-to-right. Returns center on first that passes all.
 */
TMResult TM_FindUltimateSafe(const char *main_path, const char *anti_path,
                             double main_threshold, double anti_threshold,
                             BOOL fast, TMRegion region);

/*
 * Detect the keyboard focus ring (the bright lime-green highlight border)
 * within a region. Returns found=TRUE with the focused card's bounding box
 * and center. Used to drive arrow-key navigation in the background, since
 * mouse hover does not move the focus ring.
 */
TMResult TM_FindFocusRing(TMRegion region);

/* ─── Full-frame convenience wrappers (legacy) ───────────────────────── */

TMResult TM_FindImage(const char *template_path, double threshold);
TMResult TM_FindImageGray(const char *template_path, double threshold, BOOL invert);
TMResult TM_FindImageTransparent(const char *template_path, double threshold);

/* ─── Debug helpers (runtime-gated; the loader only enables these in a
 *      FK_DEBUG build, so release builds never turn them on) ──────────── */

/* Enable/query visual decision snapshots. */
void TM_DebugSetEnabled(int enabled);
int  TM_DebugIsEnabled(void);
/* Base directory for snapshots; a per-launch subdir (run_YYYYmmdd_HHMMSS) is
 * created lazily on the first snapshot. Defaults to "debug". */
void TM_DebugSetDir(const char *dir);
/* Save the CURRENT frame as an annotated PNG (box + score) under tag `tag`.
 * No-op unless enabled. Pass w<=0 / h<=0 to skip drawing the box. */
void TM_DebugSnap(const char *tag, double score, int x, int y, int w, int h);

/* Load an image file as the current frame (offline replay tool). */
BOOL TM_LoadFrameFromFile(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* FOCUSKEEPER_TEMPLATE_MATCH_H */
