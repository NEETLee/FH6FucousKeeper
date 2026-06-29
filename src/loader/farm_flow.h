/*
 * farm_flow.h - Business flow state machines for FH6 automation
 *
 * Each flow (buy_car, wheelspin, remove_car, race) is a self-contained
 * state machine that uses game_input + template_match to navigate and act.
 */

#ifndef FOCUSKEEPER_FARM_FLOW_H
#define FOCUSKEEPER_FARM_FLOW_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FarmEngine FarmEngine;

/* Logging callback */
typedef void (*FarmLogFunc)(const char *msg, void *ctx);

/* Frame grab callback - must call TM_SetFrame internally */
typedef BOOL (*FarmGrabFunc)(void *ctx);

/* Create/destroy the farm engine */
FarmEngine* Farm_Create(void);
void        Farm_Destroy(FarmEngine *fe);

/* Initialize with dependencies */
typedef struct {
    HWND         game_hwnd;
    const char  *assets_dir;   /* path to shared assets/templates/ */
    const char  *car_assets_dir; /* per-car template folder (override; may be NULL/"") */
    FarmLogFunc  log_func;
    void        *log_ctx;
    FarmGrabFunc grab_func;
    void        *grab_ctx;
    /* Per-car mastery skill-tree path (VK codes). Empty -> built-in 22B path. */
    DWORD        skill_dirs[16];
    int          skill_count;
    /* Farming car identity for OCR-based selection on the race car-select
     * screen (resolution-independent, no per-car template needed). The race
     * step looks for a car card whose text matches car_name (name tokens) and
     * car_pi (performance index). Empty name + car_pi<=0 -> template fallback. */
    WCHAR        car_name[64];
    int          car_pi;
} FarmConfig;

BOOL Farm_Init(FarmEngine *fe, const FarmConfig *cfg);
void Farm_Shutdown(FarmEngine *fe);
/* Refresh config + game window on each pipeline run. */
BOOL Farm_Refresh(FarmEngine *fe, const FarmConfig *cfg);

/* Stop flag (can be set from another thread) */
void Farm_RequestStop(FarmEngine *fe);
void Farm_ClearStop(FarmEngine *fe);
BOOL Farm_IsRunning(FarmEngine *fe);

/* ─── Flow entry points ─────────────────────────────────────────────── */

/*
 * enter_menu: navigate to the main menu state.
 * Repeatedly presses ESC until collectionjournal.png is detected.
 * Returns TRUE if menu reached within timeout.
 */
BOOL Farm_EnterMenu(FarmEngine *fe);

/*
 * goto_vehicles_tab: from any menu state, reach the "车辆" (Vehicles) tab.
 * Menu tabs are switched with PageUp/PageDown, so this normalizes to the
 * leftmost tab (剧情) and steps right once. Both the CR balance and the
 * "NN 技术点数可用" skill-point counter are visible there, so it's the single
 * screen used for OCR economy reads. Returns TRUE if the menu was reached.
 */
BOOL Farm_GotoVehiclesTab(FarmEngine *fe);
/* Navigate to the 剧情 tab (needed for 收集簿 / buy_car). */
BOOL Farm_GotoStoryTab(FarmEngine *fe);

/*
 * buy_car: buy a specific car N times.
 * Navigates: menu -> collectionjournal -> masterexplorer -> carcollection
 *           -> brand -> consumablecar -> purchase loop
 * Returns number of cars successfully purchased.
 */
int Farm_BuyCar(FarmEngine *fe, int target_count);

/*
 * super_wheelspin: use skill points on car mastery for super wheelspins.
 *   mode 1: start from "My Vehicles" (Enter), get on car via rc.png
 *   mode 2: start from "Design & Paint" (DandP -> choosecar), get on car via Enter
 * Returns number of wheelspins completed.
 */
int Farm_SuperWheelspinMode(FarmEngine *fe, int target_count, int mode);
/* Back-compat wrapper: defaults to mode 1. */
int Farm_SuperWheelspin(FarmEngine *fe, int target_count);

/*
 * remove_car: remove cars from garage.
 *   mode 1: image-recognition removal (find_and_remove_consumable_car) -
 *           filters to Subaru brand, locates the 22B via ultimate-safe match
 *           (rejects NEW cars), removes only confirmed targets.
 *   mode 2: remove most-recently-acquired (sell_consumable_car) - sorts by
 *           recently acquired and deletes from the top.
 * Returns number of cars removed.
 */
int Farm_RemoveCarMode(FarmEngine *fe, int target_count, int mode);
/* Back-compat wrapper: defaults to mode 2 (most recent). */
int Farm_RemoveCar(FarmEngine *fe, int target_count);

/*
 * race: run EventLab races in a loop.
 * share_code: the share code to enter.
 * Returns number of races completed.
 */
int Farm_Race(FarmEngine *fe, const char *share_code, int target_count);

#ifdef __cplusplus
}
#endif

#endif /* FOCUSKEEPER_FARM_FLOW_H */
