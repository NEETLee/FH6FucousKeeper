/*
 * settings.c - INI Configuration Management
 *
 * Uses the Windows GetPrivateProfileString/WritePrivateProfileString API
 * for INI file operations. The INI file is stored alongside the executable.
 */

#include "settings.h"
#include "msg_replay.h"
#include <stdio.h>
#include <wchar.h>

#define INI_SECTION_GENERAL L"General"
#define INI_SECTION_HOTKEY  L"Hotkey"
#define INI_SECTION_UI      L"UI"
#define INI_SECTION_AUTORACE L"AutoRace"

static WCHAR s_ini_path[MAX_PATH] = {0};

/* ─── Internal Helpers ────────────────────────────────────────────── */

static void EnsureIniPath(void)
{
    if (s_ini_path[0] != L'\0') return;

    GetModuleFileNameW(NULL, s_ini_path, MAX_PATH);
    /* Replace .exe extension with .ini */
    WCHAR *dot = wcsrchr(s_ini_path, L'.');
    if (dot) {
        wcscpy(dot, L".ini");
    } else {
        wcscat(s_ini_path, L".ini");
    }
}

static int ReadInt(const WCHAR *section, const WCHAR *key, int default_val)
{
    return GetPrivateProfileIntW(section, key, default_val, s_ini_path);
}

static void WriteInt(const WCHAR *section, const WCHAR *key, int value)
{
    WCHAR buf[32];
    _snwprintf(buf, 32, L"%d", value);
    WritePrivateProfileStringW(section, key, buf, s_ini_path);
}

/* ─── Public API ──────────────────────────────────────────────────── */

void Settings_Default(AppSettings *settings)
{
    if (!settings) return;

    settings->game_version     = GAME_VERSION_AUTO;
    settings->replay_interval  = MSG_REPLAY_DEFAULT_INTERVAL;
    settings->hotkey_modifiers = DEFAULT_HOTKEY_MOD;
    settings->hotkey_vk        = DEFAULT_HOTKEY_VK;
    settings->auto_find        = TRUE;
    settings->minimize_to_tray = TRUE;
    settings->auto_start       = FALSE;
    settings->log_to_file      = TRUE;
    settings->start_minimized  = FALSE;
    settings->prevent_sleep    = TRUE;
    settings->language         = 0; /* LANG_AUTO */

    /* Auto Race defaults */
    wcscpy(settings->race_profile, L"skill_grind.ini");
    settings->race_hotkey_mod  = MOD_CONTROL;
    settings->race_hotkey_vk   = VK_F11;
}

BOOL Settings_Load(AppSettings *settings)
{
    if (!settings) return FALSE;

    EnsureIniPath();
    Settings_Default(settings);

    /* Check if INI file exists */
    if (GetFileAttributesW(s_ini_path) == INVALID_FILE_ATTRIBUTES) {
        /* File doesn't exist, save defaults and return */
        Settings_Save(settings);
        return TRUE;
    }

    settings->game_version     = (GameVersion)ReadInt(INI_SECTION_GENERAL, L"GameVersion", GAME_VERSION_AUTO);
    settings->replay_interval  = (DWORD)ReadInt(INI_SECTION_GENERAL, L"ReplayInterval", MSG_REPLAY_DEFAULT_INTERVAL);
    settings->auto_find        = (BOOL)ReadInt(INI_SECTION_GENERAL, L"AutoFind", TRUE);
    settings->log_to_file      = (BOOL)ReadInt(INI_SECTION_GENERAL, L"LogToFile", TRUE);
    settings->prevent_sleep    = (BOOL)ReadInt(INI_SECTION_GENERAL, L"PreventSleep", TRUE);

    settings->hotkey_modifiers = (DWORD)ReadInt(INI_SECTION_HOTKEY, L"Modifiers", DEFAULT_HOTKEY_MOD);
    settings->hotkey_vk        = (DWORD)ReadInt(INI_SECTION_HOTKEY, L"VirtualKey", DEFAULT_HOTKEY_VK);

    settings->minimize_to_tray = (BOOL)ReadInt(INI_SECTION_UI, L"MinimizeToTray", TRUE);
    settings->auto_start       = (BOOL)ReadInt(INI_SECTION_UI, L"AutoStart", FALSE);
    settings->start_minimized  = (BOOL)ReadInt(INI_SECTION_UI, L"StartMinimized", FALSE);
    settings->language         = ReadInt(INI_SECTION_UI, L"Language", 0);

    /* Auto Race */
    WCHAR profile_buf[64] = {0};
    GetPrivateProfileStringW(INI_SECTION_AUTORACE, L"Profile", L"skill_grind.ini",
                             profile_buf, 64, s_ini_path);
    wcsncpy(settings->race_profile, profile_buf, 63);
    settings->race_hotkey_mod  = (DWORD)ReadInt(INI_SECTION_AUTORACE, L"HotkeyModifiers", MOD_CONTROL);
    settings->race_hotkey_vk   = (DWORD)ReadInt(INI_SECTION_AUTORACE, L"HotkeyVK", VK_F11);

    /* Validate replay interval */
    if (settings->replay_interval < 10) settings->replay_interval = 10;
    if (settings->replay_interval > 1000) settings->replay_interval = 1000;

    return TRUE;
}

BOOL Settings_Save(const AppSettings *settings)
{
    if (!settings) return FALSE;

    EnsureIniPath();

    WriteInt(INI_SECTION_GENERAL, L"GameVersion",     (int)settings->game_version);
    WriteInt(INI_SECTION_GENERAL, L"ReplayInterval",  (int)settings->replay_interval);
    WriteInt(INI_SECTION_GENERAL, L"AutoFind",        (int)settings->auto_find);
    WriteInt(INI_SECTION_GENERAL, L"LogToFile",       (int)settings->log_to_file);
    WriteInt(INI_SECTION_GENERAL, L"PreventSleep",    (int)settings->prevent_sleep);

    WriteInt(INI_SECTION_HOTKEY, L"Modifiers",  (int)settings->hotkey_modifiers);
    WriteInt(INI_SECTION_HOTKEY, L"VirtualKey", (int)settings->hotkey_vk);

    WriteInt(INI_SECTION_UI, L"MinimizeToTray", (int)settings->minimize_to_tray);
    WriteInt(INI_SECTION_UI, L"AutoStart",      (int)settings->auto_start);
    WriteInt(INI_SECTION_UI, L"StartMinimized", (int)settings->start_minimized);
    WriteInt(INI_SECTION_UI, L"Language",       settings->language);

    /* Auto Race */
    WritePrivateProfileStringW(INI_SECTION_AUTORACE, L"Profile",
                               settings->race_profile, s_ini_path);
    WriteInt(INI_SECTION_AUTORACE, L"HotkeyModifiers", (int)settings->race_hotkey_mod);
    WriteInt(INI_SECTION_AUTORACE, L"HotkeyVK",        (int)settings->race_hotkey_vk);

    return TRUE;
}

const WCHAR* Settings_GetPath(void)
{
    EnsureIniPath();
    return s_ini_path;
}
