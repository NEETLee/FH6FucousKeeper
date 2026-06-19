/*
 * farm_economy.h - OCR-based economy reading + buy-quantity calculation
 *
 * Reads the player's credits (CR) balance and available skill points (SP)
 * from the FH6 UI via OCR on known screen regions, and computes how many
 * consumable cars (22B) can be processed this cycle.
 *
 * Coordinates are normalized (0..1) so they scale to any resolution.
 */

#ifndef FOCUSKEEPER_FARM_ECONOMY_H
#define FOCUSKEEPER_FARM_ECONOMY_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Normalized ROI (fractions of frame width/height). */
typedef struct { float x, y, w, h; } EconRect;

/* Default calibrated crop regions (normalized 0..1):
 *   BALANCE: tight strip on the CR number row (top-right, below player name).
 *   SKILL  : "NN 技术点数可用" on the 车辆 tab.
 * Both are cropped from the frame and OCRed in isolation (with upscale).
 */
extern const EconRect ECON_REGION_BALANCE;
extern const EconRect ECON_REGION_SKILL;

/* Init/shutdown the OCR backend (wraps OcrEngine_*). lang e.g. L"en-US". */
BOOL FarmEconomy_Init(const WCHAR *lang);
void FarmEconomy_Shutdown(void);
BOOL FarmEconomy_IsReady(void);

/*
 * OCR a normalized ROI of a BGRA frame and parse the first integer found
 * (commas/spaces ignored). Returns the value, or -1 if no digits read.
 */
int FarmEconomy_ReadNumber(const BYTE *pixels, int width, int height,
                           int stride, EconRect roi);

/*
 * OCR an explicit pixel RECT and parse the first integer. Used by the
 * anchor-based read path (locate the CR coin icon, then read the strip to its
 * right). RECT is auto-clamped to the frame. Returns the value, or -1.
 */
int FarmEconomy_ReadNumberRectPx(const BYTE *pixels, int width, int height, int stride,
                                 RECT rc, int upscale, int min_digits);

/* Convenience wrappers using the built-in regions. -1 on failure. */
int FarmEconomy_ReadBalance(const BYTE *pixels, int width, int height, int stride);
int FarmEconomy_ReadSkillPoints(const BYTE *pixels, int width, int height, int stride);

/*
 * Compute how many cars to process: min(balance/cost_per_car, sp/sp_per_car).
 * Negative inputs are treated as "unknown" and drop out of the min(); if both
 * are unknown returns 0. cost_per_car/sp_per_car default to 81700/30 when <=0.
 */
int FarmEconomy_ComputeCount(long balance, long skill_points,
                             long cost_per_car, long sp_per_car);

#ifdef __cplusplus
}
#endif

#endif /* FOCUSKEEPER_FARM_ECONOMY_H */
