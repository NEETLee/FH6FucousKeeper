/*
 * farm_flow.c - Business flow state machines
 *
 * Implements enter_menu + buy_car (phase 2).
 * Other flows (wheelspin, remove, race) stubbed for phase 3.
 */

#include "farm_flow.h"
#include "game_input.h"
#include "template_match.h"
#include "xbox_textentry.h"
#include "screen_capture.h"
#include "ocr_engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

struct FarmEngine {
    GameInput   *input;
    FarmConfig   cfg;
    volatile BOOL running;
    volatile BOOL stop_requested;
    int          car_counter;
    char         tmpl_path[512];   /* scratch buffer for template paths */
    char         tmpl_path2[512];  /* second buffer (for two-template matchers) */
};

/* ─── Helpers ────────────────────────────────────────────────────────── */

static void farm_log(FarmEngine *fe, const char *msg) {
    if (fe->cfg.log_func)
        fe->cfg.log_func(msg, fe->cfg.log_ctx);
}

/* Resolve a template path into `buf`. Car-specific templates (e.g. the 22B's
 * consumablecar/removecarobject/CCbrand) live in the selected car's folder;
 * if present there it wins, otherwise we fall back to the shared assets dir. */
static char* tmpl_into(FarmEngine *fe, char *buf, size_t bufsz, const char *name) {
    if (fe->cfg.car_assets_dir && fe->cfg.car_assets_dir[0]) {
        snprintf(buf, bufsz, "%s/%s", fe->cfg.car_assets_dir, name);
        if (GetFileAttributesA(buf) != INVALID_FILE_ATTRIBUTES)
            return buf;
    }
    snprintf(buf, bufsz, "%s/%s", fe->cfg.assets_dir, name);
    return buf;
}

static char* tmpl(FarmEngine *fe, const char *name) {
    return tmpl_into(fe, fe->tmpl_path, sizeof(fe->tmpl_path), name);
}
/* Second buffer: required when one call needs two distinct template paths,
 * otherwise both args would alias the single tmpl() scratch buffer. */
static char* tmpl2(FarmEngine *fe, const char *name) {
    return tmpl_into(fe, fe->tmpl_path2, sizeof(fe->tmpl_path2), name);
}

static BOOL grab(FarmEngine *fe) {
    if (fe->cfg.grab_func)
        return fe->cfg.grab_func(fe->cfg.grab_ctx);
    return FALSE;
}

static BOOL check_stop(FarmEngine *fe) {
    return fe->stop_requested;
}

static void farm_sleep(FarmEngine *fe, int ms) {
    (void)fe;
    Sleep(ms);
}

/* ─── Input helpers (background-verified) ────────────────────────────── */

/* Single background mouse click (list items / buttons). */
static void click1(FarmEngine *fe, int x, int y) {
    GameInput_MouseClick(fe->input, x, y);
}
/* Double background mouse click (big tiles in collection journal). */
static void click2(FarmEngine *fe, int x, int y) {
    GameInput_MouseDoubleClick(fe->input, x, y);
}
/* Keyboard press. */
static void key(FarmEngine *fe, DWORD vk, int hold_ms) {
    GameInput_Press(fe->input, vk, hold_ms > 0 ? hold_ms : 80);
}
/* Move cursor away to avoid hover tooltips (FH6Auto move_to_game_coord(5,5)). */
static void away(FarmEngine *fe) {
    GameInput_MoveAway(fe->input);
}

/* ─── Matching helpers (best of gray/color/transparent) ──────────────── */

/* Locate a template in a named region, returning the best score across the
 * three matching modes. (FH6Auto picks one mode per element; using the best
 * of all three is more robust in the background.) */
static TMResult find_best(FarmEngine *fe, const char *name, const char *region_name) {
    grab(fe);
    TMRegion rg = TM_NamedRegion(region_name);
    TMResult g = TM_FindGray(tmpl(fe, name), 0.0, TRUE, FALSE, rg);
    TMResult c = TM_FindColor(tmpl(fe, name), 0.0, TRUE, rg);
    TMResult t = TM_FindTransparent(tmpl(fe, name), 0.0, TRUE, rg);
    TMResult best = g;
    if (c.score > best.score) best = c;
    if (t.score > best.score) best = t;
    return best;
}

#ifdef FK_DEBUG
/* Save an annotated snapshot of the current frame for offline inspection.
 * Compiled out entirely in release builds. */
static void farm_snap(FarmEngine *fe, const char *tag, TMResult r) {
    (void)fe;
    if (!TM_DebugIsEnabled()) return;
    TM_DebugSnap(tag, r.score, r.x, r.y, r.w, r.h);
}
#define FARM_SNAP(fe, tag, r) farm_snap((fe), (tag), (r))
#else
#define FARM_SNAP(fe, tag, r) ((void)0)
#endif

/* Wait until a template's best score reaches threshold. */
static TMResult wait_best(FarmEngine *fe, const char *name, const char *region_name,
                          double threshold, int timeout_ms, int interval_ms) {
    TMResult r = {0};
    DWORD start = GetTickCount();
    if (interval_ms <= 0) interval_ms = 300;
    while (!check_stop(fe)) {
        r = find_best(fe, name, region_name);
        if (r.score >= threshold) { r.found = TRUE; return r; }
        if ((int)(GetTickCount() - start) >= timeout_ms) break;
        Sleep(interval_ms);
    }
    r.found = (r.score >= threshold);
    return r;
}

/* Compatibility wrapper for the race flow (full-frame best-of wait). */
static TMResult wait_template(FarmEngine *fe, const char *name,
                              double threshold, int timeout_ms, int interval_ms) {
    return wait_best(fe, name, "full", threshold, timeout_ms, interval_ms);
}

/* Wait for any of several templates (best score across all + modes). */
static TMResult wait_any_best(FarmEngine *fe, const char *const *names, int n,
                              const char *region_name, double threshold,
                              int timeout_ms, int interval_ms) {
    TMResult best = {0};
    DWORD start = GetTickCount();
    if (interval_ms <= 0) interval_ms = 300;
    while (!check_stop(fe)) {
        best.score = 0;
        for (int i = 0; i < n; i++) {
            TMResult r = find_best(fe, names[i], region_name);
            if (r.score > best.score) best = r;
        }
        if (best.score >= threshold) { best.found = TRUE; return best; }
        if ((int)(GetTickCount() - start) >= timeout_ms) break;
        Sleep(interval_ms);
    }
    best.found = (best.score >= threshold);
    return best;
}

/* ─── Public API ─────────────────────────────────────────────────────── */

FarmEngine* Farm_Create(void) {
    FarmEngine *fe = (FarmEngine*)calloc(1, sizeof(FarmEngine));
    return fe;
}

void Farm_Destroy(FarmEngine *fe) {
    if (!fe) return;
    Farm_Shutdown(fe);
    free(fe);
}

BOOL Farm_Init(FarmEngine *fe, const FarmConfig *cfg) {
    if (!fe || !cfg) return FALSE;
    fe->cfg = *cfg;
    fe->input = GameInput_Create();
    if (!GameInput_Init(fe->input, cfg->game_hwnd)) {
        farm_log(fe, "GameInput_Init failed");
        return FALSE;
    }
    fe->running = TRUE;
    fe->stop_requested = FALSE;
    fe->car_counter = 0;
    return TRUE;
}

void Farm_Shutdown(FarmEngine *fe) {
    if (!fe) return;
    fe->running = FALSE;
    if (fe->input) {
        GameInput_Destroy(fe->input);
        fe->input = NULL;
    }
}

BOOL Farm_Refresh(FarmEngine *fe, const FarmConfig *cfg) {
    if (!fe || !cfg) return FALSE;
    fe->cfg = *cfg;
    if (!fe->input) {
        fe->input = GameInput_Create();
        if (!fe->input) return FALSE;
    }
    if (!GameInput_Init(fe->input, cfg->game_hwnd)) {
        farm_log(fe, "GameInput_Init failed");
        return FALSE;
    }
    fe->running = TRUE;
    return TRUE;
}

void Farm_RequestStop(FarmEngine *fe) {
    if (fe) fe->stop_requested = TRUE;
}

void Farm_ClearStop(FarmEngine *fe) {
    if (fe) {
        fe->stop_requested = FALSE;
        fe->running = TRUE;
    }
}

BOOL Farm_IsRunning(FarmEngine *fe) {
    return fe && fe->running && !fe->stop_requested;
}

/* Return TRUE when the main menu is visible (any tab). */
static BOOL menu_visible(FarmEngine *fe) {
    grab(fe);
    TMRegion left = TM_NamedRegion("left");
    TMResult cj = TM_FindGray(tmpl(fe, "collectionjournal.png"), 0.70, TRUE, FALSE, left);
    if (cj.found) return TRUE;
    TMResult bn = TM_FindGray(tmpl(fe, "BNandUC.png"), 0.65, TRUE, FALSE, left);
    if (bn.found) return TRUE;
    /* FH logo / tab bar – present on every main-menu tab. */
    TMResult logo = TM_FindGray(tmpl(fe, "horizon6.png"), 0.65, TRUE, FALSE,
                                TM_NamedRegion("full"));
    return logo.found;
}

static BOOL vehicles_tab_visible(FarmEngine *fe) {
    TMResult r = find_best(fe, "BNandUC.png", "left");
    return r.found && r.score >= 0.65;
}

static BOOL story_tab_visible(FarmEngine *fe) {
    grab(fe);
    TMRegion left = TM_NamedRegion("left");
    TMResult r = TM_FindGray(tmpl(fe, "collectionjournal.png"), 0.70, TRUE, FALSE, left);
    return r.found;
}

/* Buy & Sell tab – try top first (FH6Auto 上), then left. */
static TMResult wait_buyandsell(FarmEngine *fe, const char *const *names, int n,
                                double threshold, int timeout_ms) {
    TMResult r = wait_any_best(fe, names, n, "top", threshold, timeout_ms, 500);
    if (r.found) return r;
    return wait_any_best(fe, names, n, "left", threshold, timeout_ms / 2, 500);
}

/* Menu -> 车辆 tab -> BNandUC -> Enter (5s load). */
static BOOL open_buy_new_used_cars(FarmEngine *fe) {
    if (!Farm_EnterMenu(fe)) return FALSE;

    TMResult bn = find_best(fe, "BNandUC.png", "left");
    if (!bn.found || bn.score < 0.65) {
        if (story_tab_visible(fe)) {
            farm_log(fe, "open_buycars: PageDown from 剧情");
            key(fe, VK_NEXT, 150);
            farm_sleep(fe, 1000);
        } else if (!vehicles_tab_visible(fe)) {
            farm_log(fe, "open_buycars: go to 车辆 tab");
            if (!Farm_GotoVehiclesTab(fe)) return FALSE;
        }
    }

    TMResult r = wait_best(fe, "BNandUC.png", "left", 0.70, 15000, 300);
    if (!r.found) {
        farm_log(fe, "open_buycars: BNandUC not found");
        return FALSE;
    }
    click1(fe, r.cx, r.cy);
    farm_sleep(fe, 800);
    key(fe, VK_RETURN, 80);
    farm_sleep(fe, 5000);
    return TRUE;
}

/* After driving the favorited car, wait for Buy & Sell and re-enter my vehicles. */
static BOOL reenter_my_vehicles(FarmEngine *fe) {
    static const char *BS2[] = { "buyandsell-b.png", "buyandsell-w.png" };
    for (int i = 0; i < 30 && !check_stop(fe); i++) {
        TMResult b = wait_any_best(fe, BS2, 2, "top", 0.70, 1500, 200);
        if (!b.found)
            b = wait_any_best(fe, BS2, 2, "left", 0.70, 800, 200);
        if (b.found) {
            farm_log(fe, "remove_car: re-enter my vehicles");
            key(fe, VK_RETURN, 80);
            farm_sleep(fe, 1500);
            return TRUE;
        }
        farm_sleep(fe, 1000);
    }
    return FALSE;
}

/* ─── enter_menu (gray, "left" region, threshold 0.70) ─── */

BOOL Farm_EnterMenu(FarmEngine *fe) {
    farm_log(fe, "enter_menu: trying to reach main menu...");
    for (int i = 0; i < 60; i++) {
        if (check_stop(fe)) return FALSE;

        if (menu_visible(fe)) {
            char msg[128];
            snprintf(msg, sizeof(msg), "enter_menu: menu found (%d/60)", i + 1);
            farm_log(fe, msg);
            farm_sleep(fe, 400);
            return TRUE;
        }

        char msg[64];
        snprintf(msg, sizeof(msg), "enter_menu: not in menu (%d/60), pressing ESC", i+1);
        farm_log(fe, msg);
        GameInput_Press(fe->input, VK_ESCAPE, 80);
        farm_sleep(fe, 1000);
    }
    farm_log(fe, "enter_menu: FAILED after 60 attempts");
    return FALSE;
}

/* ─── goto_vehicles_tab (PageUp/PageDown tab switching) ──────────────── */

BOOL Farm_GotoVehiclesTab(FarmEngine *fe) {
    if (!fe || check_stop(fe)) return FALSE;

    /* 1. Already on 车辆 tab – do nothing. */
    if (vehicles_tab_visible(fe)) {
        farm_log(fe, "goto_vehicles_tab: already on 车辆 tab");
        return TRUE;
    }

    /* 2. Roaming / submenu → ESC into menu. */
    if (!menu_visible(fe)) {
        if (!Farm_EnterMenu(fe)) return FALSE;
    }

    if (vehicles_tab_visible(fe)) {
        farm_log(fe, "goto_vehicles_tab: on 车辆 tab after enter_menu");
        return TRUE;
    }

    /* 3. On 剧情 tab (leftmost) → exactly one PageDown. */
    if (story_tab_visible(fe)) {
        farm_log(fe, "goto_vehicles_tab: on 剧情 tab, PageDown once");
        key(fe, VK_NEXT, 150);
        farm_sleep(fe, 900);
        if (vehicles_tab_visible(fe)) {
            farm_log(fe, "goto_vehicles_tab: reached 车辆 tab");
            return TRUE;
        }
    } else {
        /* 4. On some other menu tab → PageUp until 剧情, then one PageDown.
         *    Stop as soon as 剧情 anchor appears (no blind spam). */
        farm_log(fe, "goto_vehicles_tab: normalize to 剧情 tab");
        for (int i = 0; i < 6 && !check_stop(fe); i++) {
            if (story_tab_visible(fe)) break;
            key(fe, VK_PRIOR, 150);
            farm_sleep(fe, 600);
        }
        if (story_tab_visible(fe)) {
            farm_log(fe, "goto_vehicles_tab: on 剧情, PageDown once");
            key(fe, VK_NEXT, 150);
            farm_sleep(fe, 900);
        }
    }

    if (vehicles_tab_visible(fe)) {
        farm_log(fe, "goto_vehicles_tab: verified");
        return TRUE;
    }

    farm_log(fe, "goto_vehicles_tab: could not reach 车辆 tab");
    return FALSE;
}

/* ─── goto_story_tab (for buy_car / 收集簿 on 剧情 tab) ─────────────── */

BOOL Farm_GotoStoryTab(FarmEngine *fe) {
    if (!fe || check_stop(fe)) return FALSE;

    if (story_tab_visible(fe)) {
        farm_log(fe, "goto_story_tab: already on 剧情 tab");
        return TRUE;
    }

    if (!menu_visible(fe)) {
        if (!Farm_EnterMenu(fe)) return FALSE;
    }

    if (story_tab_visible(fe)) return TRUE;

    /* From 车辆 tab (common after read-econ): one PageUp. */
    if (vehicles_tab_visible(fe)) {
        farm_log(fe, "goto_story_tab: PageUp from 车辆 -> 剧情");
        key(fe, VK_PRIOR, 150);
        farm_sleep(fe, 900);
        if (story_tab_visible(fe)) return TRUE;
    }

    /* From any other menu tab: PageUp until 剧情. */
    farm_log(fe, "goto_story_tab: PageUp to 剧情");
    for (int i = 0; i < 6 && !check_stop(fe); i++) {
        if (story_tab_visible(fe)) return TRUE;
        key(fe, VK_PRIOR, 150);
        farm_sleep(fe, 600);
    }

    if (story_tab_visible(fe)) return TRUE;
    farm_log(fe, "goto_story_tab: could not reach 剧情 tab");
    return FALSE;
}

/* ─── buy_car (faithful port of FH6Auto logic_buy_car) ───────────────── */

int Farm_BuyCar(FarmEngine *fe, int target_count) {
    if (!fe || check_stop(fe)) return 0;

    char msg[160];
    snprintf(msg, sizeof(msg), "buy_car: target=%d", target_count);
    farm_log(fe, msg);

    /* Step 1: Enter menu on 剧情 tab (收集簿 is only visible there). */
    if (!Farm_GotoStoryTab(fe)) return fe->car_counter;

    /* Step 2: 收集簿 collectionjournal - left, double-click */
    TMResult r = wait_best(fe, "collectionjournal.png", "left", 0.65, 30000, 400);
    if (!r.found) { farm_log(fe, "buy_car: collectionjournal not found"); return fe->car_counter; }
    snprintf(msg, sizeof(msg), "buy_car: dclick collectionjournal (%d,%d) %.2f", r.cx, r.cy, r.score);
    farm_log(fe, msg);
    click2(fe, r.cx, r.cy);
    farm_sleep(fe, 1000);

    /* Step 3: 探索大师 masterexplorer - full, double-click */
    r = wait_best(fe, "masterexplorer.png", "full", 0.75, 30000, 400);
    if (!r.found) { farm_log(fe, "buy_car: masterexplorer not found"); return fe->car_counter; }
    snprintf(msg, sizeof(msg), "buy_car: dclick masterexplorer (%d,%d) %.2f", r.cx, r.cy, r.score);
    farm_log(fe, msg);
    click2(fe, r.cx, r.cy);
    farm_sleep(fe, 600);

    /* Step 4: 车辆收藏 carcollection - full, double-click */
    r = wait_best(fe, "carcollection.png", "full", 0.75, 30000, 300);
    if (!r.found) { farm_log(fe, "buy_car: carcollection not found"); return fe->car_counter; }
    snprintf(msg, sizeof(msg), "buy_car: dclick carcollection (%d,%d) %.2f", r.cx, r.cy, r.score);
    farm_log(fe, msg);
    click2(fe, r.cx, r.cy);
    farm_sleep(fe, 1000);

    /* Step 5: backspace (jump to brand filter) */
    key(fe, VK_BACK, 80);
    farm_sleep(fe, 500);

    /* Step 6: CCbrand - full, scroll UP to find (5 tries), single-click */
    TMResult brand_r = {0};
    for (int attempt = 0; attempt < 5; attempt++) {
        if (check_stop(fe)) return fe->car_counter;
        brand_r = wait_best(fe, "CCbrand.png", "full", 0.75, 800, 200);
        if (brand_r.found) break;
        farm_log(fe, "buy_car: CCbrand not found, pressing UP");
        key(fe, VK_UP, 80);
        farm_sleep(fe, 250);
    }
    if (!brand_r.found) { farm_log(fe, "buy_car: brand not found"); return fe->car_counter; }
    snprintf(msg, sizeof(msg), "buy_car: click brand (%d,%d) %.2f", brand_r.cx, brand_r.cy, brand_r.score);
    farm_log(fe, msg);
    click1(fe, brand_r.cx, brand_r.cy);
    farm_sleep(fe, 800);
    key(fe, VK_DOWN, 80);
    farm_sleep(fe, 400);

    /* Step 7: consumablecar - full, double-click.
     * FH6Auto uses 0.90, but background WGC capture scores the correct car
     * ~0.80 (HDR/color), so use 0.80; matchTemplate returns the best location. */
    r = wait_best(fe, "consumablecar.png", "full", 0.80, 8000, 300);
    if (!r.found) { farm_log(fe, "buy_car: consumablecar not found"); return fe->car_counter; }
    snprintf(msg, sizeof(msg), "buy_car: dclick consumablecar (%d,%d) %.2f", r.cx, r.cy, r.score);
    farm_log(fe, msg);
    click2(fe, r.cx, r.cy);
    farm_sleep(fe, 1000);

    /* Step 8: Purchase loop (Space -> Down -> Enter x3) */
    farm_log(fe, "buy_car: entering purchase loop");
    while (fe->car_counter < target_count) {
        if (check_stop(fe)) break;

        key(fe, VK_SPACE, 80);  farm_sleep(fe, 600); away(fe);
        key(fe, VK_DOWN, 80);   farm_sleep(fe, 200); away(fe);
        key(fe, VK_RETURN, 80); farm_sleep(fe, 600); away(fe);
        key(fe, VK_RETURN, 80); farm_sleep(fe, 600); away(fe);
        key(fe, VK_RETURN, 80); farm_sleep(fe, 700);

        fe->car_counter++;
        snprintf(msg, sizeof(msg), "buy_car: purchased %d/%d", fe->car_counter, target_count);
        farm_log(fe, msg);
    }

    /* Step 9: Exit back to menu */
    for (int i = 0; i < 5; i++) {
        if (check_stop(fe)) break;
        key(fe, VK_ESCAPE, 80);
        farm_sleep(fe, 800);
    }

    return fe->car_counter;
}

/* ─── super_wheelspin (faithful port of FH6Auto logic_super_wheelspin) ── */

int Farm_SuperWheelspin(FarmEngine *fe, int target_count) {
    /* Mode 2 (from Design & Paint) is the preferred default path. */
    return Farm_SuperWheelspinMode(fe, target_count, 2);
}

int Farm_SuperWheelspinMode(FarmEngine *fe, int target_count, int mode) {
    if (!fe || check_stop(fe)) return 0;
    if (mode != 2) mode = 1;
    int counter = 0;
    char msg[128];
    static const char *BS[]   = { "buyandsell-w.png", "buyandsell-b.png" };
    static const char *UANDT[]= { "UandT-w.png", "UandT-b.png" };
    static const char *CLS[]  = { "clsldcnw.png", "clsldcnb.png" };
    /* skill_dirs: from the active car profile; fall back to 22B (R,U,U,U,L). */
    DWORD skill_dirs[16] = { VK_RIGHT, VK_UP, VK_UP, VK_UP, VK_LEFT };
    int n_skill = 5;
    if (fe->cfg.skill_count > 0 && fe->cfg.skill_count <= 16) {
        n_skill = fe->cfg.skill_count;
        for (int i = 0; i < n_skill; i++) skill_dirs[i] = fe->cfg.skill_dirs[i];
    }
    BOOL detail_confirmed = FALSE;

    snprintf(msg, sizeof(msg), "wheelspin: starting (mode %d)", mode);
    farm_log(fe, msg);
    if (!open_buy_new_used_cars(fe)) return counter;

    /* Buy & Sell tab */
    TMResult bs = wait_buyandsell(fe, BS, 2, 0.75, 60000);
    if (!bs.found) { farm_log(fe, "wheelspin: buyandsell not found"); return counter; }
    click1(fe, bs.cx, bs.cy);
    farm_sleep(fe, 1000);
    key(fe, VK_NEXT, 150);
    farm_sleep(fe, 500);

    int loop_guard = 0;
    const int loop_max = target_count * 2 + 2;

    while (counter < target_count && !check_stop(fe)) {
        if (++loop_guard > loop_max) {
            farm_log(fe, "wheelspin: loop guard exceeded, stopping");
            break;
        }
        if (mode == 1) {
            /* mode 1: Enter "My Vehicles" */
            farm_log(fe, "wheelspin: entering my vehicles");
            key(fe, VK_RETURN, 80);
            farm_sleep(fe, 2000);
        } else {
            /* mode 2: enter "Design & Paint" -> "Choose Car" */
            farm_log(fe, "wheelspin: entering design & paint");
            TMResult dp = wait_best(fe, "DandP.png", "full", 0.70, 8000, 300);
            if (!dp.found) { farm_log(fe, "wheelspin: DandP not found"); return counter; }
            click1(fe, dp.cx, dp.cy);
            farm_sleep(fe, 1500);
            TMResult cc = wait_best(fe, "choosecar.png", "full", 0.70, 8000, 300);
            if (!cc.found) { farm_log(fe, "wheelspin: choosecar not found"); return counter; }
            click1(fe, cc.cx, cc.cy);
            farm_sleep(fe, 2000);
        }

        /* Jump to brand filter */
        key(fe, VK_BACK, 80);
        farm_sleep(fe, 1000);

        /* Find brand (scroll UP up to 30x) */
        TMResult brand = {0};
        for (int a = 0; a < 30 && !check_stop(fe); a++) {
            brand = wait_best(fe, "CCbrand.png", "full", 0.75, 800, 200);
            if (brand.found) break;
            key(fe, VK_UP, 80);
            farm_sleep(fe, 250);
        }
        if (!brand.found) { farm_log(fe, "wheelspin: brand not found"); return counter; }
        click1(fe, brand.cx, brand.cy);
        farm_sleep(fe, 1000);

        /* Search pages (right) for a NEW car: newCC + newcartag */
        BOOL found_car = FALSE;
        for (int pg = 0; pg < 85 && !check_stop(fe); pg++) {
            grab(fe);
            TMRegion full = TM_NamedRegion("full");
            TMResult car = TM_FindWithElement(tmpl(fe, "newCC.png"), tmpl2(fe, "newcartag.png"),
                                              0.80, 0.75, TRUE, full);
            if (!car.found && !detail_confirmed) {
                /* toggle detail state with P, retry once */
                key(fe, 'P', 80);
                farm_sleep(fe, 600);
                grab(fe);
                car = TM_FindWithElement(tmpl(fe, "newCC.png"), tmpl2(fe, "newcartag.png"),
                                         0.80, 0.75, TRUE, full);
            }
            if (car.found && car.score >= 0.80) {
                detail_confirmed = TRUE;
                snprintf(msg, sizeof(msg), "wheelspin: new car found pg=%d (%d,%d) %.2f",
                         pg, car.cx, car.cy, car.score);
                farm_log(fe, msg);
                click1(fe, car.cx, car.cy);
                found_car = TRUE;
                break;
            }
            /* Next page: 4x right */
            for (int k = 0; k < 4; k++) { key(fe, VK_RIGHT, 60); farm_sleep(fe, 100); }
            farm_sleep(fe, 400);
        }
        if (!found_car) { farm_log(fe, "wheelspin: no new car, list exhausted"); return counter; }

        /* Get on the car */
        farm_sleep(fe, 500);
        if (mode == 1) {
            /* mode 1: find "drive car" (rc) button, fallback to Enter x2 */
            TMResult rc = wait_best(fe, "rc.png", "full", 0.70, 500, 100);
            if (rc.found) {
                click1(fe, rc.cx, rc.cy);
                farm_sleep(fe, 2000);
            } else {
                key(fe, VK_RETURN, 80); farm_sleep(fe, 1000);
                key(fe, VK_RETURN, 80); farm_sleep(fe, 1000);
            }
        } else {
            /* mode 2: single Enter */
            key(fe, VK_RETURN, 80); farm_sleep(fe, 1000);
        }

        /* Upgrade & Tune (UandT) - ESC up to 20x until found, bottom-left */
        TMResult ut = {0};
        for (int i = 0; i < 20 && !check_stop(fe); i++) {
            ut = wait_any_best(fe, UANDT, 2, "bottomleft", 0.70, 300, 100);
            if (ut.found) break;
            key(fe, VK_ESCAPE, 80);
            farm_sleep(fe, 500);
        }
        if (!ut.found) { farm_log(fe, "wheelspin: UandT not found"); return counter; }
        click1(fe, ut.cx, ut.cy);
        farm_sleep(fe, 500);

        /* Car mastery (clsldcn), bottom-left */
        TMResult cls = wait_any_best(fe, CLS, 2, "bottomleft", 0.70, 20000, 400);
        if (!cls.found) { farm_log(fe, "wheelspin: mastery not found"); return counter; }
        click1(fe, cls.cx, cls.cy);
        farm_sleep(fe, 1500);

        /* Already-maxed check: EXPwU (gray-only, strict – color mode false-positives). */
        grab(fe);
        TMRegion left = TM_NamedRegion("left");
        TMResult exp = TM_FindGray(tmpl(fe, "EXPwU.png"), 0.82, TRUE, FALSE, left);
        if (exp.found) {
            char expmsg[96];
            snprintf(expmsg, sizeof(expmsg),
                     "wheelspin: skills already spent (EXPwU %.2f), skip", exp.score);
            farm_log(fe, expmsg);
            counter++;
        } else {
            farm_sleep(fe, 1000);
            key(fe, VK_RETURN, 80);
            farm_sleep(fe, 1500);

            for (int d = 0; d < n_skill && !check_stop(fe); d++) {
                key(fe, skill_dirs[d], 80);
                farm_sleep(fe, 200);
                key(fe, VK_RETURN, 80);
                farm_sleep(fe, 1200);
            }

            /* SPNE = no skill points -> finish early */
            TMResult spne = find_best(fe, "SPNE.png", "full");
            if (spne.score >= 0.70) {
                farm_log(fe, "wheelspin: no skill points left, ending early");
                key(fe, VK_RETURN, 80); farm_sleep(fe, 800);
                for (int i = 0; i < 3; i++) { key(fe, VK_ESCAPE, 80); farm_sleep(fe, 1000); }
                return counter;
            }
            counter++;
            snprintf(msg, sizeof(msg), "wheelspin: completed %d/%d", counter, target_count);
            farm_log(fe, msg);
        }

        if (counter >= target_count) break;

        /* Back out for next car: ESC ESC UP */
        key(fe, VK_ESCAPE, 80); farm_sleep(fe, 1200);
        key(fe, VK_ESCAPE, 80); farm_sleep(fe, 800);
        key(fe, VK_UP, 150);    farm_sleep(fe, 800);
    }

    key(fe, VK_ESCAPE, 80); farm_sleep(fe, 1200);
    key(fe, VK_ESCAPE, 80); farm_sleep(fe, 1200);
    return counter;
}

/* ─── remove_car ─────────────────────────────────────────────────────────
 * mode 1: find_and_remove_consumable_car (image-recognition, targets 22B)
 * mode 2: sell_consumable_car (most-recently-acquired)
 * Both share the same prelude (drive a favorited car so the consumable cars
 * become removable), then diverge.
 */

int Farm_RemoveCar(FarmEngine *fe, int target_count) {
    return Farm_RemoveCarMode(fe, target_count, 1);
}

int Farm_RemoveCarMode(FarmEngine *fe, int target_count, int mode) {
    if (!fe || check_stop(fe)) return 0;
    if (mode != 1) mode = 2;
    int counter = 0;
    char msg[128];
    static const char *BS[]  = { "buyandsell-w.png", "buyandsell-b.png" };

    snprintf(msg, sizeof(msg), "remove_car: starting (mode %d)", mode);
    farm_log(fe, msg);
    if (!open_buy_new_used_cars(fe)) return counter;

    TMResult bs = wait_buyandsell(fe, BS, 2, 0.75, 40000);
    if (!bs.found) { farm_log(fe, "remove_car: buyandsell not found"); return counter; }
    click1(fe, bs.cx, bs.cy);
    farm_sleep(fe, 1000);
    key(fe, VK_NEXT, 150);
    farm_sleep(fe, 1000);

    /* Enter "My Vehicles" */
    key(fe, VK_RETURN, 80);
    farm_sleep(fe, 2000);

    /* Favorite one car (Y) to protect it, then drive it */
    key(fe, 'Y', 80);     farm_sleep(fe, 1000);
    key(fe, VK_RETURN, 80); farm_sleep(fe, 800);
    key(fe, VK_ESCAPE, 80); farm_sleep(fe, 1500);
    key(fe, VK_RETURN, 80); farm_sleep(fe, 800);
    away(fe);              farm_sleep(fe, 200);

    TMResult rc = wait_best(fe, "rc.png", "full", 0.65, 5000, 200);
    if (rc.found) {
        farm_log(fe, "remove_car: get-on-car");
        click1(fe, rc.cx, rc.cy);
        farm_sleep(fe, 2000);
    } else {
        farm_log(fe, "remove_car: already driving / rc not found, ESC x2");
        key(fe, VK_ESCAPE, 80); farm_sleep(fe, 1500);
        key(fe, VK_ESCAPE, 80);
    }
    farm_sleep(fe, 2000);

    if (!reenter_my_vehicles(fe)) {
        farm_log(fe, "remove_car: could not re-enter my vehicles");
        return counter;
    }

    if (mode == 2) {
        /* ===== mode 2: remove most-recently-acquired ===== */
        /* Sort by "Most Recently Acquired": X -> 6x Down -> Enter */
        key(fe, 'X', 80);
        farm_sleep(fe, 500);
        away(fe);
        for (int i = 0; i < 6; i++) { key(fe, VK_DOWN, 80); farm_sleep(fe, 250); }
        farm_sleep(fe, 200);
        key(fe, VK_RETURN, 80);
        farm_sleep(fe, 1200);

        /* Back to first item */
        key(fe, VK_BACK, 80);
        farm_sleep(fe, 800);
        key(fe, VK_RETURN, 80);
        farm_sleep(fe, 1500);

        farm_log(fe, "remove_car: deletion loop (mode2, please confirm removals)");

        /* Delete loop: Enter -> 6x Down (remove from garage) -> Enter -> Down(yes) -> Enter */
        while (counter < target_count && !check_stop(fe)) {
            key(fe, VK_RETURN, 80);
            farm_sleep(fe, 1200);
            for (int i = 0; i < 6; i++) {
                if (check_stop(fe)) return counter;
                key(fe, VK_DOWN, 80);
                farm_sleep(fe, 200);
            }
            key(fe, VK_RETURN, 80);
            farm_sleep(fe, 500);
            key(fe, VK_DOWN, 80);
            farm_sleep(fe, 300);
            key(fe, VK_RETURN, 80);
            farm_sleep(fe, 1500);

            counter++;
            snprintf(msg, sizeof(msg), "remove_car: removed %d/%d", counter, target_count);
            farm_log(fe, msg);
        }
    } else {
        /* ===== mode 1: image-recognition removal (targets 22B) ===== */
        TMRegion full = TM_NamedRegion("full");

        /* Filter to "repairable/consumable" items: Y -> repitem -> ESC */
        key(fe, 'Y', 80);
        farm_sleep(fe, 1000);
        TMResult rep = wait_best(fe, "repitem.png", "center", 0.70, 3000, 300);
        if (!rep.found) { farm_log(fe, "remove_car: repitem not found"); return counter; }
        click1(fe, rep.cx, rep.cy);
        farm_sleep(fe, 800);
        key(fe, VK_ESCAPE, 80);
        farm_sleep(fe, 1000);

        /* Switch to the consumable brand (Subaru): backspace, find CCbrand (up to 5x up) */
        key(fe, VK_BACK, 80);
        TMResult brand = {0};
        for (int a = 0; a < 5 && !check_stop(fe); a++) {
            brand = wait_best(fe, "CCbrand.png", "full", 0.75, 800, 200);
            if (brand.found) break;
            key(fe, VK_UP, 80);
            farm_sleep(fe, 250);
        }
        if (!brand.found) { farm_log(fe, "remove_car: brand not found"); return counter; }
        click1(fe, brand.cx, brand.cy);
        farm_sleep(fe, 800);

        farm_log(fe, "remove_car: deletion loop (mode1, please confirm removals)");

        BOOL detail_confirmed = FALSE;
        int not_found_pages = 0;
        while (counter < target_count && !check_stop(fe)) {
            grab(fe);
            /* Ultimate-safe: main=removecarobject(22B), anti=newcartag(reject NEW).
             * The template is cropped to thumbnail + "B 600" PI bar (no car-name
             * text), and the threshold is high (0.85) so a painted/tuned racing
             * 22B (different paint + different PI) scores too low to be deleted;
             * only stock consumable 22Bs (stock paint, B 600) clear the bar. */
            TMResult car = TM_FindUltimateSafe(tmpl(fe, "removecarobject.png"),
                                               tmpl2(fe, "newcartag.png"),
                                               0.85, 0.65, TRUE, full);
            if (!car.found && !detail_confirmed) {
                /* toggle detail state with P, retry once */
                farm_log(fe, "remove_car: target not found, pressing P");
                key(fe, 'P', 80);
                farm_sleep(fe, 600);
                grab(fe);
                car = TM_FindUltimateSafe(tmpl(fe, "removecarobject.png"),
                                          tmpl2(fe, "newcartag.png"),
                                          0.85, 0.65, TRUE, full);
            }
            if (car.found) detail_confirmed = TRUE;

            if (!car.found) {
                snprintf(msg, sizeof(msg),
                         "remove_car: no removable stock 22B on page (best=%.2f, need>=0.85)",
                         car.score);
                farm_log(fe, msg);
                FARM_SNAP(fe, "remove_reject", car);
                not_found_pages++;
                if (not_found_pages >= 5) {
                    farm_log(fe, "remove_car: 5 pages without target, list cleared");
                    break;
                }
                /* Next page: 4x right */
                for (int k = 0; k < 4; k++) { key(fe, VK_RIGHT, 60); farm_sleep(fe, 100); }
                farm_sleep(fe, 400);
                continue;
            }
            not_found_pages = 0;

            snprintf(msg, sizeof(msg), "remove_car: target locked (%d,%d) %.2f",
                     car.cx, car.cy, car.score);
            farm_log(fe, msg);
            FARM_SNAP(fe, "remove_target", car);
            click1(fe, car.cx, car.cy);
            farm_sleep(fe, 800);

            /* Find "remove from garage" button (center). */
            TMResult rm = wait_best(fe, "removecar.png", "center", 0.70, 1500, 300);
            if (!rm.found) {
                /* not directly visible: Enter to open the menu, retry */
                key(fe, VK_RETURN, 80);
                farm_sleep(fe, 800);
                rm = wait_best(fe, "removecar.png", "center", 0.75, 1500, 300);
            }
            if (!rm.found) {
                /* wrong card / not removable: ESC and move right to avoid a loop */
                farm_log(fe, "remove_car: removecar button not found, skipping card");
                key(fe, VK_ESCAPE, 80);
                farm_sleep(fe, 1000);
                key(fe, VK_RIGHT, 80);
                farm_sleep(fe, 1200);
                continue;
            }
            click1(fe, rm.cx, rm.cy);
            farm_sleep(fe, 800);

            /* Confirm removal: Down (select "Yes") -> Enter */
            key(fe, VK_DOWN, 80);
            farm_sleep(fe, 300);
            GameInput_Press(fe->input, VK_RETURN, 80);
            farm_sleep(fe, 1200);

            counter++;
            snprintf(msg, sizeof(msg), "remove_car: removed %d/%d", counter, target_count);
            farm_log(fe, msg);
        }
    }

    /* Exit */
    for (int i = 0; i < 3; i++) {
        GameInput_Press(fe->input, VK_ESCAPE, 80);
        farm_sleep(fe, 1000);
    }
    return counter;
}

/* Resolution-independent detection of the pre-race "开始竞赛赛事" (Start Event)
 * menu via OCR. Windows OCR normalizes text scale, so this works at any window
 * resolution without a per-resolution template. Returns:
 *   1  = the start option is on screen (press Enter to start)
 *   0  = not found this frame
 *  -1  = OCR unavailable (caller should rely on the template fallback)
 * Matches both Chinese (开始/竞赛/赛事) and English (START) UI text. */
static int start_menu_ocr(void) {
    if (!OcrEngine_IsReady()) return -1;

    CaptureFrame f = {0};
    BOOL got = FALSE;
    for (int i = 0; i < 2; i++) {
        if (ScreenCapture_GrabFrame(&f)) got = TRUE;
        Sleep(30);
    }
    if (!got || !f.pixels) return -1;

    /* Left ~half, lower ~two-thirds: where the race menu lives, while skipping
     * the car-name header at the very top. */
    RECT roi;
    roi.left   = 0;
    roi.top    = (f.height * 35) / 100;
    roi.right  = (f.width * 55) / 100;
    roi.bottom = f.height;

    /* OcrResult is ~2 MB; keep it off the (~1 MB) pipeline-thread stack. The
     * pipeline is single-threaded, so static storage is safe here (mirrors
     * farm_economy.c). */
    static OcrResult r;
    memset(&r, 0, sizeof(r));
    if (!OcrEngine_RecognizeRegion(f.pixels, f.width, f.height, f.stride, roi, &r))
        return -1;

    /* Windows OCR inserts spaces between CJK glyphs ("开 始 竞 赛 赛 事") and may
     * drop/misread one of them, so strip ALL whitespace first, then look for
     * distinctive substrings. Without this the wcsstr checks never matched and
     * OCR detection was effectively dead (ocr=0 even on a clear start menu). */
    static WCHAR packed[OCR_MAX_TEXT_LEN * 8];
    int pn = 0;
    for (const WCHAR *p = r.all_text;
         *p && pn < (int)(sizeof(packed) / sizeof(packed[0])) - 1; p++) {
        if (*p != L' ' && *p != L'\t' && *p != L'\r' && *p != L'\n')
            packed[pn++] = *p;
    }
    packed[pn] = 0;

    if (wcsstr(packed, L"开始") || wcsstr(packed, L"竞赛") ||
        wcsstr(packed, L"赛事"))
        return 1;

    /* Case-insensitive "START" for English UIs. */
    for (const WCHAR *p = packed; p[0] && p[1] && p[2] && p[3] && p[4]; p++) {
        if ((p[0] == L'S' || p[0] == L's') && (p[1] == L'T' || p[1] == L't') &&
            (p[2] == L'A' || p[2] == L'a') && (p[3] == L'R' || p[3] == L'r') &&
            (p[4] == L'T' || p[4] == L't'))
            return 1;
    }
    return 0;
}

/* The EventLab "rate this blueprint" popup (点赞作者 / 点踩作者 / 赞标签 /
 * 取消) hijacks all menu input and IGNORES Esc - it can only be cleared by
 * clicking one of its buttons. It typically pops after crossing the finish
 * line or when restarting, which is exactly when the race loop is either
 * waiting for the start menu or about to drive, so an undetected popup hangs
 * the whole flow (even the 200s timeout recovery uses Esc and can't escape).
 *
 * Dismiss it by CLICKING the 点赞 (like) button - harmless, and it closes the
 * popup so the flow continues. Returns TRUE if a popup was found & dismissed. */
static BOOL dismiss_social_popup(FarmEngine *fe) {
    /* Detect the rating popup by OCR, NOT template matching: the 点赞/点踩
     * templates are tiny 2-glyph labels that false-match all over the race HUD
     * (multi-scale gray match), which spammed clicks mid-race and kicked the
     * flow back to free-roam. The real popup is the ONLY place both "点赞" and
     * "点踩" text appear together, so require both, then click the 点赞 glyph
     * the OCR located (resolution-independent, no template). */
    if (!OcrEngine_IsReady()) return FALSE;

    CaptureFrame f = {0};
    BOOL got = FALSE;
    for (int i = 0; i < 2; i++) { if (ScreenCapture_GrabFrame(&f)) got = TRUE; Sleep(30); }
    if (!got || !f.pixels) return FALSE;

    RECT roi = { 0, 0, f.width, f.height };
    static OcrResult r;
    memset(&r, 0, sizeof(r));
    if (!OcrEngine_RecognizeRegion(f.pixels, f.width, f.height, f.stride, roi, &r))
        return FALSE;

    /* Whitespace-stripped全文 confirms the popup (Windows OCR spaces CJK out). */
    static WCHAR packed[OCR_MAX_TEXT_LEN * 8];
    int pn = 0;
    for (const WCHAR *p = r.all_text;
         *p && pn < (int)(sizeof(packed) / sizeof(packed[0])) - 1; p++) {
        if (*p != L' ' && *p != L'\t' && *p != L'\r' && *p != L'\n')
            packed[pn++] = *p;
    }
    packed[pn] = 0;
    if (!wcsstr(packed, L"点赞") || !wcsstr(packed, L"点踩"))
        return FALSE;   /* not the rating popup */

    /* Locate a word carrying the 赞 glyph to click the 点赞 button. */
    int cx = -1, cy = -1;
    for (int i = 0; i < r.line_count && cx < 0; i++) {
        const OcrLine *line = &r.lines[i];
        for (int j = 0; j < line->word_count; j++) {
            const OcrWord *w = &line->words[j];
            if (wcschr(w->text, L'赞')) {
                cx = (w->bounds.left + w->bounds.right) / 2;
                cy = (w->bounds.top + w->bounds.bottom) / 2;
                break;
            }
        }
    }
    if (cx < 0) { GameInput_Press(fe->input, VK_RETURN, 80); }
    else        { GameInput_Click(fe->input, cx, cy); }
    farm_log(fe, "race: like popup dismissed (OCR 点赞)");
    farm_sleep(fe, 1200);
    return TRUE;
}

/* Copy `src` into `dst` with all whitespace removed and ASCII upper->lower,
 * for case-insensitive substring matching. */
static void pack_lower(const WCHAR *src, WCHAR *dst, int dstcap) {
    int n = 0;
    if (!src) { dst[0] = 0; return; }
    for (const WCHAR *p = src; *p && n < dstcap - 1; p++) {
        WCHAR c = *p;
        if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n') continue;
        if (c >= L'A' && c <= L'Z') c = (WCHAR)(c - L'A' + L'a');
        dst[n++] = c;
    }
    dst[n] = 0;
}

/* OCR the cars currently visible and click the card matching the target.
 * When want_pi > 0 the PI is MANDATORY: only a line containing that exact
 * number is eligible, so we lock onto the one tuned/favorited car (PI 834)
 * and never a freshly-bought stock 22B. The car name adds bonus score.
 * Returns TRUE (and clicks) on a match. */
static BOOL ocr_pick_car_on_page(FarmEngine *fe, const WCHAR *want, int want_pi,
                                 const WCHAR *pi_str) {
    CaptureFrame f = {0};
    BOOL got = FALSE;
    for (int i = 0; i < 2; i++) { if (ScreenCapture_GrabFrame(&f)) got = TRUE; Sleep(30); }
    if (!got || !f.pixels) return FALSE;
    /* Keep the TM frame in sync so debug snapshots show what OCR saw. */
    TM_SetFrame(f.pixels, f.width, f.height, f.stride);

    RECT roi = { 0, 0, f.width, f.height };
    static OcrResult r;
    memset(&r, 0, sizeof(r));
    if (!OcrEngine_RecognizeRegion(f.pixels, f.width, f.height, f.stride, roi, &r))
        return FALSE;

    int best_score = 0, best_cx = 0, best_cy = 0;
    for (int i = 0; i < r.line_count; i++) {
        const OcrLine *line = &r.lines[i];
        if (line->word_count <= 0) continue;
        WCHAR lpack[OCR_MAX_TEXT_LEN];
        pack_lower(line->full_text, lpack, OCR_MAX_TEXT_LEN);

        BOOL has_pi = (want_pi > 0 && wcsstr(lpack, pi_str) != NULL);
        if (want_pi > 0 && !has_pi) continue;   /* PI mandatory -> skip */

        int score = has_pi ? 3 : 0;
        const WCHAR *p = want ? want : L"";
        while (*p) {
            while (*p == L' ') p++;
            WCHAR tok[64]; int tn = 0;
            while (*p && *p != L' ' && tn < 63) tok[tn++] = *p++;
            tok[tn] = 0;
            if (tn >= 2) {
                WCHAR tl[64]; pack_lower(tok, tl, 64);
                if (tl[0] && wcsstr(lpack, tl)) score += 2;
            }
        }
        if (score <= 0) continue;          /* need PI (when set) or a name hit */
        if (score <= best_score) continue;

        RECT b = line->words[0].bounds;
        for (int j = 1; j < line->word_count; j++) {
            RECT wb = line->words[j].bounds;
            if (wb.left < b.left) b.left = wb.left;
            if (wb.top < b.top) b.top = wb.top;
            if (wb.right > b.right) b.right = wb.right;
            if (wb.bottom > b.bottom) b.bottom = wb.bottom;
        }
        best_score = score;
        best_cx = (b.left + b.right) / 2;
        best_cy = (b.top + b.bottom) / 2;
    }

    if (best_score <= 0) return FALSE;
    {
        char msg[96];
        snprintf(msg, sizeof(msg),
                 "race: select car via OCR (%d,%d) score=%d", best_cx, best_cy, best_score);
        farm_log(fe, msg);
    }
    GameInput_Click(fe->input, best_cx, best_cy);
    farm_sleep(fe, 600);
    return TRUE;
}

/* Select the farming car on the race car-select screen, resolution-independent.
 *
 * The wrong-car bug: remove_car favorites+drives a protected car (e.g. an
 * AE86), which becomes the "current car", so the target 22B is no longer on the
 * default page. We therefore: (0) try the current page, (1) jump to the
 * manufacturer view (Backspace, the "前往制造商" hotkey) and pick Subaru
 * (reusing the proven CCbrand tile nav from remove_car), then (2) OCR the
 * Subaru cars - paging down - for the card whose PI == car_pi (the one tuned
 * car), clicking it. Returns TRUE if the target was found and clicked. */
static BOOL select_car_by_ocr(FarmEngine *fe) {
    if (!OcrEngine_IsReady()) return FALSE;
    const WCHAR *want = fe->cfg.car_name;
    int want_pi = fe->cfg.car_pi;
    if ((!want || !want[0]) && want_pi <= 0) return FALSE;

    WCHAR pi_str[16];
    _snwprintf(pi_str, 16, L"%d", want_pi > 0 ? want_pi : 0);

    /* 0) Maybe the target is already on the current page. */
    if (ocr_pick_car_on_page(fe, want, want_pi, pi_str)) return TRUE;

    /* 1) Go to the manufacturer view and select Subaru. */
    farm_log(fe, "race: target car not on page, opening manufacturer (Backspace)");
    key(fe, VK_BACK, 80);
    farm_sleep(fe, 1200);
    TMResult brand = {0};
    for (int a = 0; a < 6 && !check_stop(fe); a++) {
        brand = wait_best(fe, "CCbrand.png", "full", 0.72, 800, 200);
        if (brand.found) break;
        key(fe, VK_UP, 80);
        farm_sleep(fe, 300);
    }
    if (brand.found) {
        farm_log(fe, "race: select Subaru brand (CCbrand)");
        click1(fe, brand.cx, brand.cy);
        farm_sleep(fe, 1200);
    } else {
        farm_log(fe, "race: Subaru brand tile not found (CCbrand), OCR on current view");
    }

    /* 2) OCR the brand's cars, paging down to find the PI-matched car. */
    for (int pg = 0; pg < 5 && !check_stop(fe); pg++) {
        if (ocr_pick_car_on_page(fe, want, want_pi, pi_str)) return TRUE;
        FARM_SNAP(fe, "car_ocr_page", brand);
        key(fe, VK_NEXT, 80);   /* page down within the car grid */
        farm_sleep(fe, 900);
    }
    farm_log(fe, "race: OCR car-select found no PI/name match after paging");
    return FALSE;
}

int Farm_Race(FarmEngine *fe, const char *share_code, int target_count) {
    if (!fe || check_stop(fe) || !share_code) return 0;
    int counter = 0;

    farm_log(fe, "race: starting");
    if (!Farm_EnterMenu(fe)) return counter;

    /* Normalize to the leftmost (剧情) tab first. The economy read leaves us on
     * the 车辆 tab, so a blind 4x PageDown overshot the Creative Hub into 商店.
     * From 剧情 we page right and stop as soon as the EventLab tile appears. */
    if (!Farm_GotoStoryTab(fe))
        farm_log(fe, "race: could not normalize to 剧情 tab, paging anyway");

    TMResult el = {0};
    for (int pg = 0; pg < 8 && !check_stop(fe); pg++) {
        el = find_best(fe, "eventlab.png", "full");
        char m[96];
        snprintf(m, sizeof(m), "race: scan eventlab pg=%d score=%.2f", pg, el.score);
        farm_log(fe, m);
        FARM_SNAP(fe, "eventlab", el);
        if (el.score >= 0.65) { el.found = TRUE; break; }
        el.found = FALSE;
        GameInput_Press(fe->input, VK_NEXT, 120);  /* next tab */
        farm_sleep(fe, 900);
    }
    if (!el.found) { farm_log(fe, "race: eventlab not found (overshoot fix)"); return counter; }
    {
        char m[96];
        snprintf(m, sizeof(m), "race: click eventlab (%d,%d) %.2f", el.cx, el.cy, el.score);
        farm_log(fe, m);
    }
    GameInput_Click(fe->input, el.cx, el.cy);
    farm_sleep(fe, 2500);  /* EventLab main page loads community data slowly */

    /* On the EventLab main page, click the 游玩赛事 (Play Event) tile.
     * NEVER press Enter as a fallback here: the default-highlighted option is
     * 创建赛事 (Create Event), so a blind Enter opens the event editor. Instead
     * poll for the play-event icon (logging the best score each pass so a stall
     * is diagnosable), and abort cleanly if it never appears. */
    TMResult pe = {0};
    {
        DWORD t0 = GetTickCount();
        for (int i = 0; !check_stop(fe); i++) {
            pe = find_best(fe, "playenent.png", "full");
            char m[96];
            snprintf(m, sizeof(m), "race: scan play-event #%d score=%.2f", i, pe.score);
            farm_log(fe, m);
            FARM_SNAP(fe, "playevent", pe);
            if (pe.score >= 0.62) { pe.found = TRUE; break; }
            pe.found = FALSE;
            if ((int)(GetTickCount() - t0) >= 25000) break;
            farm_sleep(fe, 800);
        }
    }
    if (!pe.found) {
        farm_log(fe, "race: play-event icon not found (no Enter fallback - would create event)");
        return counter;
    }
    {
        char m[96];
        snprintf(m, sizeof(m), "race: click play-event (%d,%d) %.2f", pe.cx, pe.cy, pe.score);
        farm_log(fe, m);
    }
    GameInput_Click(fe->input, pe.cx, pe.cy);
    farm_sleep(fe, 1500);

    /* Open the share-code entry popup. */
    GameInput_Press(fe->input, VK_BACK, 80);
    farm_sleep(fe, 800);
    GameInput_Press(fe->input, VK_UP, 80);
    farm_sleep(fe, 400);
    GameInput_Press(fe->input, VK_RETURN, 80);
    farm_sleep(fe, 1200);

    /* Fill + confirm the share code via UI Automation. The popup is a separate
     * UWP process, so PostMessage to the game window can't reach it; UIA sets
     * the edit value and invokes 确定 cross-process. Fall back to typing
     * (rarely works for this popup) only if UIA fails. */
    if (XboxTextEntry_SubmitShareCode(share_code, 8000)) {
        farm_log(fe, "race: share code submitted via UIA");
    } else {
        farm_log(fe, "race: UIA submit failed, falling back to typing");
        for (const char *p = share_code; *p; p++) {
            if (check_stop(fe)) return counter;
            if (*p >= '0' && *p <= '9') {
                GameInput_Press(fe->input, (DWORD)*p, 50);
                farm_sleep(fe, 50);
            }
        }
        farm_sleep(fe, 400);
        GameInput_Press(fe->input, VK_RETURN, 80);
    }
    farm_sleep(fe, 1200);
    GameInput_Press(fe->input, VK_DOWN, 80);
    farm_sleep(fe, 300);
    GameInput_Press(fe->input, VK_RETURN, 80);
    farm_sleep(fe, 1500);

    /* Wait for VEI (event info loaded) */
    TMResult vei = wait_template(fe, "VEI.png", 0.70, 20000, 1000);
    if (!vei.found) { farm_log(fe, "race: VEI timeout"); return counter; }
    GameInput_Press(fe->input, VK_RETURN, 80);
    farm_sleep(fe, 2000);
    GameInput_Press(fe->input, VK_RETURN, 80);
    farm_sleep(fe, 2000);

    /* Select the farming car. Prefer OCR (name + PI from the profile): it is
     * resolution-independent and always picks the RIGHT car, so a previous
     * remove_car that left a favorited car as "current" no longer leaks the
     * wrong car into the race. Fall back to the legacy skillcar.png template
     * only if OCR is unavailable or finds no match. */
    farm_sleep(fe, 800);  /* let the car grid settle before OCR */
    if (!select_car_by_ocr(fe)) {
        /* Fallback only on a STRONG skillcar match (>=0.85): a weak match grabs
         * the wrong current car (e.g. the AE86 matched skillcar at 0.72), which
         * is exactly the bug we're fixing - better to leave selection untouched
         * than to actively pick the wrong car. */
        TMResult car = wait_template(fe, "skillcar.png", 0.85, 4000, 300);
        FARM_SNAP(fe, "car_select", car);
        if (car.found) {
            farm_log(fe, "race: select car via skillcar.png template (fallback)");
            GameInput_Click(fe->input, car.cx, car.cy);
            farm_sleep(fe, 500);
        } else {
            farm_log(fe, "race: car-select fell through (no strong match), using current car");
        }
    }
    GameInput_Press(fe->input, VK_RETURN, 80);
    farm_sleep(fe, 4000);

    farm_log(fe, "race: entering race loop");

    /* Race loop */
    while (counter < target_count && !check_stop(fe)) {
        char msg[64];
        snprintf(msg, sizeof(msg), "race: starting race %d/%d", counter+1, target_count);
        farm_log(fe, msg);

        /* Confirm the "开始竞赛赛事" (Start Event) button with ENTER.
         *
         * CRITICAL: do NOT press Down/Up here. That banner is the default-
         * highlighted button, but on a detection miss (a loading/transition
         * frame) the old code pressed VK_DOWN, which moved the *keyboard* cursor
         * onto "难度与设置" (Difficulty & Settings). The subsequent confirm is a
         * keyboard Enter (acting on the keyboard-selected item, not the moused-
         * over one in the background), so it opened Settings instead of starting
         * the race. On a miss we just wait and re-grab; the cursor never moves.
         * After Enter we verify the banner is gone before driving, so W is never
         * held while still sitting in a menu. */
        /* Detect the "开始竞赛赛事" (Start Event) menu, then press Enter ONCE and
         * go drive. Two detectors, accept either:
         *   1) OCR (primary, resolution-independent) - no per-resolution template.
         *   2) template match (fallback, also covers non-Chinese/English UIs).
         * 开始竞赛赛事 is the default-highlighted option, so we NEVER move the
         * cursor here (moving it landed Enter on Settings). We also do NOT
         * re-verify by re-matching (that caused an endless "retrying" loop even
         * after the race had started); if Enter misses, the 200s timeout below
         * recovers. */
        /* The start menu FADES IN: its match score climbs to ~0.9 only once it
         * is fully rendered with the START banner highlighted. Empirically:
         *   - settled menu          -> ~0.85-0.92
         *   - fade-in / transition  -> ~0.62-0.70
         *   - in-race false positive-> ~0.63-0.67
         * A hover+Enter only registers on the SETTLED menu, so we must wait for
         * a strong score before pressing. That also gives a clean "started"
         * test: once racing, the score collapses well below the press bar, so a
         * single low read after the press means the race began (no more relying
         * on flaky OCR or the ambiguous 0.55 band that kept us re-pressing).
         *
         * PRESS_TH: only press when the menu is clearly settled.
         * GONE_TH : below this after a press => menu dismissed, race underway. */
        const double PRESS_TH = 0.80;
        const double GONE_TH  = 0.72;

        /* FIRST race: we land on the "开始竞赛赛事" menu and MUST confirm it.
         * RESTART rounds (counter>0): the end-of-race "Restart" (X+Enter) already
         * relaunched the same event straight into the countdown - the menu does
         * NOT reappear - so we only do a SHORT scan; if no settled menu shows up
         * we are already racing and go straight to driving (no 20s waste). */
        BOOL is_restart = (counter > 0);
        int max_iters = is_restart ? 6 : 120;  /* ~restart: a few s; first: ~48s */

        BOOL found_start = FALSE;
        int confirm_attempts = 0;
        int settle_waits = 0;
        for (int i = 0; i < max_iters && !check_stop(fe); i++) {
            int ocr = start_menu_ocr();
            TMResult sw = find_best(fe, "startw.png", "left");
            TMResult sb = find_best(fe, "start.png", "left");
            TMResult best = sw.score >= sb.score ? sw : sb;
            double tscore = best.score;

            /* Fallback for odd resolutions where the settled menu never reaches
             * 0.80: after ~8s of lingering moderate matches, accept >=0.62. */
            double press_bar = (settle_waits >= 20) ? 0.62 : PRESS_TH;

            if (ocr != 1 && tscore < press_bar) {
                if (tscore >= 0.55) settle_waits++;  /* present but not settled yet */
                /* The start menu may be blocked by the like-blueprint popup
                 * (Esc can't clear it). Probe & dismiss every ~2s while we wait
                 * so the menu can finally render instead of hanging forever. */
                if ((i % 5) == 0 && dismiss_social_popup(fe))
                    continue;  /* re-scan immediately after clearing the popup */
                if ((i % 5) == 0) {
                    snprintf(msg, sizeof(msg),
                             "race: waiting for start menu (ocr=%d t=%.2f)", ocr, tscore);
                    farm_log(fe, msg);
                    FARM_SNAP(fe, "start_wait", best);
                }
                farm_sleep(fe, 400);  /* loading; wait, never move the cursor */
                continue;
            }

            /* Settled menu. Confirm by HOVERING the START button then Enter
             * (the verified "golden combo"). A bare keyboard Enter without a
             * preceding WM_MOUSEMOVE often does NOT register on this menu - the
             * background game activates the mouse-hovered item, not the
             * keyboard-selected one. */
            found_start = TRUE;
            confirm_attempts++;
            if (best.found && tscore >= 0.50) {
                GameInput_Click(fe->input, best.cx, best.cy);
            } else {
                GameInput_Press(fe->input, VK_RETURN, 80);
            }
            snprintf(msg, sizeof(msg),
                     "race: start menu (ocr=%d t=%.2f) confirm #%d",
                     ocr, tscore, confirm_attempts);
            farm_log(fe, msg);
            FARM_SNAP(fe, "start_confirm", best);
            farm_sleep(fe, 1500);

            /* Verify dismissed: the menu score collapses once racing begins. */
            TMResult sw2 = find_best(fe, "startw.png", "left");
            TMResult sb2 = find_best(fe, "start.png", "left");
            TMResult best2 = sw2.score >= sb2.score ? sw2 : sb2;
            double tscore2 = best2.score;
            FARM_SNAP(fe, "start_verify", best2);
            if (tscore2 < GONE_TH) {
                snprintf(msg, sizeof(msg),
                         "race: start confirmed (t=%.2f), race underway", tscore2);
                farm_log(fe, msg);
                break;
            }
            if (confirm_attempts >= 5) {
                farm_log(fe, "race: start confirm exhausted; driving anyway (timeout recovers)");
                break;
            }
            settle_waits = 0;  /* still up - re-confirm from a settled state */
        }
        if (!found_start) {
            if (is_restart) {
                /* Normal: restart went straight into the race countdown. */
                farm_log(fe, "race: restart -> already racing (no start menu)");
            } else {
                farm_log(fe, "race: start menu not found");
                return counter;
            }
        }
        farm_sleep(fe, 4000);  /* let the race countdown begin before driving */

        /* Hold W + Up (accelerate) */
        GameInput_KeyDown(fe->input, 'W');
        GameInput_KeyDown(fe->input, VK_UP);

        /* Wait for finish: detect restart.png or 200s timeout */
        DWORD race_start = GetTickCount();
        BOOL finished = FALSE;
        BOOL timeout = FALSE;
        while (!check_stop(fe)) {
            DWORD elapsed = GetTickCount() - race_start;
            if (elapsed > 200000) { timeout = TRUE; break; }

            /* NOTE: no popup check while driving - the rating popup only appears
             * AFTER the finish line, and probing it here false-matched the HUD.
             * It is handled at the finish and during the next start-menu wait. */

            /* Check for restart (finish) every ~1s */
            if (elapsed % 1000 < 400) {
                grab(fe);
                TMResult rst = TM_FindImageGray(tmpl(fe, "restart.png"), 0.70, FALSE);
                if (rst.found) { finished = TRUE; break; }
            }
            farm_sleep(fe, 300);
        }

        /* Release driving keys */
        GameInput_KeyUp(fe->input, 'W');
        GameInput_KeyUp(fe->input, VK_UP);

        if (check_stop(fe)) return counter;

        if (timeout) {
            farm_log(fe, "race: timeout! restarting");
            GameInput_Press(fe->input, VK_ESCAPE, 80);
            farm_sleep(fe, 1500);
            grab(fe);
            TMResult ra = TM_FindImageGray(tmpl(fe, "restarta.png"), 0.65, FALSE);
            if (ra.found) {
                GameInput_Click(fe->input, ra.cx, ra.cy);
                farm_sleep(fe, 1000);
                GameInput_Press(fe->input, VK_RETURN, 80);
            }
            farm_sleep(fe, 4000);
            continue; /* don't count this race */
        }

        if (!finished) return counter;

        /* The like-blueprint popup most often fires right at the finish line,
         * before the restart menu - clear it first so X/Enter hit the menu. */
        dismiss_social_popup(fe);

        /* Finish: restart race */
        if (counter < target_count - 1) {
            GameInput_Press(fe->input, 'X', 80); /* restart option */
            farm_sleep(fe, 800);
            GameInput_Press(fe->input, VK_RETURN, 80);
        } else {
            GameInput_Press(fe->input, VK_RETURN, 80);
        }
        farm_sleep(fe, 2000);
        /* ...and it can also pop *after* confirming restart. */
        dismiss_social_popup(fe);

        counter++;
        snprintf(msg, sizeof(msg), "race: completed %d/%d", counter, target_count);
        farm_log(fe, msg);
    }

    return counter;
}
