/*
 * gui.c - Main Application GUI
 *
 * Implements the tabbed dialog interface using Win32 CommonControls.
 * The dialog is created programmatically (no .rc template needed for layout)
 * using CreateWindowEx for full control over positioning.
 *
 * Tab pages are implemented as child dialogs that are shown/hidden
 * when the user switches tabs.
 */

#include "gui.h"
#include "resource.h"
#include "logger.h"
#include "i18n.h"
#include "version_check.h"
#include <stdio.h>
#include <wchar.h>
#include <shellapi.h>

/* ─── Constants ───────────────────────────────────────────────────── */
#define BASE_WIDTH      520
#define BASE_HEIGHT     420
#define TAB_MARGIN      4
#define CTRL_MARGIN     6

/* ─── DPI Scaling ─────────────────────────────────────────────────── */
static UINT s_dpi = 96;
#define S(x) MulDiv((x), (int)s_dpi, 96)

/* ─── Module State ────────────────────────────────────────────────── */
static struct {
    HWND        hwnd_main;
    HWND        hwnd_tab;
    HWND        pages[5];       /* Status, Windows, Log, Settings, AutoRace */
    int         current_page;
    HINSTANCE   hInstance;
    HFONT       hFont;
    AppSettings *settings;

    /* Status page controls */
    HWND        hwnd_status_text;
    HWND        hwnd_game_info;
    HWND        hwnd_stats;
    HWND        hwnd_btn_enable;
    HWND        hwnd_btn_disable;
    HWND        hwnd_btn_find;

    /* Window list page */
    HWND        hwnd_listview;
    HWND        hwnd_btn_refresh;
    HWND        hwnd_btn_select;

    /* Log page */
    HWND        hwnd_log_edit;
    HWND        hwnd_btn_clear;

    /* Settings page */
    HWND        hwnd_chk_autofind;
    HWND        hwnd_chk_tray;
    HWND        hwnd_chk_logfile;
    HWND        hwnd_chk_prevent_sleep;
    HWND        hwnd_btn_save;

    /* Auto Race page */
    HWND        hwnd_combo_profile;
    HWND        hwnd_btn_race_start;
    HWND        hwnd_btn_race_stop;
    HWND        hwnd_lbl_race_info;
    HWND        hwnd_edit_profile_desc;

    /* Modeless profile-notes window (at most one) */
    HWND        hwnd_notes;

    /* Footer font and brush */
    HFONT       hFontFooter;
    HFONT       hFontStatus;
    HBRUSH      hBrushBg;
    BOOL        hook_active;
    BOOL        farm_available;
    BOOL        pipeline_running;
} s_gui = {0};

/* ─── Forward Declarations ────────────────────────────────────────── */
static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK PanelWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static void CreateTabControl(HWND parent);
static void CreateStatusPage(HWND parent);
static void CreateWindowListPage(HWND parent);
static void CreateLogPage(HWND parent);
static void CreateSettingsPage(HWND parent);
static void CreateAutoRacePage(HWND parent);
static void SwitchPage(int index);
static void LayoutPages(void);
static BOOL RegisterPanelClass(HINSTANCE hInstance);

/* ─── Panel class: forwards WM_COMMAND/WM_NOTIFY to main window ──── */
#define PANEL_CLASS L"FKPanel"

static BOOL RegisterPanelClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = PanelWndProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = PANEL_CLASS;
    return RegisterClassExW(&wc) != 0;
}

static LRESULT CALLBACK PanelWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_COMMAND:
    case WM_NOTIFY:
        /* Forward to the top-level main window */
        return SendMessage(GetAncestor(hwnd, GA_ROOT), msg, wParam, lParam);

    case WM_CTLCOLORSTATIC: {
        int id = GetDlgCtrlID((HWND)lParam);
        if (id == IDC_LBL_ABOUT_BRIEF || id == IDC_LBL_ABOUT_AUTHOR) {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, RGB(130, 130, 130));
            SetBkMode(hdc, TRANSPARENT);
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        }
        if (id == IDC_LBL_TIP) {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, RGB(180, 60, 60));
            SetBkMode(hdc, TRANSPARENT);
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        }
        if (id == IDC_STATUS_TEXT) {
            HDC hdc = (HDC)wParam;
            if (s_gui.hook_active)
                SetTextColor(hdc, RGB(30, 140, 50));
            else
                SetTextColor(hdc, RGB(120, 120, 120));
            SetBkMode(hdc, TRANSPARENT);
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        }
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }

    case WM_CTLCOLORBTN:
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);

    case WM_SIZE:
        if (hwnd == s_gui.pages[4] && s_gui.hwnd_edit_profile_desc) {
            int w = LOWORD(lParam);
            int h = HIWORD(lParam);
            RECT rc;
            GetWindowRect(s_gui.hwnd_edit_profile_desc, &rc);
            MapWindowPoints(HWND_DESKTOP, hwnd, (POINT *)&rc, 2);
            int m = S(CTRL_MARGIN);
            SetWindowPos(s_gui.hwnd_edit_profile_desc, NULL,
                rc.left, rc.top,
                w - rc.left - m, h - rc.top - m,
                SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

/* ─── Helper: Create a styled control ─────────────────────────────── */
static HWND CreateCtrl(const WCHAR *cls, const WCHAR *text, DWORD style,
                       int x, int y, int w, int h, HWND parent, int id)
{
    HWND hwnd = CreateWindowExW(0, cls, text,
        WS_CHILD | WS_VISIBLE | style,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, s_gui.hInstance, NULL);
    if (hwnd && s_gui.hFont) {
        SendMessage(hwnd, WM_SETFONT, (WPARAM)s_gui.hFont, TRUE);
    }
    return hwnd;
}

static HWND CreateCtrlEx(DWORD exStyle, const WCHAR *cls, const WCHAR *text,
                         DWORD style, int x, int y, int w, int h, HWND parent, int id)
{
    HWND hwnd = CreateWindowExW(exStyle, cls, text,
        WS_CHILD | WS_VISIBLE | style,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, s_gui.hInstance, NULL);
    if (hwnd && s_gui.hFont) {
        SendMessage(hwnd, WM_SETFONT, (WPARAM)s_gui.hFont, TRUE);
    }
    return hwnd;
}

/* ─── Public API ──────────────────────────────────────────────────── */

HWND Gui_Create(const GuiContext *ctx)
{
    WNDCLASSEXW wc = {0};
    int screen_x, screen_y, win_w, win_h;
    HDC hdc;

    if (!ctx) return NULL;

    s_gui.hInstance = ctx->hInstance;
    s_gui.settings = ctx->settings;

    /* Detect system DPI for scaling */
    hdc = GetDC(NULL);
    s_dpi = (UINT)GetDeviceCaps(hdc, LOGPIXELSX);
    ReleaseDC(NULL, hdc);
    if (s_dpi < 96) s_dpi = 96;

    /* Initialize Common Controls */
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES | ICC_LINK_CLASS };
    InitCommonControlsEx(&icc);

    /* Register the panel container class */
    RegisterPanelClass(ctx->hInstance);

    /* Create a DPI-scaled UI font */
    s_gui.hFont = CreateFontW(-S(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    if (!s_gui.hFont) {
        s_gui.hFont = CreateFontW(-S(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    }

    /* Footer font: smaller, italic */
    s_gui.hFontFooter = CreateFontW(-S(11), 0, 0, 0, FW_NORMAL, TRUE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    if (!s_gui.hFontFooter) {
        s_gui.hFontFooter = CreateFontW(-S(11), 0, 0, 0, FW_NORMAL, TRUE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    }

    /* Status font: larger, bold */
    s_gui.hFontStatus = CreateFontW(-S(18), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    if (!s_gui.hFontStatus) {
        s_gui.hFontStatus = CreateFontW(-S(18), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    }

    /* Background brush for footer color handling */
    s_gui.hBrushBg = GetSysColorBrush(COLOR_WINDOW);

    /* Register window class */
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = ctx->hInstance;
    wc.hIcon = LoadIcon(ctx->hInstance, MAKEINTRESOURCE(IDI_APP_ICON));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"FH6FocusKeeperMain";
    wc.hIconSm = wc.hIcon;

    if (!wc.hIcon) wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);

    RegisterClassExW(&wc);

    /* Center on screen with DPI-scaled size */
    win_w = S(BASE_WIDTH);
    win_h = S(BASE_HEIGHT);
    screen_x = (GetSystemMetrics(SM_CXSCREEN) - win_w) / 2;
    screen_y = (GetSystemMetrics(SM_CYSCREEN) - win_h) / 2;

    /* Create main window (debug builds get a visible [DEBUG] title mark) */
#ifdef FK_DEBUG
    const WCHAR *win_title = L"FH6 FocusKeeper  [DEBUG]";
#else
    const WCHAR *win_title = L"FH6 FocusKeeper";
#endif
    s_gui.hwnd_main = CreateWindowExW(
        WS_EX_APPWINDOW,
        L"FH6FocusKeeperMain",
        win_title,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        screen_x, screen_y, win_w, win_h,
        NULL, NULL, ctx->hInstance, NULL
    );

    if (!s_gui.hwnd_main) return NULL;

    /* Create UI elements */
    CreateTabControl(s_gui.hwnd_main);
    CreateStatusPage(s_gui.hwnd_main);
    CreateWindowListPage(s_gui.hwnd_main);
    CreateLogPage(s_gui.hwnd_main);
    CreateSettingsPage(s_gui.hwnd_main);
    CreateAutoRacePage(s_gui.hwnd_main);

    LayoutPages();
    SwitchPage(0);

    /* Apply i18n labels + debug title suffix (CreateWindow used a fallback). */
    Gui_RefreshLanguage(FALSE, FALSE);

    /* Set initial status text */
    Gui_UpdateStatus(FALSE, NULL, NULL, NULL, 0);
    Gui_UpdateButtons(FALSE, FALSE);

    return s_gui.hwnd_main;
}

void Gui_UpdateStatus(BOOL hook_active, const WCHAR *game_title,
                      const WCHAR *version_str, HWND game_hwnd, DWORD game_pid)
{
    WCHAR buf[512];

    if (!s_gui.hwnd_status_text) return;

    s_gui.hook_active = hook_active;

    if (hook_active) {
        SetWindowTextW(s_gui.hwnd_status_text, I18n_Get(STR_STATUS_ACTIVE));
    } else {
        SetWindowTextW(s_gui.hwnd_status_text, I18n_Get(STR_STATUS_IDLE));
    }
    /* Force color update */
    InvalidateRect(s_gui.hwnd_status_text, NULL, TRUE);

    _snwprintf(buf, 511,
        L"%s%s\r\n"
        L"%s0x%08X\r\n"
        L"%s%lu",
        I18n_Get(STR_GAME_WINDOW),
        game_title ? game_title : I18n_Get(STR_NOT_DETECTED),
        I18n_Get(STR_GAME_HWND),
        (unsigned int)(UINT_PTR)game_hwnd,
        I18n_Get(STR_GAME_PID),
        game_pid);

    (void)version_str;
    SetWindowTextW(s_gui.hwnd_game_info, buf);
}

void Gui_UpdateStats(LONG killfocus, LONG activateapp, LONG ncactivate, LONG activate)
{
    WCHAR buf[256];

    if (!s_gui.hwnd_stats) return;

    _snwprintf(buf, 255,
        L"%s\r\n"
        L"  WM_KILLFOCUS      \u00d7 %ld\r\n"
        L"  WM_ACTIVATEAPP  \u00d7 %ld\r\n"
        L"  WM_NCACTIVATE    \u00d7 %ld\r\n"
        L"  WM_ACTIVATE        \u00d7 %ld",
        I18n_Get(STR_STATS_HEADER),
        killfocus, activateapp, ncactivate, activate);

    SetWindowTextW(s_gui.hwnd_stats, buf);
}

void Gui_AppendLog(const WCHAR *text)
{
    if (!s_gui.hwnd_log_edit || !text) return;

    int len = GetWindowTextLengthW(s_gui.hwnd_log_edit);

    /* Prevent log from growing too large (keep last 32KB) */
    if (len > 32000) {
        SendMessage(s_gui.hwnd_log_edit, EM_SETSEL, 0, len / 2);
        SendMessage(s_gui.hwnd_log_edit, EM_REPLACESEL, FALSE, (LPARAM)L"...(已截断)...\r\n");
        len = GetWindowTextLengthW(s_gui.hwnd_log_edit);
    }

    SendMessage(s_gui.hwnd_log_edit, EM_SETSEL, len, len);
    SendMessage(s_gui.hwnd_log_edit, EM_REPLACESEL, FALSE, (LPARAM)text);
    SendMessage(s_gui.hwnd_log_edit, EM_REPLACESEL, FALSE, (LPARAM)L"\r\n");
    SendMessage(s_gui.hwnd_log_edit, EM_SCROLLCARET, 0, 0);
}

void Gui_ClearLog(void)
{
    if (!s_gui.hwnd_log_edit) return;
    SetWindowTextW(s_gui.hwnd_log_edit, L"");
}

void Gui_RefreshWindowList(const FindResult *result)
{
    if (!s_gui.hwnd_listview || !result) return;

    ListView_DeleteAllItems(s_gui.hwnd_listview);

    for (int i = 0; i < result->count; i++) {
        const WindowInfo *info = &result->candidates[i];
        LVITEMW lvi = {0};
        WCHAR pid_str[32];
        WCHAR hwnd_str[32];

        lvi.mask = LVIF_TEXT;
        lvi.iItem = i;
        lvi.iSubItem = 0;
        lvi.pszText = (LPWSTR)info->title;
        ListView_InsertItem(s_gui.hwnd_listview, &lvi);

        ListView_SetItemText(s_gui.hwnd_listview, i, 1, (LPWSTR)info->class_name);
        ListView_SetItemText(s_gui.hwnd_listview, i, 2, (LPWSTR)info->process_name);

        _snwprintf(pid_str, 31, L"%lu", info->pid);
        ListView_SetItemText(s_gui.hwnd_listview, i, 3, pid_str);

        _snwprintf(hwnd_str, 31, L"0x%08X", (unsigned)(UINT_PTR)info->hwnd);
        ListView_SetItemText(s_gui.hwnd_listview, i, 4, hwnd_str);
    }
}

int Gui_GetSelectedWindowIndex(void)
{
    if (!s_gui.hwnd_listview) return -1;
    return ListView_GetNextItem(s_gui.hwnd_listview, -1, LVNI_SELECTED);
}

HWND Gui_GetMainWindow(void)
{
    return s_gui.hwnd_main;
}

void Gui_Show(BOOL show)
{
    if (s_gui.hwnd_main) {
        ShowWindow(s_gui.hwnd_main, show ? SW_SHOW : SW_HIDE);
        if (show) {
            SetForegroundWindow(s_gui.hwnd_main);
        }
    }
}

BOOL Gui_IsMinimized(void)
{
    return s_gui.hwnd_main ? IsIconic(s_gui.hwnd_main) : FALSE;
}

void Gui_UpdateButtons(BOOL hook_active, BOOL muted)
{
    if (s_gui.hwnd_btn_enable) {
        SetWindowTextW(s_gui.hwnd_btn_enable,
            hook_active ? I18n_Get(STR_BTN_DISABLE) : I18n_Get(STR_BTN_ENABLE));
    }
    if (s_gui.hwnd_btn_disable) {
        SetWindowTextW(s_gui.hwnd_btn_disable,
            muted ? I18n_Get(STR_BTN_MUTE_DISABLE) : I18n_Get(STR_BTN_MUTE_ENABLE));
    }
}

void Gui_ReadSettings(AppSettings *settings)
{
    if (!settings) return;

    /* Checkboxes */
    if (s_gui.hwnd_chk_autofind)
        settings->auto_find = (SendMessage(s_gui.hwnd_chk_autofind, BM_GETCHECK, 0, 0) == BST_CHECKED);
    if (s_gui.hwnd_chk_tray)
        settings->minimize_to_tray = (SendMessage(s_gui.hwnd_chk_tray, BM_GETCHECK, 0, 0) == BST_CHECKED);
    if (s_gui.hwnd_chk_logfile)
        settings->log_to_file = (SendMessage(s_gui.hwnd_chk_logfile, BM_GETCHECK, 0, 0) == BST_CHECKED);
    if (s_gui.hwnd_chk_prevent_sleep)
        settings->prevent_sleep = (SendMessage(s_gui.hwnd_chk_prevent_sleep, BM_GETCHECK, 0, 0) == BST_CHECKED);

    /* Language radio (on settings page = pages[3]) */
    HWND page = s_gui.pages[3];
    if (page) {
        HWND r_zh = GetDlgItem(page, IDC_RADIO_LANG_ZH);
        HWND r_zh_tw = GetDlgItem(page, IDC_RADIO_LANG_ZH_TW);
        HWND r_en = GetDlgItem(page, IDC_RADIO_LANG_EN);
        if (r_zh && SendMessage(r_zh, BM_GETCHECK, 0, 0) == BST_CHECKED)
            settings->language = 1;
        else if (r_zh_tw && SendMessage(r_zh_tw, BM_GETCHECK, 0, 0) == BST_CHECKED)
            settings->language = 3;
        else if (r_en && SendMessage(r_en, BM_GETCHECK, 0, 0) == BST_CHECKED)
            settings->language = 2;
        else
            settings->language = 0;
    }
}

void Gui_RefreshLanguage(BOOL hook_active, BOOL muted)
{
    TCITEMW tci;

    if (!s_gui.hwnd_main) return;

    /* Window title with version (debug builds get a visible [DEBUG] suffix) */
    {
        WCHAR title[160];
#ifdef FK_DEBUG
        wsprintfW(title, L"%s v%s  [DEBUG]", I18n_Get(STR_APP_TITLE), APP_VERSION);
#else
        wsprintfW(title, L"%s v%s", I18n_Get(STR_APP_TITLE), APP_VERSION);
#endif
        SetWindowTextW(s_gui.hwnd_main, title);
    }

    /* Tab labels */
    if (s_gui.hwnd_tab) {
        tci.mask = TCIF_TEXT;
        tci.pszText = (LPWSTR)I18n_Get(STR_TAB_STATUS);
        TabCtrl_SetItem(s_gui.hwnd_tab, 0, &tci);
        tci.pszText = (LPWSTR)I18n_Get(STR_TAB_WINDOWS);
        TabCtrl_SetItem(s_gui.hwnd_tab, 1, &tci);
        tci.pszText = (LPWSTR)I18n_Get(STR_TAB_LOG);
        TabCtrl_SetItem(s_gui.hwnd_tab, 2, &tci);
        tci.pszText = (LPWSTR)I18n_Get(STR_TAB_SETTINGS);
        TabCtrl_SetItem(s_gui.hwnd_tab, 3, &tci);
        tci.pszText = (LPWSTR)I18n_Get(STR_TAB_AUTO_RACE);
        TabCtrl_SetItem(s_gui.hwnd_tab, 4, &tci);
    }

    /* Status page */
    Gui_UpdateButtons(hook_active, muted);
    if (s_gui.hwnd_btn_find)
        SetWindowTextW(s_gui.hwnd_btn_find, I18n_Get(STR_BTN_FIND));

    /* Window list page */
    if (s_gui.hwnd_listview) {
        LVCOLUMNW lvc;
        lvc.mask = LVCF_TEXT;
        lvc.pszText = (LPWSTR)I18n_Get(STR_COL_TITLE);
        ListView_SetColumn(s_gui.hwnd_listview, 0, &lvc);
        lvc.pszText = (LPWSTR)I18n_Get(STR_COL_CLASS);
        ListView_SetColumn(s_gui.hwnd_listview, 1, &lvc);
        lvc.pszText = (LPWSTR)I18n_Get(STR_COL_PROCESS);
        ListView_SetColumn(s_gui.hwnd_listview, 2, &lvc);
        lvc.pszText = (LPWSTR)I18n_Get(STR_COL_PID);
        ListView_SetColumn(s_gui.hwnd_listview, 3, &lvc);
        lvc.pszText = (LPWSTR)I18n_Get(STR_COL_HWND);
        ListView_SetColumn(s_gui.hwnd_listview, 4, &lvc);
    }
    if (s_gui.hwnd_btn_refresh)
        SetWindowTextW(s_gui.hwnd_btn_refresh, I18n_Get(STR_BTN_REFRESH));
    if (s_gui.hwnd_btn_select)
        SetWindowTextW(s_gui.hwnd_btn_select, I18n_Get(STR_BTN_SELECT));

    /* Log page */
    if (s_gui.hwnd_btn_clear)
        SetWindowTextW(s_gui.hwnd_btn_clear, I18n_Get(STR_BTN_CLEAR_LOG));

    /* Settings page labels and controls */
    HWND page = s_gui.pages[3];
    if (page) {
        HWND h;
        h = GetDlgItem(page, IDC_LBL_OPTIONS);
        if (h) SetWindowTextW(h, I18n_Get(STR_SETTINGS_OPTIONS));
        h = GetDlgItem(page, IDC_LBL_LANGUAGE);
        if (h) SetWindowTextW(h, I18n_Get(STR_SETTINGS_LANGUAGE));
        h = GetDlgItem(page, IDC_RADIO_LANG_AUTO);
        if (h) SetWindowTextW(h, I18n_Get(STR_SETTINGS_LANG_AUTO));
        h = GetDlgItem(page, IDC_RADIO_LANG_ZH);
        if (h) SetWindowTextW(h, I18n_Get(STR_SETTINGS_LANG_ZH));
        h = GetDlgItem(page, IDC_RADIO_LANG_ZH_TW);
        if (h) SetWindowTextW(h, I18n_Get(STR_SETTINGS_LANG_ZH_TW));
        h = GetDlgItem(page, IDC_RADIO_LANG_EN);
        if (h) SetWindowTextW(h, I18n_Get(STR_SETTINGS_LANG_EN));
    }
    if (s_gui.hwnd_chk_autofind)
        SetWindowTextW(s_gui.hwnd_chk_autofind, I18n_Get(STR_SETTINGS_AUTOFIND));
    if (s_gui.hwnd_chk_tray)
        SetWindowTextW(s_gui.hwnd_chk_tray, I18n_Get(STR_SETTINGS_TRAY));
    if (s_gui.hwnd_chk_logfile)
        SetWindowTextW(s_gui.hwnd_chk_logfile, I18n_Get(STR_SETTINGS_LOGFILE));
    if (s_gui.hwnd_chk_prevent_sleep)
        SetWindowTextW(s_gui.hwnd_chk_prevent_sleep, I18n_Get(STR_SETTINGS_PREVENT_SLEEP));
    if (s_gui.hwnd_btn_save)
        SetWindowTextW(s_gui.hwnd_btn_save, I18n_Get(STR_BTN_SAVE));

    /* About / credits labels */
    HWND page0 = s_gui.pages[0];
    if (page0) {
        HWND hab = GetDlgItem(page0, IDC_LBL_ABOUT_BRIEF);
        if (hab) {
            WCHAR about[192];
            wsprintfW(about, L"%s  |  v%s", I18n_Get(STR_ABOUT_BRIEF), APP_VERSION);
            SetWindowTextW(hab, about);
        }
        HWND htip = GetDlgItem(page0, IDC_LBL_TIP);
        if (htip) SetWindowTextW(htip, I18n_Get(STR_TIP_WINDOWED));
    }
    if (page) {
        HWND ha;
        ha = GetDlgItem(page, IDC_LBL_ABOUT_AUTHOR);
        if (ha) SetWindowTextW(ha, I18n_Get(STR_ABOUT_AUTHOR));
        ha = GetDlgItem(page, IDC_LBL_ABOUT_REPO);
        if (ha) SetWindowTextW(ha, I18n_Get(STR_ABOUT_REPO));
    }

    /* Auto Farm tab: refresh ID'd buttons/checkboxes (static labels with
     * no control ID are localized at creation time). */
    HWND pf = s_gui.pages[4];
    if (pf) {
        struct { int id; StringId s; } items[] = {
            { IDC_PIPE_BTN_READ,   STR_PIPE_READ },
            { IDC_PIPE_CHK_RACE,   STR_PIPE_STEP_RACE },
            { IDC_PIPE_CHK_BUY,    STR_PIPE_STEP_BUY },
            { IDC_PIPE_CHK_SPIN,   STR_PIPE_STEP_SPIN },
            { IDC_PIPE_CHK_REMOVE, STR_PIPE_STEP_REMOVE },
            { IDC_PIPE_CHK_AUTO,   STR_PIPE_AUTO_COUNT },
            { IDC_PIPE_BTN_LOOP,   STR_PIPE_BTN_LOOP },
            { IDC_PIPE_BTN_STOP,   STR_PIPE_BTN_STOP },
            { IDC_PIPE_BTN_RACE,   STR_PIPE_STEP_RACE },
            { IDC_PIPE_BTN_BUY,    STR_PIPE_STEP_BUY },
            { IDC_PIPE_BTN_SPIN,   STR_PIPE_STEP_SPIN },
            { IDC_PIPE_BTN_REMOVE, STR_PIPE_STEP_REMOVE },
            { IDC_PIPE_BTN_CAR_NOTES,  STR_PIPE_BTN_NOTES },
            { IDC_PIPE_BTN_RACE_NOTES, STR_PIPE_BTN_NOTES },
        };
        for (size_t i = 0; i < sizeof(items)/sizeof(items[0]); i++) {
            HWND h = GetDlgItem(pf, items[i].id);
            if (h) SetWindowTextW(h, I18n_Get(items[i].s));
        }
    }

    /* Force full repaint including all child controls */
    RedrawWindow(s_gui.hwnd_main, NULL, NULL,
        RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

/* ─── Tab Control Creation ────────────────────────────────────────── */

static void CreateTabControl(HWND parent)
{
    TCITEMW tci;
    RECT rc;

    GetClientRect(parent, &rc);

    s_gui.hwnd_tab = CreateWindowExW(0, WC_TABCONTROLW, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TCS_TABS,
        S(TAB_MARGIN), S(TAB_MARGIN),
        rc.right - S(TAB_MARGIN) * 2, rc.bottom - S(TAB_MARGIN) * 2,
        parent, (HMENU)IDC_TAB, s_gui.hInstance, NULL);

    SendMessage(s_gui.hwnd_tab, WM_SETFONT, (WPARAM)s_gui.hFont, TRUE);

    tci.mask = TCIF_TEXT;

    tci.pszText = (LPWSTR)I18n_Get(STR_TAB_STATUS);
    TabCtrl_InsertItem(s_gui.hwnd_tab, 0, &tci);

    tci.pszText = (LPWSTR)I18n_Get(STR_TAB_WINDOWS);
    TabCtrl_InsertItem(s_gui.hwnd_tab, 1, &tci);

    tci.pszText = (LPWSTR)I18n_Get(STR_TAB_LOG);
    TabCtrl_InsertItem(s_gui.hwnd_tab, 2, &tci);

    tci.pszText = (LPWSTR)I18n_Get(STR_TAB_SETTINGS);
    TabCtrl_InsertItem(s_gui.hwnd_tab, 3, &tci);

    tci.pszText = (LPWSTR)I18n_Get(STR_TAB_AUTO_RACE);
    TabCtrl_InsertItem(s_gui.hwnd_tab, 4, &tci);
}

/* ─── Status Page ─────────────────────────────────────────────────── */

static void CreateStatusPage(HWND parent)
{
    int m = S(CTRL_MARGIN);
    int pw = S(488);
    HWND page = CreateWindowExW(0, PANEL_CLASS, L"",
        WS_CHILD | WS_CLIPCHILDREN,
        0, 0, 0, 0, parent, NULL, s_gui.hInstance, NULL);
    s_gui.pages[0] = page;

    /* Positions are placeholders; LayoutPages sizes to the real page rect. */
    s_gui.hwnd_status_text = CreateCtrl(L"STATIC", I18n_Get(STR_STATUS_IDLE),
        SS_CENTER | SS_CENTERIMAGE, 0, 0, pw, S(80), page, IDC_STATUS_TEXT);
    if (s_gui.hwnd_status_text && s_gui.hFontStatus)
        SendMessage(s_gui.hwnd_status_text, WM_SETFONT, (WPARAM)s_gui.hFontStatus, TRUE);

    s_gui.hwnd_game_info = CreateCtrl(L"STATIC", L"",
        SS_LEFT, m, S(86), S(230), S(72), page, IDC_GAME_TITLE);

    s_gui.hwnd_stats = CreateCtrl(L"STATIC", L"",
        SS_LEFT, m + S(240), S(86), S(230), S(110), page, IDC_STAT_KILLFOCUS);

    int btn_y = S(208);
    s_gui.hwnd_btn_find = CreateCtrl(L"BUTTON", I18n_Get(STR_BTN_FIND),
        BS_PUSHBUTTON, m, btn_y, S(150), S(32), page, IDC_BTN_FIND);

    s_gui.hwnd_btn_enable = CreateCtrl(L"BUTTON", I18n_Get(STR_BTN_ENABLE),
        BS_PUSHBUTTON, m + S(158), btn_y, S(160), S(32), page, IDC_BTN_ENABLE);

    s_gui.hwnd_btn_disable = CreateCtrl(L"BUTTON", I18n_Get(STR_BTN_MUTE_ENABLE),
        BS_PUSHBUTTON, m + S(326), btn_y, S(150), S(32), page, IDC_BTN_MUTE_TOGGLE);

    CreateCtrl(L"STATIC", I18n_Get(STR_TIP_WINDOWED),
        SS_CENTER | SS_NOPREFIX, 0, S(252), pw, S(20), page, IDC_LBL_TIP);

    HWND hAbout = CreateCtrl(L"STATIC", I18n_Get(STR_ABOUT_BRIEF),
        SS_CENTER | SS_NOPREFIX, 0, S(318), pw, S(14), page, IDC_LBL_ABOUT_BRIEF);
    if (hAbout && s_gui.hFontFooter)
        SendMessage(hAbout, WM_SETFONT, (WPARAM)s_gui.hFontFooter, TRUE);
}

/* ─── Window List Page ────────────────────────────────────────────── */

static void CreateWindowListPage(HWND parent)
{
    LVCOLUMNW lvc;
    int m = S(CTRL_MARGIN);
    HWND page = CreateWindowExW(0, PANEL_CLASS, L"",
        WS_CHILD | WS_CLIPCHILDREN,
        0, 0, 0, 0, parent, NULL, s_gui.hInstance, NULL);
    s_gui.pages[1] = page;

    /* Size filled by LayoutPages. */
    s_gui.hwnd_listview = CreateCtrlEx(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        m, m, S(488), S(260), page, IDC_WINDOW_LIST);

    ListView_SetExtendedListViewStyle(s_gui.hwnd_listview,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    lvc.mask = LVCF_TEXT | LVCF_WIDTH;

    lvc.pszText = (LPWSTR)I18n_Get(STR_COL_TITLE);  lvc.cx = S(160);
    ListView_InsertColumn(s_gui.hwnd_listview, 0, &lvc);

    lvc.pszText = (LPWSTR)I18n_Get(STR_COL_CLASS);  lvc.cx = S(100);
    ListView_InsertColumn(s_gui.hwnd_listview, 1, &lvc);

    lvc.pszText = (LPWSTR)I18n_Get(STR_COL_PROCESS); lvc.cx = S(100);
    ListView_InsertColumn(s_gui.hwnd_listview, 2, &lvc);

    lvc.pszText = (LPWSTR)I18n_Get(STR_COL_PID);    lvc.cx = S(50);
    ListView_InsertColumn(s_gui.hwnd_listview, 3, &lvc);

    lvc.pszText = (LPWSTR)I18n_Get(STR_COL_HWND);   lvc.cx = S(80);
    ListView_InsertColumn(s_gui.hwnd_listview, 4, &lvc);

    s_gui.hwnd_btn_refresh = CreateCtrl(L"BUTTON", I18n_Get(STR_BTN_REFRESH),
        BS_PUSHBUTTON, m, S(280), S(100), S(28), page, IDC_BTN_REFRESH);

    s_gui.hwnd_btn_select = CreateCtrl(L"BUTTON", I18n_Get(STR_BTN_SELECT),
        BS_PUSHBUTTON, m + S(115), S(280), S(140), S(28), page, IDC_BTN_SELECT);
}

/* ─── Log Page ────────────────────────────────────────────────────── */

static void CreateLogPage(HWND parent)
{
    int m = S(CTRL_MARGIN);
    HWND page = CreateWindowExW(0, PANEL_CLASS, L"",
        WS_CHILD | WS_CLIPCHILDREN,
        0, 0, 0, 0, parent, NULL, s_gui.hInstance, NULL);
    s_gui.pages[2] = page;

    /* Size filled by LayoutPages. */
    s_gui.hwnd_log_edit = CreateCtrlEx(WS_EX_CLIENTEDGE, L"EDIT", L"",
        ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
        m, m, S(488), S(260), page, IDC_LOG_EDIT);

    s_gui.hwnd_btn_clear = CreateCtrl(L"BUTTON", I18n_Get(STR_BTN_CLEAR_LOG),
        BS_PUSHBUTTON, m, S(280), S(100), S(28), page, IDC_BTN_CLEAR_LOG);
}

/* ─── Settings Page ───────────────────────────────────────────────── */

static void CreateSettingsPage(HWND parent)
{
    int m = S(CTRL_MARGIN);
    int y = S(4);
    HWND page = CreateWindowExW(0, PANEL_CLASS, L"",
        WS_CHILD | WS_CLIPCHILDREN,
        0, 0, 0, 0, parent, NULL, s_gui.hInstance, NULL);
    s_gui.pages[3] = page;

    CreateCtrl(L"STATIC", I18n_Get(STR_SETTINGS_OPTIONS), SS_LEFT, m, y, S(80), S(20), page, IDC_LBL_OPTIONS);
    y += S(24);

    s_gui.hwnd_chk_autofind = CreateCtrl(L"BUTTON", I18n_Get(STR_SETTINGS_AUTOFIND),
        BS_AUTOCHECKBOX, m + S(10), y, S(300), S(20), page, IDC_CHK_AUTOFIND);
    y += S(26);

    s_gui.hwnd_chk_tray = CreateCtrl(L"BUTTON", I18n_Get(STR_SETTINGS_TRAY),
        BS_AUTOCHECKBOX, m + S(10), y, S(300), S(20), page, IDC_CHK_TRAY);
    y += S(26);

    s_gui.hwnd_chk_logfile = CreateCtrl(L"BUTTON", I18n_Get(STR_SETTINGS_LOGFILE),
        BS_AUTOCHECKBOX, m + S(10), y, S(300), S(20), page, IDC_CHK_LOGFILE);
    y += S(26);

    s_gui.hwnd_chk_prevent_sleep = CreateCtrl(L"BUTTON", I18n_Get(STR_SETTINGS_PREVENT_SLEEP),
        BS_AUTOCHECKBOX, m + S(10), y, S(300), S(20), page, IDC_CHK_PREVENT_SLEEP);

    y += S(36);
    CreateCtrl(L"STATIC", I18n_Get(STR_SETTINGS_LANGUAGE), SS_LEFT, m, y, S(80), S(20), page, IDC_LBL_LANGUAGE);
    CreateCtrl(L"BUTTON", I18n_Get(STR_SETTINGS_LANG_AUTO),
        BS_AUTORADIOBUTTON | WS_GROUP, m + S(88), y, S(90), S(20), page, IDC_RADIO_LANG_AUTO);
    CreateCtrl(L"BUTTON", I18n_Get(STR_SETTINGS_LANG_ZH),
        BS_AUTORADIOBUTTON, m + S(184), y, S(80), S(20), page, IDC_RADIO_LANG_ZH);
    CreateCtrl(L"BUTTON", I18n_Get(STR_SETTINGS_LANG_ZH_TW),
        BS_AUTORADIOBUTTON, m + S(270), y, S(80), S(20), page, IDC_RADIO_LANG_ZH_TW);
    CreateCtrl(L"BUTTON", I18n_Get(STR_SETTINGS_LANG_EN),
        BS_AUTORADIOBUTTON, m + S(356), y, S(80), S(20), page, IDC_RADIO_LANG_EN);

    y += S(36);
    s_gui.hwnd_btn_save = CreateCtrl(L"BUTTON", I18n_Get(STR_BTN_SAVE),
        BS_PUSHBUTTON, m, y, S(110), S(28), page, IDC_BTN_SAVE);

    /* Footer pinned to page bottom in LayoutPages. */
    {
        HWND hRepo = CreateCtrl(L"SysLink", I18n_Get(STR_ABOUT_REPO),
            0, m, S(300), S(420), S(16), page, IDC_LBL_ABOUT_REPO);
        if (hRepo && s_gui.hFontFooter)
            SendMessage(hRepo, WM_SETFONT, (WPARAM)s_gui.hFontFooter, TRUE);
    }

    /* Apply current settings to controls */
    if (s_gui.settings) {
        SendMessage(s_gui.hwnd_chk_autofind, BM_SETCHECK,
            s_gui.settings->auto_find ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessage(s_gui.hwnd_chk_tray, BM_SETCHECK,
            s_gui.settings->minimize_to_tray ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessage(s_gui.hwnd_chk_logfile, BM_SETCHECK,
            s_gui.settings->log_to_file ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessage(s_gui.hwnd_chk_prevent_sleep, BM_SETCHECK,
            s_gui.settings->prevent_sleep ? BST_CHECKED : BST_UNCHECKED, 0);

        /* Language radio buttons */
        HWND lang_btn;
        switch (s_gui.settings->language) {
        case 1:  lang_btn = GetDlgItem(page, IDC_RADIO_LANG_ZH); break;
        case 3:  lang_btn = GetDlgItem(page, IDC_RADIO_LANG_ZH_TW); break;
        case 2:  lang_btn = GetDlgItem(page, IDC_RADIO_LANG_EN); break;
        default: lang_btn = GetDlgItem(page, IDC_RADIO_LANG_AUTO); break;
        }
        if (lang_btn) SendMessage(lang_btn, BM_SETCHECK, BST_CHECKED, 0);
    }
}

/* ─── Auto Race Page ──────────────────────────────────────────────── */

/* small helper for a numeric edit with a preceding label */
static void CreateLabeledEdit(HWND page, const WCHAR *label, const WCHAR *def,
                             int lx, int ex, int y, int lw, int ew, int id) {
    CreateCtrl(L"STATIC", label, SS_LEFT, lx, y + S(3), S(lw), S(18), page, 0);
    CreateCtrlEx(WS_EX_CLIENTEDGE, L"EDIT", def, ES_NUMBER,
        ex, y, S(ew), S(22), page, id);
}

/* Set an auto-checkbox checked. */
static void SetChecked(HWND page, int id, BOOL checked) {
    HWND h = GetDlgItem(page, id);
    if (h) SendMessage(h, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
}

static void CreateAutoRacePage(HWND parent)
{
    int m = S(CTRL_MARGIN);
    int pw = S(488);                  /* content width (matches Log page) */
    int lblw = S(80);                 /* width for the row labels (steps/single) */
    int y = S(4);
    HWND page = CreateWindowExW(0, PANEL_CLASS, L"",
        WS_CHILD | WS_CLIPCHILDREN,
        0, 0, 0, 0, parent, NULL, s_gui.hInstance, NULL);
    s_gui.pages[4] = page;
    s_gui.hwnd_edit_profile_desc = NULL;

    /* ── Foundation: car + race profile pickers, each with a Notes button ── */
    {
        int carlw = S(28), racelw = S(36), gap = S(8), notew = S(48), pad = S(4);
        int combo_total = pw - carlw - racelw - gap - notew * 2 - pad * 2;
        if (combo_total < S(120)) combo_total = S(120);
        int cw1 = combo_total / 2;
        int cw2 = combo_total - cw1;
        int cx = m;
        CreateCtrl(L"STATIC", I18n_Get(STR_PIPE_CAR), SS_LEFT,
            cx, y + S(4), carlw, S(18), page, 0);
        cx += carlw;
        CreateCtrlEx(0, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL,
            cx, y, cw1, S(220), page, IDC_PIPE_COMBO_CAR);
        cx += cw1 + pad;
        CreateCtrl(L"BUTTON", I18n_Get(STR_PIPE_BTN_NOTES), BS_PUSHBUTTON,
            cx, y, notew, S(24), page, IDC_PIPE_BTN_CAR_NOTES);
        cx += notew + gap;
        CreateCtrl(L"STATIC", I18n_Get(STR_PIPE_STEP_RACE), SS_LEFT,
            cx, y + S(4), racelw, S(18), page, 0);
        cx += racelw;
        s_gui.hwnd_combo_profile = CreateCtrlEx(0, L"COMBOBOX", L"",
            CBS_DROPDOWNLIST | WS_VSCROLL,
            cx, y, cw2, S(220), page, IDC_COMBO_PROFILE);
        cx += cw2 + pad;
        CreateCtrl(L"BUTTON", I18n_Get(STR_PIPE_BTN_NOTES), BS_PUSHBUTTON,
            cx, y, notew, S(24), page, IDC_PIPE_BTN_RACE_NOTES);
    }
    y += S(34);

    /* ── Foundation: economy readout + refresh CR/SP ── */
    {
        int btnw = S(120);
        HWND econ = CreateCtrl(L"STATIC",
            L"CR: - | SP: - | -\r\n-",
            SS_LEFT, m, y, pw - btnw - S(8), S(40), page, IDC_PIPE_LBL_ECON);
        if (econ && s_gui.hFont)
            SendMessage(econ, WM_SETFONT, (WPARAM)s_gui.hFont, TRUE);
        CreateCtrl(L"BUTTON", I18n_Get(STR_PIPE_READ), BS_PUSHBUTTON,
            m + pw - btnw, y + S(7), btnw, S(26), page, IDC_PIPE_BTN_READ);
    }
    y += S(46);

    /* ── Loop step selection (checkboxes) ── */
    CreateCtrl(L"STATIC", I18n_Get(STR_PIPE_STEPS_LABEL), SS_LEFT,
        m, y + S(2), lblw, S(18), page, 0);
    {
        int cx = m + lblw;
        int cw = (pw - lblw) / 4;
        CreateCtrlEx(0, L"BUTTON", I18n_Get(STR_PIPE_STEP_RACE), BS_AUTOCHECKBOX,
            cx, y, cw, S(20), page, IDC_PIPE_CHK_RACE);   cx += cw;
        CreateCtrlEx(0, L"BUTTON", I18n_Get(STR_PIPE_STEP_BUY), BS_AUTOCHECKBOX,
            cx, y, cw, S(20), page, IDC_PIPE_CHK_BUY);    cx += cw;
        CreateCtrlEx(0, L"BUTTON", I18n_Get(STR_PIPE_STEP_SPIN), BS_AUTOCHECKBOX,
            cx, y, cw, S(20), page, IDC_PIPE_CHK_SPIN);   cx += cw;
        CreateCtrlEx(0, L"BUTTON", I18n_Get(STR_PIPE_STEP_REMOVE), BS_AUTOCHECKBOX,
            cx, y, cw, S(20), page, IDC_PIPE_CHK_REMOVE);
    }
    SetChecked(page, IDC_PIPE_CHK_RACE, TRUE);
    SetChecked(page, IDC_PIPE_CHK_BUY, TRUE);
    SetChecked(page, IDC_PIPE_CHK_SPIN, TRUE);
    SetChecked(page, IDC_PIPE_CHK_REMOVE, TRUE);
    y += S(30);

    /* ── Params (single row): target SP, cycles, manual count, auto-count ──
     * CR/SP cost comes from the car profile; SP-per-lap from the race profile. */
    CreateLabeledEdit(page, I18n_Get(STR_PIPE_TARGET_SP), L"999",
        m, m + S(56), y, 52, 44, IDC_PIPE_EDIT_TARGET_SP);
    CreateLabeledEdit(page, I18n_Get(STR_PIPE_CYCLES), L"1",
        m + S(116), m + S(180), y, 56, 40, IDC_PIPE_EDIT_CYCLES);
    CreateLabeledEdit(page, I18n_Get(STR_PIPE_MANUAL_COUNT), L"1",
        m + S(240), m + S(304), y, 56, 40, IDC_PIPE_EDIT_COUNT);
    CreateCtrlEx(0, L"BUTTON", I18n_Get(STR_PIPE_AUTO_COUNT), BS_AUTOCHECKBOX,
        m + S(360), y + S(1), pw - S(360), S(22), page, IDC_PIPE_CHK_AUTO);
    SetChecked(page, IDC_PIPE_CHK_AUTO, TRUE);
    /* Hidden legacy laps edit kept so old code paths stay valid. */
    CreateCtrlEx(0, L"EDIT", L"3", ES_NUMBER, 0, 0, 0, 0, page, IDC_PIPE_EDIT_LAPS);
    ShowWindow(GetDlgItem(page, IDC_PIPE_EDIT_LAPS), SW_HIDE);
    y += S(34);

    /* ── Controls: full loop / stop ── */
    CreateCtrl(L"BUTTON", I18n_Get(STR_PIPE_BTN_LOOP),
        BS_PUSHBUTTON, m, y, pw / 2 - S(5), S(28), page, IDC_PIPE_BTN_LOOP);
    CreateCtrl(L"BUTTON", I18n_Get(STR_PIPE_BTN_STOP),
        BS_PUSHBUTTON, m + pw / 2 + S(5), y, pw / 2 - S(5), S(28), page, IDC_PIPE_BTN_STOP);
    EnableWindow(GetDlgItem(page, IDC_PIPE_BTN_STOP), FALSE);
    y += S(36);

    /* ── Single-step debug buttons ── */
    CreateCtrl(L"STATIC", I18n_Get(STR_PIPE_SINGLE_LABEL), SS_LEFT,
        m, y + S(4), lblw, S(18), page, 0);
    {
        int nbtns = 4;
        int bx = m + lblw;
        int avail = pw - lblw;
        int bg = S(5);
        int bw = (avail - bg * (nbtns - 1)) / nbtns;
        CreateCtrl(L"BUTTON", I18n_Get(STR_PIPE_STEP_RACE),   BS_PUSHBUTTON, bx, y, bw, S(24), page, IDC_PIPE_BTN_RACE);   bx += bw + bg;
        CreateCtrl(L"BUTTON", I18n_Get(STR_PIPE_STEP_BUY),    BS_PUSHBUTTON, bx, y, bw, S(24), page, IDC_PIPE_BTN_BUY);    bx += bw + bg;
        CreateCtrl(L"BUTTON", I18n_Get(STR_PIPE_STEP_SPIN),   BS_PUSHBUTTON, bx, y, bw, S(24), page, IDC_PIPE_BTN_SPIN);   bx += bw + bg;
        CreateCtrl(L"BUTTON", I18n_Get(STR_PIPE_STEP_REMOVE), BS_PUSHBUTTON, bx, y, bw, S(24), page, IDC_PIPE_BTN_REMOVE);
    }
    y += S(32);

    /* ── Per-step pipeline log (append, fills remaining height) ── */
    {
        int log_h = S(BASE_HEIGHT) - S(72) - y;
        if (log_h < S(60)) log_h = S(60);
        HWND log = CreateCtrlEx(WS_EX_CLIENTEDGE, L"EDIT", L"",
            ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
            m, y, pw, log_h, page, IDC_PIPE_LOG);
        if (log && s_gui.hFont)
            SendMessage(log, WM_SETFONT, (WPARAM)s_gui.hFont, TRUE);
    }
}

/* ─── Page Layout and Switching ───────────────────────────────────── */

static void LayoutPages(void)
{
    RECT rc;
    int m = S(CTRL_MARGIN);
    int btn_h = S(28);
    int gap = S(6);

    if (!s_gui.hwnd_tab) return;

    GetClientRect(s_gui.hwnd_tab, &rc);
    TabCtrl_AdjustRect(s_gui.hwnd_tab, FALSE, &rc);

    /* Convert to parent coordinates */
    MapWindowPoints(s_gui.hwnd_tab, s_gui.hwnd_main, (POINT *)&rc, 2);

    for (int i = 0; i < 5; i++) {
        if (s_gui.pages[i]) {
            SetWindowPos(s_gui.pages[i], NULL,
                rc.left, rc.top,
                rc.right - rc.left, rc.bottom - rc.top,
                SWP_NOZORDER);
        }
    }

    /* Status: center info columns; buttons / tip / footer span page. */
    if (s_gui.pages[0] && s_gui.hwnd_status_text) {
        RECT pr;
        GetClientRect(s_gui.pages[0], &pr);
        int pw = pr.right - pr.left;
        int ph = pr.bottom - pr.top;
        int banner_h = S(80);
        int tip_h = S(20);
        int about_h = S(14);
        int btn_row_h = S(32);
        int about_y = ph - m - about_h;
        int tip_y = about_y - S(8) - tip_h;
        int btn_y = tip_y - S(16) - btn_row_h;
        int col_y = banner_h + S(6);
        /* Center a fixed-width two-column block so short text doesn't hug the left. */
        int block_w = S(400);
        if (block_w > pw - 2 * m) block_w = pw - 2 * m;
        int block_x = (pw - block_w) / 2;
        int col_gap = S(16);
        int col_w = (block_w - col_gap) / 2;
        int avail = pw - 2 * m;
        int bg = S(8);
        int bw = (avail - 2 * bg) / 3;

        SetWindowPos(s_gui.hwnd_status_text, NULL, 0, 0, pw, banner_h, SWP_NOZORDER);
        if (s_gui.hwnd_game_info)
            SetWindowPos(s_gui.hwnd_game_info, NULL, block_x, col_y, col_w, S(72), SWP_NOZORDER);
        if (s_gui.hwnd_stats)
            SetWindowPos(s_gui.hwnd_stats, NULL, block_x + col_w + col_gap, col_y, col_w, S(110), SWP_NOZORDER);
        if (s_gui.hwnd_btn_find)
            SetWindowPos(s_gui.hwnd_btn_find, NULL, m, btn_y, bw, btn_row_h, SWP_NOZORDER);
        if (s_gui.hwnd_btn_enable)
            SetWindowPos(s_gui.hwnd_btn_enable, NULL, m + bw + bg, btn_y, bw, btn_row_h, SWP_NOZORDER);
        if (s_gui.hwnd_btn_disable)
            SetWindowPos(s_gui.hwnd_btn_disable, NULL, m + 2 * (bw + bg), btn_y, bw, btn_row_h, SWP_NOZORDER);

        HWND tip = GetDlgItem(s_gui.pages[0], IDC_LBL_TIP);
        if (tip) SetWindowPos(tip, NULL, 0, tip_y, pw, tip_h, SWP_NOZORDER);
        HWND about = GetDlgItem(s_gui.pages[0], IDC_LBL_ABOUT_BRIEF);
        if (about) SetWindowPos(about, NULL, 0, about_y, pw, about_h, SWP_NOZORDER);
    }

    /* Window list: list fills, buttons on bottom edge. */
    if (s_gui.pages[1] && s_gui.hwnd_listview) {
        RECT pr;
        GetClientRect(s_gui.pages[1], &pr);
        int pw = pr.right - pr.left;
        int ph = pr.bottom - pr.top;
        int btn_y = ph - m - btn_h;
        int list_h = btn_y - gap - m;
        if (list_h < S(60)) list_h = S(60);
        SetWindowPos(s_gui.hwnd_listview, NULL, m, m, pw - 2 * m, list_h, SWP_NOZORDER);
        if (s_gui.hwnd_btn_refresh)
            SetWindowPos(s_gui.hwnd_btn_refresh, NULL, m, btn_y, S(100), btn_h, SWP_NOZORDER);
        if (s_gui.hwnd_btn_select)
            SetWindowPos(s_gui.hwnd_btn_select, NULL, m + S(115), btn_y, S(140), btn_h, SWP_NOZORDER);
    }

    /* Log: edit fills, clear on bottom edge. */
    if (s_gui.pages[2] && s_gui.hwnd_log_edit) {
        RECT pr;
        GetClientRect(s_gui.pages[2], &pr);
        int pw = pr.right - pr.left;
        int ph = pr.bottom - pr.top;
        int btn_y = ph - m - btn_h;
        int edit_h = btn_y - gap - m;
        if (edit_h < S(60)) edit_h = S(60);
        SetWindowPos(s_gui.hwnd_log_edit, NULL, m, m, pw - 2 * m, edit_h, SWP_NOZORDER);
        if (s_gui.hwnd_btn_clear)
            SetWindowPos(s_gui.hwnd_btn_clear, NULL, m, btn_y, S(100), btn_h, SWP_NOZORDER);
    }

    /* Settings: center the form block; pin about link to bottom. */
    if (s_gui.pages[3]) {
        RECT pr;
        GetClientRect(s_gui.pages[3], &pr);
        int pw = pr.right - pr.left;
        int ph = pr.bottom - pr.top;
        /* Language row needs ~440px; keep form centered as one block. */
        int form_w = S(440);
        if (form_w > pw - 2 * m) form_w = pw - 2 * m;
        int x = (pw - form_w) / 2;
        int y = S(4);
        int row_h = S(20);
        int chk_indent = S(10);
        HWND h;

        h = GetDlgItem(s_gui.pages[3], IDC_LBL_OPTIONS);
        if (h) SetWindowPos(h, NULL, x, y, S(80), row_h, SWP_NOZORDER);
        y += S(24);

        if (s_gui.hwnd_chk_autofind)
            SetWindowPos(s_gui.hwnd_chk_autofind, NULL, x + chk_indent, y, form_w - chk_indent, row_h, SWP_NOZORDER);
        y += S(26);
        if (s_gui.hwnd_chk_tray)
            SetWindowPos(s_gui.hwnd_chk_tray, NULL, x + chk_indent, y, form_w - chk_indent, row_h, SWP_NOZORDER);
        y += S(26);
        if (s_gui.hwnd_chk_logfile)
            SetWindowPos(s_gui.hwnd_chk_logfile, NULL, x + chk_indent, y, form_w - chk_indent, row_h, SWP_NOZORDER);
        y += S(26);
        if (s_gui.hwnd_chk_prevent_sleep)
            SetWindowPos(s_gui.hwnd_chk_prevent_sleep, NULL, x + chk_indent, y, form_w - chk_indent, row_h, SWP_NOZORDER);
        y += S(36);

        h = GetDlgItem(s_gui.pages[3], IDC_LBL_LANGUAGE);
        if (h) SetWindowPos(h, NULL, x, y, S(80), row_h, SWP_NOZORDER);
        h = GetDlgItem(s_gui.pages[3], IDC_RADIO_LANG_AUTO);
        if (h) SetWindowPos(h, NULL, x + S(88), y, S(90), row_h, SWP_NOZORDER);
        h = GetDlgItem(s_gui.pages[3], IDC_RADIO_LANG_ZH);
        if (h) SetWindowPos(h, NULL, x + S(184), y, S(80), row_h, SWP_NOZORDER);
        h = GetDlgItem(s_gui.pages[3], IDC_RADIO_LANG_ZH_TW);
        if (h) SetWindowPos(h, NULL, x + S(270), y, S(80), row_h, SWP_NOZORDER);
        h = GetDlgItem(s_gui.pages[3], IDC_RADIO_LANG_EN);
        if (h) SetWindowPos(h, NULL, x + S(356), y, S(80), row_h, SWP_NOZORDER);
        y += S(36);

        if (s_gui.hwnd_btn_save)
            SetWindowPos(s_gui.hwnd_btn_save, NULL, x + (form_w - S(110)) / 2, y, S(110), S(28), SWP_NOZORDER);

        HWND repo = GetDlgItem(s_gui.pages[3], IDC_LBL_ABOUT_REPO);
        if (repo) {
            SetWindowPos(repo, NULL, m, ph - m - S(16),
                pw - 2 * m, S(16), SWP_NOZORDER);
        }
    }

    /* Auto race: stretch pipeline log to remaining page area. */
    if (s_gui.pages[4]) {
        HWND log = GetDlgItem(s_gui.pages[4], IDC_PIPE_LOG);
        if (log) {
            RECT pr, lr;
            GetClientRect(s_gui.pages[4], &pr);
            GetWindowRect(log, &lr);
            MapWindowPoints(HWND_DESKTOP, s_gui.pages[4], (POINT *)&lr, 2);
            SetWindowPos(log, NULL, m, lr.top,
                pr.right - pr.left - 2 * m,
                pr.bottom - pr.top - lr.top - m,
                SWP_NOZORDER);
        }
    }
}

static void SwitchPage(int index)
{
    if (index < 0 || index > 4) return;

    for (int i = 0; i < 5; i++) {
        if (s_gui.pages[i]) {
            ShowWindow(s_gui.pages[i], (i == index) ? SW_SHOW : SW_HIDE);
        }
    }
    s_gui.current_page = index;
}

/* ─── Auto Race Page Public API ──────────────────────────────────── */

void Gui_UpdateRaceStatus(const WCHAR *status, const WCHAR *step,
                          int laps, DWORD elapsed_ms)
{
    if (!s_gui.hwnd_lbl_race_info) return;

    WCHAR buf[256];

    if (!status || !step) {
        SetWindowTextW(s_gui.hwnd_lbl_race_info, status ? status : L"");
        return;
    }

    DWORD sec = elapsed_ms / 1000;
    DWORD min = sec / 60;
    DWORD hr = min / 60;

    _snwprintf(buf, 255, L"%s | %s | \u5708:%d | %02lu:%02lu:%02lu",
        status, step, laps, hr, min % 60, sec % 60);

    SetWindowTextW(s_gui.hwnd_lbl_race_info, buf);
}

void Gui_SetRaceRunning(BOOL running)
{
    if (s_gui.hwnd_btn_race_start)
        EnableWindow(s_gui.hwnd_btn_race_start, !running);
    if (s_gui.hwnd_btn_race_stop)
        EnableWindow(s_gui.hwnd_btn_race_stop, running);
    if (s_gui.hwnd_combo_profile)
        EnableWindow(s_gui.hwnd_combo_profile, !running);
}

void Gui_PopulateProfiles(const WCHAR names[][64], int count)
{
    if (!s_gui.hwnd_combo_profile) return;

    SendMessage(s_gui.hwnd_combo_profile, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < count; i++) {
        SendMessageW(s_gui.hwnd_combo_profile, CB_ADDSTRING, 0, (LPARAM)names[i]);
    }
    if (count > 0) {
        SendMessage(s_gui.hwnd_combo_profile, CB_SETCURSEL, 0, 0);
    }
}

void Gui_GetSelectedProfile(WCHAR *out, int max_len)
{
    if (!out || max_len <= 0) return;
    out[0] = L'\0';

    if (!s_gui.hwnd_combo_profile) return;

    int sel = (int)SendMessage(s_gui.hwnd_combo_profile, CB_GETCURSEL, 0, 0);
    if (sel >= 0) {
        SendMessageW(s_gui.hwnd_combo_profile, CB_GETLBTEXT, sel, (LPARAM)out);
    }
}

void Gui_ShowUpdateAvailable(const WCHAR *version, const WCHAR *url)
{
    if (!version || !url) return;

    /* Find the repo SysLink on the settings page */
    HWND page = s_gui.pages[3];
    if (!page) return;
    HWND hRepo = GetDlgItem(page, IDC_LBL_ABOUT_REPO);
    if (!hRepo) return;

    WCHAR text[512];
    _snwprintf(text, 512,
        L"GitHub: <a href=\"https://github.com/NEETLee/FH6FucousKeeper\">https://github.com/NEETLee/FH6FucousKeeper</a>"
        L"  \u26a0 <a href=\"%s\">\u65b0\u7248\u672c %s</a>", url, version);

    SetWindowTextW(hRepo, text);
}

void Gui_SetProfileDescription(const WCHAR *text)
{
    if (!s_gui.hwnd_edit_profile_desc) return;
    SetWindowTextW(s_gui.hwnd_edit_profile_desc, text ? text : L"");
}

/* ─── On-demand profile notes dialog (modeless) ───────────────────── */

#define NOTES_WND_CLASS L"FH6FocusKeeperNotesDlg"
#define IDC_NOTES_EDIT  1001

static void Notes_LayoutChildren(HWND hwnd)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    int m = S(12), btn_h = S(30), btn_w = S(96);
    HWND edit = GetDlgItem(hwnd, IDC_NOTES_EDIT);
    HWND ok = GetDlgItem(hwnd, IDOK);
    int edit_h = rc.bottom - m * 3 - btn_h;
    if (edit_h < S(80)) edit_h = S(80);
    if (edit)
        SetWindowPos(edit, NULL, m, m, rc.right - m * 2, edit_h, SWP_NOZORDER);
    if (ok)
        SetWindowPos(ok, NULL, (rc.right - btn_w) / 2, rc.bottom - m - btn_h,
            btn_w, btn_h, SWP_NOZORDER);
}

static LRESULT CALLBACK NotesWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lParam;
        const WCHAR *body = cs->lpCreateParams ? (const WCHAR *)cs->lpCreateParams : L"";
        HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", body,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
            ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
            0, 0, 0, 0,
            hwnd, (HMENU)(INT_PTR)IDC_NOTES_EDIT, cs->hInstance, NULL);
        HWND ok = CreateWindowExW(0, L"BUTTON", I18n_Get(STR_PIPE_NOTES_OK),
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            0, 0, 0, 0,
            hwnd, (HMENU)(INT_PTR)IDOK, cs->hInstance, NULL);
        if (s_gui.hFont) {
            if (edit) SendMessageW(edit, WM_SETFONT, (WPARAM)s_gui.hFont, TRUE);
            if (ok)   SendMessageW(ok,   WM_SETFONT, (WPARAM)s_gui.hFont, TRUE);
        }
        Notes_LayoutChildren(hwnd);
        return 0;
    }
    case WM_SIZE:
        Notes_LayoutChildren(hwnd);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (s_gui.hwnd_notes == hwnd)
            s_gui.hwnd_notes = NULL;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void Gui_ShowNotesDialog(HWND parent, const WCHAR *title, const WCHAR *text)
{
    const WCHAR *body = (text && text[0]) ? text : I18n_Get(STR_PIPE_NOTES_EMPTY);
    const WCHAR *ttl  = title ? title : L"Notes";

    static BOOL registered = FALSE;
    if (!registered) {
        WNDCLASSEXW wc = {0};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = NotesWndProc;
        wc.hInstance = s_gui.hInstance ? s_gui.hInstance : GetModuleHandleW(NULL);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = NOTES_WND_CLASS;
        RegisterClassExW(&wc);
        registered = TRUE;
    }

    /* Reuse the existing modeless window if still open. */
    if (s_gui.hwnd_notes && IsWindow(s_gui.hwnd_notes)) {
        SetWindowTextW(s_gui.hwnd_notes, ttl);
        HWND edit = GetDlgItem(s_gui.hwnd_notes, IDC_NOTES_EDIT);
        if (edit) SetWindowTextW(edit, body);
        ShowWindow(s_gui.hwnd_notes, SW_SHOW);
        SetWindowPos(s_gui.hwnd_notes, HWND_TOP, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        return;
    }

    int dlg_w = S(640), dlg_h = S(520);
    RECT pr = {0};
    if (parent) GetWindowRect(parent, &pr);
    int x = pr.left + ((pr.right - pr.left) - dlg_w) / 2;
    int y = pr.top  + ((pr.bottom - pr.top) - dlg_h) / 2;
    if (x < 0) x = 40;
    if (y < 0) y = 40;

    /* Owned popup, modeless: main window stays enabled and usable. */
    s_gui.hwnd_notes = CreateWindowExW(0,
        NOTES_WND_CLASS, ttl,
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_VISIBLE,
        x, y, dlg_w, dlg_h,
        parent, NULL,
        s_gui.hInstance ? s_gui.hInstance : GetModuleHandleW(NULL),
        (LPVOID)body);
}

/* ─── Pipeline (Auto Wheelspin Farm) helpers ──────────────────────── */

static int GetEditInt(HWND page, int id, int fallback) {
    WCHAR buf[32] = {0};
    HWND h = GetDlgItem(page, id);
    if (!h) return fallback;
    GetWindowTextW(h, buf, 31);
    if (!buf[0]) return fallback;
    return _wtoi(buf);
}

static BOOL IsChecked(HWND page, int id) {
    return page &&
        (SendMessage(GetDlgItem(page, id), BM_GETCHECK, 0, 0) == BST_CHECKED);
}

void Gui_GetPipelineParams(GuiPipelineParams *out)
{
    if (!out) return;
    HWND page = s_gui.pages[4];
    out->race_laps    = GetEditInt(page, IDC_PIPE_EDIT_LAPS, 3);
    out->cycles       = GetEditInt(page, IDC_PIPE_EDIT_CYCLES, 1);
    out->manual_count = GetEditInt(page, IDC_PIPE_EDIT_COUNT, 1);
    out->target_sp    = GetEditInt(page, IDC_PIPE_EDIT_TARGET_SP, 999);
    out->auto_count   = IsChecked(page, IDC_PIPE_CHK_AUTO);
    out->enable_race  = IsChecked(page, IDC_PIPE_CHK_RACE);
    out->enable_buy   = IsChecked(page, IDC_PIPE_CHK_BUY);
    out->enable_spin  = IsChecked(page, IDC_PIPE_CHK_SPIN);
    out->enable_remove= IsChecked(page, IDC_PIPE_CHK_REMOVE);
}

void Gui_SetPipelineEcon(int cr, int sp, int count,
                         const WCHAR *stage, const WCHAR *totals)
{
    HWND page = s_gui.pages[4];
    if (!page) return;
    HWND h = GetDlgItem(page, IDC_PIPE_LBL_ECON);
    if (!h) return;

    WCHAR crbuf[24], spbuf[24], cntbuf[24];
    if (cr >= 0)    _snwprintf(crbuf, 24, L"%d", cr);    else wcscpy(crbuf, L"-");
    if (sp >= 0)    _snwprintf(spbuf, 24, L"%d", sp);    else wcscpy(spbuf, L"-");
    if (count >= 0) _snwprintf(cntbuf, 24, L"%d", count); else wcscpy(cntbuf, L"-");

    WCHAR line1[256], line2[320], buf[600];
    _snwprintf(line1, 256, I18n_Get(STR_PIPE_ECON_FMT), crbuf, spbuf, cntbuf);
    _snwprintf(line2, 320, I18n_Get(STR_PIPE_STAGE_FMT),
        stage ? stage : L"-", totals ? totals : L"");
    _snwprintf(buf, 600, L"%s\r\n%s", line1, line2);
    SetWindowTextW(h, buf);
}

void Gui_AppendPipelineLog(const WCHAR *text)
{
    HWND page = s_gui.pages[4];
    if (!page || !text) return;
    HWND h = GetDlgItem(page, IDC_PIPE_LOG);
    if (!h) return;

    SYSTEMTIME st;
    GetLocalTime(&st);
    WCHAR line[1100];
    _snwprintf(line, 1100, L"[%02d:%02d:%02d] %s\r\n",
        st.wHour, st.wMinute, st.wSecond, text);

    int len = GetWindowTextLengthW(h);
    if (len > 28000) {
        SetWindowTextW(h, L"");
        len = 0;
    }
    SendMessageW(h, EM_SETSEL, len, len);
    SendMessageW(h, EM_REPLACESEL, FALSE, (LPARAM)line);
    SendMessageW(h, EM_SCROLLCARET, 0, 0);
}

void Gui_PopulateCarProfiles(const WCHAR names[][64], int count)
{
    HWND page = s_gui.pages[4];
    if (!page) return;
    HWND combo = GetDlgItem(page, IDC_PIPE_COMBO_CAR);
    if (!combo) return;
    SendMessage(combo, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < count; i++)
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)names[i]);
    if (count > 0)
        SendMessage(combo, CB_SETCURSEL, 0, 0);
}

int Gui_GetSelectedCarProfile(void)
{
    HWND page = s_gui.pages[4];
    if (!page) return -1;
    HWND combo = GetDlgItem(page, IDC_PIPE_COMBO_CAR);
    if (!combo) return -1;
    return (int)SendMessage(combo, CB_GETCURSEL, 0, 0);
}

void Gui_SetPipelineRunning(BOOL running)
{
    HWND page = s_gui.pages[4];
    if (!page) return;
    s_gui.pipeline_running = running;
    int step_btns[] = { IDC_PIPE_BTN_RACE, IDC_PIPE_BTN_READ, IDC_PIPE_BTN_BUY,
                        IDC_PIPE_BTN_SPIN, IDC_PIPE_BTN_REMOVE, IDC_PIPE_BTN_LOOP };
    for (size_t i = 0; i < sizeof(step_btns)/sizeof(step_btns[0]); i++) {
        HWND b = GetDlgItem(page, step_btns[i]);
        if (b) EnableWindow(b, !running && s_gui.farm_available);
    }
    HWND stop = GetDlgItem(page, IDC_PIPE_BTN_STOP);
    if (stop) EnableWindow(stop, running);
    if (s_gui.hwnd_btn_race_start)
        EnableWindow(s_gui.hwnd_btn_race_start,
                     !running && s_gui.farm_available);
}

void Gui_SetFarmAvailable(BOOL available, const WCHAR *reason)
{
    s_gui.farm_available = available;
    HWND page = s_gui.pages[4];
    if (!page) return;

    int controls[] = {
        IDC_PIPE_BTN_RACE, IDC_PIPE_BTN_READ, IDC_PIPE_BTN_BUY,
        IDC_PIPE_BTN_SPIN, IDC_PIPE_BTN_REMOVE, IDC_PIPE_BTN_LOOP,
        IDC_PIPE_CHK_RACE, IDC_PIPE_CHK_BUY, IDC_PIPE_CHK_SPIN,
        IDC_PIPE_CHK_REMOVE, IDC_PIPE_CHK_AUTO,
        IDC_PIPE_COMBO_CAR, IDC_COMBO_PROFILE,
        IDC_PIPE_EDIT_TARGET_SP, IDC_PIPE_EDIT_CYCLES, IDC_PIPE_EDIT_COUNT
    };
    for (size_t i = 0; i < sizeof(controls) / sizeof(controls[0]); i++) {
        HWND control = GetDlgItem(page, controls[i]);
        if (control) EnableWindow(control, available && !s_gui.pipeline_running);
    }
    if (s_gui.hwnd_btn_race_start)
        EnableWindow(s_gui.hwnd_btn_race_start,
                     available && !s_gui.pipeline_running);
    if (!available && reason)
        Gui_SetPipelineEcon(-1, -1, -1, reason, L"");
}

/* ─── Window Procedure ────────────────────────────────────────────── */

static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_NOTIFY: {
        NMHDR *nmhdr = (NMHDR *)lParam;
        if (nmhdr->idFrom == IDC_TAB && nmhdr->code == TCN_SELCHANGE) {
            int sel = TabCtrl_GetCurSel(s_gui.hwnd_tab);
            SwitchPage(sel);
        }
        if (nmhdr->idFrom == IDC_LBL_ABOUT_REPO &&
            (nmhdr->code == NM_CLICK || nmhdr->code == NM_RETURN)) {
            NMLINK *link = (NMLINK *)lParam;
            if (link->item.szUrl[0])
                ShellExecuteW(NULL, L"open", link->item.szUrl, NULL, NULL, SW_SHOWNORMAL);
        }
        break;
    }

    case WM_SIZE:
        LayoutPages();
        break;

    case WM_CLOSE:
        if (s_gui.settings && s_gui.settings->minimize_to_tray) {
            ShowWindow(hwnd, SW_HIDE);
            /* Notify user the first time */
            static BOOL s_tray_notified = FALSE;
            if (!s_tray_notified) {
                s_tray_notified = TRUE;
                /* Post a custom message so the main module can show a balloon */
                PostMessage(hwnd, WM_USER + 99, 0, 0);
            }
            return 0;
        }
        DestroyWindow(hwnd);
        break;

    case WM_DESTROY:
        if (s_gui.hFont) {
            DeleteObject(s_gui.hFont);
            s_gui.hFont = NULL;
        }
        if (s_gui.hFontFooter) {
            DeleteObject(s_gui.hFontFooter);
            s_gui.hFontFooter = NULL;
        }
        if (s_gui.hFontStatus) {
            DeleteObject(s_gui.hFontStatus);
            s_gui.hFontStatus = NULL;
        }
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    return 0;
}
