/*
 * farm_nav.h - Farm Navigation Engine
 *
 * Provides OCR-verified keyboard navigation for game menu automation.
 * Core abstraction: "press key, then verify screen state via OCR"
 *
 * Three-layer design:
 *   1. Primitives: press_key, wait_ocr, delay
 *   2. Navigation helpers: enter_menu, nav_to_buysell, etc.
 *   3. Flow controllers: buy_car, wheelspin, remove_car
 */

#ifndef FOCUSKEEPER_FARM_NAV_H
#define FOCUSKEEPER_FARM_NAV_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ─── Configuration ──────────────────────────────────────────────── */

typedef struct {
    int  target_count;          /* How many cars to buy/remove/spin */
    int  sell_mode;             /* 1=filter consumable, 2=sort recent */
    WCHAR skill_dirs[16][8];   /* Skill path directions (e.g. "right","up"...) */
    int  skill_dir_count;      /* Number of directions in path */
    WCHAR ocr_lang[16];        /* OCR language (e.g. "zh-Hans-CN") */
} FarmConfig;

/* ─── Callback for progress reporting ────────────────────────────── */

typedef enum {
    FARM_IDLE,
    FARM_RUNNING,
    FARM_PAUSED,
    FARM_DONE,
    FARM_ERROR,
} FarmState;

typedef void (*FarmProgressCallback)(
    FarmState state,
    int current_count,
    int target_count,
    const WCHAR *message,
    void *user_data
);

/* ─── Farm Engine API ────────────────────────────────────────────── */

typedef struct FarmEngine FarmEngine;

/* Create/destroy */
FarmEngine* Farm_Create(void);
void        Farm_Destroy(FarmEngine *engine);

/* Initialize (call once, sets up WGC + OCR + input) */
BOOL Farm_Init(FarmEngine *engine, HWND game_hwnd, const FarmConfig *config);
void Farm_Shutdown(FarmEngine *engine);

/* Set callback for progress updates */
void Farm_SetCallback(FarmEngine *engine, FarmProgressCallback cb, void *user_data);

/* Start a flow (runs on background thread) */
BOOL Farm_StartBuyCar(FarmEngine *engine);
BOOL Farm_StartWheelspin(FarmEngine *engine);
BOOL Farm_StartRemoveCar(FarmEngine *engine);

/* Control */
void Farm_Stop(FarmEngine *engine);
void Farm_Pause(FarmEngine *engine);
void Farm_Resume(FarmEngine *engine);

/* Query state */
FarmState Farm_GetState(FarmEngine *engine);
int  Farm_GetCount(FarmEngine *engine);

#ifdef __cplusplus
}
#endif

#endif /* FOCUSKEEPER_FARM_NAV_H */
