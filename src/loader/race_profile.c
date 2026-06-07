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

/* Forward declarations for encoding support */
static void EnsureIniEncoding(const WCHAR *path);

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

    EnsureIniEncoding(ini_path);

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

/* ─── Encoding Detection ──────────────────────────────────────────── */

typedef enum { ENC_UTF16LE, ENC_UTF8, ENC_ANSI } FileEncoding;

static BOOL IsValidUtf8(const unsigned char *data, DWORD size)
{
    DWORD i = 0;
    int non_ascii = 0;
    while (i < size) {
        unsigned char c = data[i];
        int extra = 0;
        if (c < 0x80) { i++; continue; }
        else if ((c & 0xE0) == 0xC0) extra = 1;
        else if ((c & 0xF0) == 0xE0) extra = 2;
        else if ((c & 0xF8) == 0xF0) extra = 3;
        else return FALSE;
        i++;
        for (int j = 0; j < extra; j++) {
            if (i >= size || (data[i] & 0xC0) != 0x80) return FALSE;
            i++;
        }
        non_ascii++;
    }
    return non_ascii > 0;
}

static FileEncoding DetectEncoding(const unsigned char *data, DWORD size)
{
    if (size >= 2 && data[0] == 0xFF && data[1] == 0xFE)
        return ENC_UTF16LE;
    if (size >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF)
        return ENC_UTF8;
    if (IsValidUtf8(data, min(size, 4096)))
        return ENC_UTF8;
    return ENC_ANSI;
}

static WCHAR* ReadFileToWideString(const WCHAR *path, int *out_len)
{
    HANDLE hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return NULL;

    DWORD size = GetFileSize(hFile, NULL);
    if (size == 0 || size == INVALID_FILE_SIZE) { CloseHandle(hFile); return NULL; }

    unsigned char *raw = (unsigned char *)malloc(size);
    if (!raw) { CloseHandle(hFile); return NULL; }

    DWORD read_bytes;
    if (!ReadFile(hFile, raw, size, &read_bytes, NULL)) {
        free(raw); CloseHandle(hFile); return NULL;
    }
    CloseHandle(hFile);

    WCHAR *result = NULL;
    int wlen = 0;
    FileEncoding enc = DetectEncoding(raw, read_bytes);

    switch (enc) {
    case ENC_UTF16LE: {
        const WCHAR *src = (const WCHAR *)(raw + 2);
        wlen = (int)((read_bytes - 2) / sizeof(WCHAR));
        result = (WCHAR *)malloc((wlen + 1) * sizeof(WCHAR));
        if (result) { memcpy(result, src, wlen * sizeof(WCHAR)); result[wlen] = L'\0'; }
        break;
    }
    case ENC_UTF8: {
        const char *src = (const char *)raw;
        int skip = (read_bytes >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) ? 3 : 0;
        wlen = MultiByteToWideChar(CP_UTF8, 0, src + skip, read_bytes - skip, NULL, 0);
        result = (WCHAR *)malloc((wlen + 1) * sizeof(WCHAR));
        if (result) {
            MultiByteToWideChar(CP_UTF8, 0, src + skip, read_bytes - skip, result, wlen);
            result[wlen] = L'\0';
        }
        break;
    }
    case ENC_ANSI: {
        wlen = MultiByteToWideChar(CP_ACP, 0, (const char *)raw, read_bytes, NULL, 0);
        result = (WCHAR *)malloc((wlen + 1) * sizeof(WCHAR));
        if (result) {
            MultiByteToWideChar(CP_ACP, 0, (const char *)raw, read_bytes, result, wlen);
            result[wlen] = L'\0';
        }
        break;
    }
    }

    free(raw);
    if (out_len) *out_len = wlen;
    return result;
}

/*
 * Ensure INI file has UTF-16LE BOM so GetPrivateProfileStringW can read it.
 * If file is UTF-8 or ANSI without BOM, convert in-place to UTF-16LE.
 */
static void EnsureIniEncoding(const WCHAR *path)
{
    HANDLE hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return;

    unsigned char bom[3] = {0};
    DWORD read_bytes;
    ReadFile(hFile, bom, 3, &read_bytes, NULL);
    CloseHandle(hFile);

    if (read_bytes >= 2 && bom[0] == 0xFF && bom[1] == 0xFE)
        return;

    int wlen = 0;
    WCHAR *wide = ReadFileToWideString(path, &wlen);
    if (!wide || wlen == 0) { free(wide); return; }

    hFile = CreateFileW(path, GENERIC_WRITE, 0, NULL,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) { free(wide); return; }

    unsigned char utf16bom[2] = {0xFF, 0xFE};
    DWORD written;
    WriteFile(hFile, utf16bom, 2, &written, NULL);
    WriteFile(hFile, wide, wlen * sizeof(WCHAR), &written, NULL);
    CloseHandle(hFile);
    free(wide);
}

/* ─── Public API ── Profile Template ──────────────────────────────── */

/* Copy the bundled default profile template (from exe's directory) to the profiles dir */
BOOL Profile_WriteDefaultTemplate(const WCHAR *path)
{
    WCHAR src_path[MAX_PATH];
    GetModuleFileNameW(NULL, src_path, MAX_PATH);
    WCHAR *slash = wcsrchr(src_path, L'\\');
    if (slash) *(slash + 1) = L'\0';
    wcscat(src_path, L"profiles\\170516901.ini");

    if (GetFileAttributesW(src_path) == INVALID_FILE_ATTRIBUTES)
        return FALSE;

    return CopyFileW(src_path, path, TRUE);
}

WCHAR* Profile_ReadComments(const WCHAR *ini_path)
{
    if (!ini_path) return NULL;

    int total_len = 0;
    WCHAR *content = ReadFileToWideString(ini_path, &total_len);
    if (!content) return NULL;

    WCHAR *result = (WCHAR *)calloc(4096, sizeof(WCHAR));
    if (!result) { free(content); return NULL; }

    int pos = 0;
    int cap = 4096;
    WCHAR *line_start = content;

    while (*line_start) {
        WCHAR *line_end = line_start;
        while (*line_end && *line_end != L'\n') line_end++;

        WCHAR *p = line_start;
        while (p < line_end && (*p == L' ' || *p == L'\t')) p++;

        if (*p == L';') {
            p++;
            if (*p == L' ') p++;

            int len = (int)(line_end - p);
            if (len > 0 && p[len - 1] == L'\r') len--;
            if (pos + len + 3 >= cap) break;

            memcpy(result + pos, p, len * sizeof(WCHAR));
            pos += len;
            result[pos++] = L'\r';
            result[pos++] = L'\n';
        }

        line_start = (*line_end == L'\n') ? line_end + 1 : line_end;
    }
    result[pos] = L'\0';

    free(content);
    return result;
}
