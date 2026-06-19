/*
 * car_profile.h - Per-car farm profiles (cost, SP cost, skill-tree path, assets)
 *
 * Each farm car lives in its own folder under {exe}/profiles/cars/<id>/:
 *   car.ini                  - [Car] Name/CostCR/CostSP, [SkillTree] Dirs
 *   <car-specific>.png       - templates that are unique to this car
 *                              (consumablecar, removecarobject, CCbrand, ...)
 *
 * Generic UI templates (collectionjournal, masterexplorer, ...) stay shared in
 * assets/templates; the farm engine looks in the car folder first and falls
 * back to the shared assets dir.
 */

#ifndef FOCUSKEEPER_CAR_PROFILE_H
#define FOCUSKEEPER_CAR_PROFILE_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CAR_PROFILE_MAX        16
#define CAR_PROFILE_MAX_SKILL  16

typedef struct {
    WCHAR name[64];                 /* display name from [Car] Name */
    char  id[64];                   /* folder name (ASCII) */
    char  dir[260];                 /* absolute path to the car folder */
    long  cost_cr;                  /* CR cost per car (default 81700) */
    long  cost_sp;                  /* SP cost per car (default 30) */
    DWORD skill_dirs[CAR_PROFILE_MAX_SKILL]; /* VK_* mastery-tree path */
    int   skill_count;
} CarProfile;

/* Resolve the cars root: {exe}/profiles/cars . Returns a static buffer. */
const char *CarProfile_GetCarsDir(void);

/* Scan the cars root, filling up to `max` profiles. Returns the count.
 * If none are found, writes a single built-in 22B fallback (skill path
 * right,up,up,up,left; cost 81700/30) so the pipeline still works. */
int  CarProfile_Scan(const char *cars_root, CarProfile *out, int max);

/* Load a single car folder (dir contains car.ini). Returns TRUE on success. */
BOOL CarProfile_Load(const char *dir, CarProfile *out);

/* Parse a skill-tree spec like "RIGHT,UP,UP,UP,LEFT" into VK codes.
 * Returns the number parsed (0 on empty/invalid). */
int  CarProfile_ParseDirs(const char *spec, DWORD *out, int max);

#ifdef __cplusplus
}
#endif

#endif /* FOCUSKEEPER_CAR_PROFILE_H */
