/*
 * race_profile.c - Race Profile INI Loading and Built-in Presets
 *
 * Profiles define the step sequence for auto-race loops.
 * Stored as INI files in the profiles/ subdirectory next to the exe.
 */

#include "race_profile.h"
#include <stdio.h>
#include <wchar.h>

static WCHAR s_profiles_dir[MAX_PATH] = {0};

/* ─── Internal Helpers ────────────────────────────────────────────── */

static void EnsureProfilesDir(void)
{
    if (s_profiles_dir[0] != L'\0') return;

    GetModuleFileNameW(NULL, s_profiles_dir, MAX_PATH);
    WCHAR *slash = wcsrchr(s_profiles_dir, L'\\');
    if (slash) {
        wcscpy(slash + 1, L"profiles\\");
    } else {
        wcscpy(s_profiles_dir, L"profiles\\");
    }

    CreateDirectoryW(s_profiles_dir, NULL);
}

static int ReadInt(const WCHAR *section, const WCHAR *key, int def, const WCHAR *path)
{
    return GetPrivateProfileIntW(section, key, def, path);
}

static void ReadStr(const WCHAR *section, const WCHAR *key,
                    WCHAR *buf, int buflen, const WCHAR *path)
{
    GetPrivateProfileStringW(section, key, L"", buf, buflen, path);
}

static void WriteInt(const WCHAR *section, const WCHAR *key, int val, const WCHAR *path)
{
    WCHAR buf[32];
    _snwprintf(buf, 32, L"%d", val);
    WritePrivateProfileStringW(section, key, buf, path);
}

static StepMode ParseMode(const WCHAR *mode_str)
{
    if (_wcsicmp(mode_str, L"hold") == 0) return STEP_MODE_HOLD;
    if (_wcsicmp(mode_str, L"tap") == 0) return STEP_MODE_TAP;
    if (_wcsicmp(mode_str, L"sequence") == 0) return STEP_MODE_SEQUENCE;
    if (_wcsicmp(mode_str, L"seq") == 0) return STEP_MODE_SEQUENCE;
    return STEP_MODE_WAIT;
}

static const WCHAR* ModeToStr(StepMode mode)
{
    switch (mode) {
    case STEP_MODE_HOLD:     return L"hold";
    case STEP_MODE_TAP:      return L"tap";
    case STEP_MODE_SEQUENCE: return L"sequence";
    default:                 return L"wait";
    }
}

/* ─── Public API ──────────────────────────────────────────────────── */

const WCHAR* Profile_GetDirectory(void)
{
    EnsureProfilesDir();
    return s_profiles_dir;
}

BOOL Profile_Load(const WCHAR *ini_path, RaceProfile *out)
{
    if (!ini_path || !out) return FALSE;

    if (GetFileAttributesW(ini_path) == INVALID_FILE_ATTRIBUTES)
        return FALSE;

    ZeroMemory(out, sizeof(RaceProfile));

    ReadStr(L"Profile", L"Name", out->name, PROFILE_NAME_LEN, ini_path);
    out->step_count = ReadInt(L"Profile", L"StepCount", 0, ini_path);

    if (out->step_count <= 0 || out->step_count > PROFILE_MAX_STEPS)
        return FALSE;

    /* Extract filename from path */
    const WCHAR *fname = wcsrchr(ini_path, L'\\');
    if (fname) fname++; else fname = ini_path;
    wcsncpy(out->filename, fname, PROFILE_NAME_LEN - 1);

    for (int i = 0; i < out->step_count; i++) {
        WCHAR section[32];
        _snwprintf(section, 32, L"Step%d", i);
        RaceStep *step = &out->steps[i];

        /* Mode (string-based for readability) */
        WCHAR mode_str[16] = {0};
        ReadStr(section, L"Mode", mode_str, 16, ini_path);
        step->mode = ParseMode(mode_str);

        /* Optional display name */
        ReadStr(section, L"Name", step->step_name, STEP_NAME_LEN, ini_path);

        /* Common fields */
        step->duration_ms = (DWORD)ReadInt(section, L"Duration", 5000, ini_path);
        step->delay_before_ms = (DWORD)ReadInt(section, L"DelayBefore", 0, ini_path);

        switch (step->mode) {
        case STEP_MODE_HOLD:
            step->key = (DWORD)ReadInt(section, L"Key", 'W', ini_path);
            break;

        case STEP_MODE_TAP:
            step->tap_key = (DWORD)ReadInt(section, L"Key", VK_RETURN, ini_path);
            step->tap_hold_ms = (DWORD)ReadInt(section, L"HoldMs", 80, ini_path);
            step->tap_interval_ms = (DWORD)ReadInt(section, L"Interval", 1500, ini_path);
            break;

        case STEP_MODE_SEQUENCE:
            step->action_count = ReadInt(section, L"ActionCount", 0, ini_path);
            step->loop_actions = (BOOL)ReadInt(section, L"Loop", 0, ini_path);
            if (step->action_count > PROFILE_MAX_ACTIONS)
                step->action_count = PROFILE_MAX_ACTIONS;

            for (int a = 0; a < step->action_count; a++) {
                WCHAR k[32];
                _snwprintf(k, 32, L"A%d_Key", a);
                step->actions[a].vk = (DWORD)ReadInt(section, k, 0, ini_path);
                _snwprintf(k, 32, L"A%d_Hold", a);
                step->actions[a].hold_ms = (DWORD)ReadInt(section, k, 0, ini_path);
                _snwprintf(k, 32, L"A%d_Delay", a);
                step->actions[a].delay_after_ms = (DWORD)ReadInt(section, k, 500, ini_path);
            }
            break;

        case STEP_MODE_WAIT:
        default:
            break;
        }
    }

    return TRUE;
}

BOOL Profile_Save(const WCHAR *ini_path, const RaceProfile *profile)
{
    if (!ini_path || !profile) return FALSE;

    DeleteFileW(ini_path);

    WritePrivateProfileStringW(L"Profile", L"Name", profile->name, ini_path);
    WriteInt(L"Profile", L"StepCount", profile->step_count, ini_path);

    for (int i = 0; i < profile->step_count; i++) {
        WCHAR section[32];
        _snwprintf(section, 32, L"Step%d", i);
        const RaceStep *step = &profile->steps[i];

        WritePrivateProfileStringW(section, L"Mode", ModeToStr(step->mode), ini_path);
        if (step->step_name[0])
            WritePrivateProfileStringW(section, L"Name", step->step_name, ini_path);
        WriteInt(section, L"Duration", (int)step->duration_ms, ini_path);
        if (step->delay_before_ms > 0)
            WriteInt(section, L"DelayBefore", (int)step->delay_before_ms, ini_path);

        switch (step->mode) {
        case STEP_MODE_HOLD:
            WriteInt(section, L"Key", (int)step->key, ini_path);
            break;

        case STEP_MODE_TAP:
            WriteInt(section, L"Key", (int)step->tap_key, ini_path);
            WriteInt(section, L"HoldMs", (int)step->tap_hold_ms, ini_path);
            WriteInt(section, L"Interval", (int)step->tap_interval_ms, ini_path);
            break;

        case STEP_MODE_SEQUENCE:
            WriteInt(section, L"ActionCount", step->action_count, ini_path);
            WriteInt(section, L"Loop", (int)step->loop_actions, ini_path);
            for (int a = 0; a < step->action_count; a++) {
                WCHAR k[32];
                _snwprintf(k, 32, L"A%d_Key", a);
                WriteInt(section, k, (int)step->actions[a].vk, ini_path);
                _snwprintf(k, 32, L"A%d_Hold", a);
                WriteInt(section, k, (int)step->actions[a].hold_ms, ini_path);
                _snwprintf(k, 32, L"A%d_Delay", a);
                WriteInt(section, k, (int)step->actions[a].delay_after_ms, ini_path);
            }
            break;

        default:
            break;
        }
    }

    return TRUE;
}

int Profile_Enumerate(WCHAR names[][PROFILE_NAME_LEN], int max_count)
{
    EnsureProfilesDir();

    WCHAR search_path[MAX_PATH];
    _snwprintf(search_path, MAX_PATH, L"%s*.ini", s_profiles_dir);

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(search_path, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return 0;

    int count = 0;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            if (count < max_count) {
                wcsncpy(names[count], fd.cFileName, PROFILE_NAME_LEN - 1);
                names[count][PROFILE_NAME_LEN - 1] = L'\0';
                count++;
            }
        }
    } while (FindNextFileW(hFind, &fd) && count < max_count);

    FindClose(hFind);
    return count;
}

/* Write the default profile with full Chinese documentation */
BOOL Profile_WriteDefaultTemplate(const WCHAR *path)
{
    FILE *f = _wfopen(path, L"w, ccs=UTF-16LE");
    if (!f) return FALSE;

    fwprintf(f,
        L"; ===================================================================\n"
        L"; FH6 FocusKeeper - \u81ea\u52a8\u8d5b\u4e8b\u914d\u7f6e\u6587\u4ef6\n"
        L"; ===================================================================\n"
        L";\n"
        L"; \u4f5c\u8005: 170516901\n"
        L"; \u89c6\u9891: \u3010\u5730\u5e73\u7ebf6 13\u79d210\u6280\u672f\u70b9\u3011\n"
        L";   https://www.bilibili.com/video/BV1PzV26gEwM/?share_source=copy_web&vd_source=7bf46fa0cbbf72f4c209cd0991698a2a\n"
        L";\n"
        L"; ===================================================================\n"
        L";\n"
        L"; Mode=xxx         \u6a21\u5f0f: wait(\u7b49\u5f85) / hold(\u6309\u4f4f) / tap(\u70b9\u6309) / sequence(\u5e8f\u5217)\n"
        L"; Name=xxx         \u6b65\u9aa4\u540d\u79f0(\u53ef\u9009\uff0c\u663e\u793a\u5728\u8fd0\u884c\u72b6\u6001\u4e2d)\n"
        L"; Duration=\u6beb\u79d2    \u8fd9\u4e00\u6b65\u7684\u603b\u6301\u7eed\u65f6\u95f4\n"
        L"; DelayBefore=\u6beb\u79d2 \u5f00\u59cb\u524d\u7b49\u5f85\u7684\u65f6\u95f4(\u53ef\u9009\uff0c\u9ed8\u8ba40)\n"
        L";\n"
        L"; Mode=hold        Key=\u952e\u7801\n"
        L"; Mode=tap         Key=\u952e\u7801, HoldMs=\u6309\u4f4f\u65f6\u957f, Interval=\u95f4\u9694\n"
        L"; Mode=sequence    ActionCount, A0_Key/Hold/Delay...\n"
        L";\n"
        L"; \u5e38\u7528\u952e\u7801: W=87 A=65 S=83 D=68 X=88 E=69 Enter=13 Esc=27 Space=32\n"
        L"; \u65f6\u95f4: 1\u79d2=1000  5\u79d2=5000  17\u79d2=17000\n"
        L";\n"
        L"; \u6d41\u7a0b: \u6309X(1\u79d2) -> \u56de\u8f66(10\u79d2) -> \u56de\u8f66(5\u79d2) -> \u6309\u4f4fW(20\u79d2) -> \u5faa\u73af\n"
        L";\n"
        L"; ===================================================================\n"
        L"\n"
        L"[Profile]\n"
        L"Name=170516901\n"
        L"StepCount=4\n"
        L"\n"
        L"[Step0]\n"
        L"Name=\u9009\u62e9\u91cd\u8d5b\n"
        L"Mode=tap\n"
        L"Duration=1000\n"
        L"Key=88\n"
        L"HoldMs=80\n"
        L"Interval=2000\n"
        L"\n"
        L"[Step1]\n"
        L"Name=\u786e\u8ba41\n"
        L"Mode=tap\n"
        L"Duration=10000\n"
        L"Key=13\n"
        L"HoldMs=80\n"
        L"Interval=6000\n"
        L"\n"
        L"[Step2]\n"
        L"Name=\u786e\u8ba42\n"
        L"Mode=tap\n"
        L"Duration=5000\n"
        L"Key=13\n"
        L"HoldMs=80\n"
        L"Interval=6000\n"
        L"\n"
        L"[Step3]\n"
        L"Name=\u6bd4\u8d5b\u4e2d\n"
        L"Mode=hold\n"
        L"Duration=20000\n"
        L"Key=87\n"
    );

    fclose(f);
    return TRUE;
}
