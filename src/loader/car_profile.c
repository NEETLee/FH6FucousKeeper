/*
 * car_profile.c - Per-car farm profile loader (see car_profile.h)
 */

#include "car_profile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ─── Defaults (Subaru 22B STI) ──────────────────────────────────────── */
#define DEF_COST_CR   81700L
#define DEF_COST_SP   30L

static void fill_default_22b(CarProfile *p, const char *dir) {
    memset(p, 0, sizeof(*p));
    wcsncpy(p->name, L"Subaru 22B STI", 63);
    strncpy(p->id, "22B", sizeof(p->id) - 1);
    if (dir) strncpy(p->dir, dir, sizeof(p->dir) - 1);
    p->cost_cr = DEF_COST_CR;
    p->cost_sp = DEF_COST_SP;
    p->skill_dirs[0] = VK_RIGHT;
    p->skill_dirs[1] = VK_UP;
    p->skill_dirs[2] = VK_UP;
    p->skill_dirs[3] = VK_UP;
    p->skill_dirs[4] = VK_LEFT;
    p->skill_count = 5;
}

/* ─── Cars root resolution: {exe}/profiles/cars ──────────────────────── */
const char *CarProfile_GetCarsDir(void) {
    static char path[260];
    WCHAR exe[260];
    if (GetModuleFileNameW(NULL, exe, 260)) {
        WCHAR *slash = wcsrchr(exe, L'\\');
        if (slash) *slash = 0;
        WCHAR wpath[260];
        _snwprintf(wpath, 260, L"%s\\profiles\\cars", exe);
        WideCharToMultiByte(CP_UTF8, 0, wpath, -1, path, sizeof(path), NULL, NULL);
        return path;
    }
    strncpy(path, "profiles/cars", sizeof(path) - 1);
    return path;
}

/* ─── Skill-tree spec parser ─────────────────────────────────────────── */
int CarProfile_ParseDirs(const char *spec, DWORD *out, int max) {
    if (!spec || !out || max <= 0) return 0;
    int n = 0;
    char buf[256];
    strncpy(buf, spec, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;

    char *ctx = NULL;
    for (char *tok = strtok_r(buf, ", \t\r\n", &ctx);
         tok && n < max;
         tok = strtok_r(NULL, ", \t\r\n", &ctx)) {
        DWORD vk = 0;
        if      (_stricmp(tok, "RIGHT") == 0 || _stricmp(tok, "R") == 0) vk = VK_RIGHT;
        else if (_stricmp(tok, "LEFT")  == 0 || _stricmp(tok, "L") == 0) vk = VK_LEFT;
        else if (_stricmp(tok, "UP")    == 0 || _stricmp(tok, "U") == 0) vk = VK_UP;
        else if (_stricmp(tok, "DOWN")  == 0 || _stricmp(tok, "D") == 0) vk = VK_DOWN;
        else continue;
        out[n++] = vk;
    }
    return n;
}

/* ─── INI helpers (UTF-8 file -> simple line scan) ───────────────────── */
static BOOL read_ini_value(const char *path, const char *section,
                           const char *key, char *out, int out_sz) {
    FILE *f = fopen(path, "rb");
    if (!f) return FALSE;
    char line[512];
    BOOL in_section = (section == NULL);
    BOOL found = FALSE;
    size_t keylen = strlen(key);

    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == ';' || *p == '#' || *p == '\r' || *p == '\n' || *p == 0) continue;

        if (*p == '[') {
            char *end = strchr(p, ']');
            if (end) {
                *end = 0;
                in_section = section && (_stricmp(p + 1, section) == 0);
            }
            continue;
        }
        if (!in_section) continue;

        char *eq = strchr(p, '=');
        if (!eq) continue;
        char *kend = eq;
        while (kend > p && (kend[-1] == ' ' || kend[-1] == '\t')) kend--;
        if ((size_t)(kend - p) != keylen) continue;
        if (_strnicmp(p, key, keylen) != 0) continue;

        char *v = eq + 1;
        while (*v == ' ' || *v == '\t') v++;
        char *vend = v + strlen(v);
        while (vend > v && (vend[-1] == '\r' || vend[-1] == '\n' ||
                            vend[-1] == ' '  || vend[-1] == '\t')) vend--;
        *vend = 0;
        strncpy(out, v, out_sz - 1);
        out[out_sz - 1] = 0;
        found = TRUE;
        break;
    }
    fclose(f);
    return found;
}

/* ─── Load a single car folder ───────────────────────────────────────── */
BOOL CarProfile_Load(const char *dir, CarProfile *out) {
    if (!dir || !out) return FALSE;

    char ini[300];
    snprintf(ini, sizeof(ini), "%s/car.ini", dir);
    if (GetFileAttributesA(ini) == INVALID_FILE_ATTRIBUTES) return FALSE;

    memset(out, 0, sizeof(*out));
    strncpy(out->dir, dir, sizeof(out->dir) - 1);

    /* folder name -> id */
    const char *slash = strrchr(dir, '/');
    const char *bslash = strrchr(dir, '\\');
    const char *base = slash > bslash ? slash : bslash;
    strncpy(out->id, base ? base + 1 : dir, sizeof(out->id) - 1);

    char val[256];
    if (read_ini_value(ini, "Car", "Name", val, sizeof(val))) {
        MultiByteToWideChar(CP_UTF8, 0, val, -1, out->name, 63);
    } else {
        MultiByteToWideChar(CP_UTF8, 0, out->id, -1, out->name, 63);
    }

    out->cost_cr = read_ini_value(ini, "Car", "CostCR", val, sizeof(val))
                   ? atol(val) : DEF_COST_CR;
    out->cost_sp = read_ini_value(ini, "Car", "CostSP", val, sizeof(val))
                   ? atol(val) : DEF_COST_SP;
    if (out->cost_cr <= 0) out->cost_cr = DEF_COST_CR;
    if (out->cost_sp <= 0) out->cost_sp = DEF_COST_SP;

    if (read_ini_value(ini, "SkillTree", "Dirs", val, sizeof(val))) {
        out->skill_count = CarProfile_ParseDirs(val, out->skill_dirs,
                                                CAR_PROFILE_MAX_SKILL);
    }
    if (out->skill_count <= 0) {
        /* fallback to 22B path */
        out->skill_dirs[0] = VK_RIGHT; out->skill_dirs[1] = VK_UP;
        out->skill_dirs[2] = VK_UP;    out->skill_dirs[3] = VK_UP;
        out->skill_dirs[4] = VK_LEFT;  out->skill_count = 5;
    }
    return TRUE;
}

/* ─── Scan the cars root ─────────────────────────────────────────────── */
int CarProfile_Scan(const char *cars_root, CarProfile *out, int max) {
    if (!out || max <= 0) return 0;
    if (!cars_root) cars_root = CarProfile_GetCarsDir();

    int count = 0;
    char pattern[300];
    snprintf(pattern, sizeof(pattern), "%s\\*", cars_root);

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (fd.cFileName[0] == '.') continue;
            char dir[260];
            snprintf(dir, sizeof(dir), "%s/%s", cars_root, fd.cFileName);
            if (count < max && CarProfile_Load(dir, &out[count]))
                count++;
        } while (FindNextFileA(h, &fd) && count < max);
        FindClose(h);
    }

    if (count == 0) {
        char dir[260];
        snprintf(dir, sizeof(dir), "%s/22B", cars_root);
        fill_default_22b(&out[0], dir);
        count = 1;
    }
    return count;
}
