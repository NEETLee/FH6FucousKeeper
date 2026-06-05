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
#include <stdio.h>
#include <wchar.h>

/* ─── Constants ───────────────────────────────────────────────────── */
#define WINDOW_WIDTH    520
#define WINDOW_HEIGHT   420
#define TAB_MARGIN      8
#define CTRL_MARGIN     12

/* ─── Module State ────────────────────────────────────────────────── */
static struct {
    HWND        hwnd_main;
    HWND        hwnd_tab;
    HWND        pages[4];       /* Status, Windows, Log, Settings */
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
    HWND        hwnd_edit_interval;
    HWND        hwnd_btn_save;
} s_gui = {0};

/* ─── Forward Declarations ────────────────────────────────────────── */
static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK PanelWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static void CreateTabControl(HWND parent);
static void CreateStatusPage(HWND parent);
static void CreateWindowListPage(HWND parent);
static void CreateLogPage(HWND parent);
static void CreateSettingsPage(HWND parent);
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

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        /* Use window background color for child statics */
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
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
    int screen_x, screen_y;

    if (!ctx) return NULL;

    s_gui.hInstance = ctx->hInstance;
    s_gui.settings = ctx->settings;

    /* Initialize Common Controls */
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES };
    InitCommonControlsEx(&icc);

    /* Register the panel container class */
    RegisterPanelClass(ctx->hInstance);

    /* Create a modern UI font */
    s_gui.hFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    if (!s_gui.hFont) {
        s_gui.hFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    }

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

    /* Center on screen */
    screen_x = (GetSystemMetrics(SM_CXSCREEN) - WINDOW_WIDTH) / 2;
    screen_y = (GetSystemMetrics(SM_CYSCREEN) - WINDOW_HEIGHT) / 2;

    /* Create main window */
    s_gui.hwnd_main = CreateWindowExW(
        WS_EX_APPWINDOW,
        L"FH6FocusKeeperMain",
        L"FH6 FocusKeeper",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        screen_x, screen_y, WINDOW_WIDTH, WINDOW_HEIGHT,
        NULL, NULL, ctx->hInstance, NULL
    );

    if (!s_gui.hwnd_main) return NULL;

    /* Create UI elements */
    CreateTabControl(s_gui.hwnd_main);
    CreateStatusPage(s_gui.hwnd_main);
    CreateWindowListPage(s_gui.hwnd_main);
    CreateLogPage(s_gui.hwnd_main);
    CreateSettingsPage(s_gui.hwnd_main);

    LayoutPages();
    SwitchPage(0);

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

    if (hook_active) {
        SetWindowTextW(s_gui.hwnd_status_text, I18n_Get(STR_STATUS_ACTIVE));
    } else {
        SetWindowTextW(s_gui.hwnd_status_text, I18n_Get(STR_STATUS_IDLE));
    }

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
            (hook_active && muted) ? I18n_Get(STR_BTN_MUTE_DISABLE) : I18n_Get(STR_BTN_MUTE_ENABLE));
    }
}

void Gui_ReadSettings(AppSettings *settings)
{
    if (!settings) return;

    /* Replay interval */
    if (s_gui.hwnd_edit_interval) {
        WCHAR buf[16] = {0};
        GetWindowTextW(s_gui.hwnd_edit_interval, buf, 15);
        DWORD val = (DWORD)_wtol(buf);
        if (val >= 10 && val <= 5000) settings->replay_interval = val;
    }

    /* Checkboxes */
    if (s_gui.hwnd_chk_autofind)
        settings->auto_find = (SendMessage(s_gui.hwnd_chk_autofind, BM_GETCHECK, 0, 0) == BST_CHECKED);
    if (s_gui.hwnd_chk_tray)
        settings->minimize_to_tray = (SendMessage(s_gui.hwnd_chk_tray, BM_GETCHECK, 0, 0) == BST_CHECKED);
    if (s_gui.hwnd_chk_logfile)
        settings->log_to_file = (SendMessage(s_gui.hwnd_chk_logfile, BM_GETCHECK, 0, 0) == BST_CHECKED);

    /* Language radio (on settings page = pages[3]) */
    HWND page = s_gui.pages[3];
    if (page) {
        HWND r_zh = GetDlgItem(page, IDC_RADIO_LANG_ZH);
        HWND r_en = GetDlgItem(page, IDC_RADIO_LANG_EN);
        if (r_zh && SendMessage(r_zh, BM_GETCHECK, 0, 0) == BST_CHECKED)
            settings->language = 1;
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

    /* Window title */
    SetWindowTextW(s_gui.hwnd_main, I18n_Get(STR_APP_TITLE));

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
        h = GetDlgItem(page, IDC_LBL_INTERVAL);
        if (h) SetWindowTextW(h, I18n_Get(STR_SETTINGS_INTERVAL));
        h = GetDlgItem(page, IDC_LBL_OPTIONS);
        if (h) SetWindowTextW(h, I18n_Get(STR_SETTINGS_OPTIONS));
        h = GetDlgItem(page, IDC_LBL_LANGUAGE);
        if (h) SetWindowTextW(h, I18n_Get(STR_SETTINGS_LANGUAGE));
        h = GetDlgItem(page, IDC_RADIO_LANG_AUTO);
        if (h) SetWindowTextW(h, I18n_Get(STR_SETTINGS_LANG_AUTO));
        h = GetDlgItem(page, IDC_RADIO_LANG_ZH);
        if (h) SetWindowTextW(h, I18n_Get(STR_SETTINGS_LANG_ZH));
        h = GetDlgItem(page, IDC_RADIO_LANG_EN);
        if (h) SetWindowTextW(h, I18n_Get(STR_SETTINGS_LANG_EN));
    }
    if (s_gui.hwnd_chk_autofind)
        SetWindowTextW(s_gui.hwnd_chk_autofind, I18n_Get(STR_SETTINGS_AUTOFIND));
    if (s_gui.hwnd_chk_tray)
        SetWindowTextW(s_gui.hwnd_chk_tray, I18n_Get(STR_SETTINGS_TRAY));
    if (s_gui.hwnd_chk_logfile)
        SetWindowTextW(s_gui.hwnd_chk_logfile, I18n_Get(STR_SETTINGS_LOGFILE));
    if (s_gui.hwnd_btn_save)
        SetWindowTextW(s_gui.hwnd_btn_save, I18n_Get(STR_BTN_SAVE));

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
        TAB_MARGIN, TAB_MARGIN,
        rc.right - TAB_MARGIN * 2, rc.bottom - TAB_MARGIN * 2,
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
}

/* ─── Status Page ─────────────────────────────────────────────────── */

static void CreateStatusPage(HWND parent)
{
    HWND page = CreateWindowExW(0, PANEL_CLASS, L"",
        WS_CHILD | WS_CLIPCHILDREN,
        0, 0, 0, 0, parent, NULL, s_gui.hInstance, NULL);
    s_gui.pages[0] = page;

    s_gui.hwnd_status_text = CreateCtrl(L"STATIC", I18n_Get(STR_STATUS_IDLE),
        SS_LEFT, CTRL_MARGIN, 10, 400, 24, page, IDC_STATUS_TEXT);

    s_gui.hwnd_game_info = CreateCtrl(L"STATIC",
        L"",
        SS_LEFT, CTRL_MARGIN, 44, 440, 80, page, IDC_GAME_TITLE);

    s_gui.hwnd_stats = CreateCtrl(L"STATIC",
        L"",
        SS_LEFT, CTRL_MARGIN, 134, 440, 100, page, IDC_STAT_KILLFOCUS);

    s_gui.hwnd_btn_find = CreateCtrl(L"BUTTON", I18n_Get(STR_BTN_FIND),
        BS_PUSHBUTTON, CTRL_MARGIN, 250, 130, 30, page, IDC_BTN_FIND);

    s_gui.hwnd_btn_enable = CreateCtrl(L"BUTTON", I18n_Get(STR_BTN_ENABLE),
        BS_PUSHBUTTON, 150, 250, 140, 30, page, IDC_BTN_ENABLE);

    s_gui.hwnd_btn_disable = CreateCtrl(L"BUTTON", I18n_Get(STR_BTN_MUTE_ENABLE),
        BS_PUSHBUTTON, 303, 250, 150, 30, page, IDC_BTN_MUTE_TOGGLE);
}

/* ─── Window List Page ────────────────────────────────────────────── */

static void CreateWindowListPage(HWND parent)
{
    LVCOLUMNW lvc;
    HWND page = CreateWindowExW(0, PANEL_CLASS, L"",
        WS_CHILD | WS_CLIPCHILDREN,
        0, 0, 0, 0, parent, NULL, s_gui.hInstance, NULL);
    s_gui.pages[1] = page;

    s_gui.hwnd_listview = CreateCtrlEx(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        CTRL_MARGIN, 10, 460, 230, page, IDC_WINDOW_LIST);

    ListView_SetExtendedListViewStyle(s_gui.hwnd_listview,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    lvc.mask = LVCF_TEXT | LVCF_WIDTH;

    lvc.pszText = (LPWSTR)I18n_Get(STR_COL_TITLE);  lvc.cx = 160;
    ListView_InsertColumn(s_gui.hwnd_listview, 0, &lvc);

    lvc.pszText = (LPWSTR)I18n_Get(STR_COL_CLASS);  lvc.cx = 100;
    ListView_InsertColumn(s_gui.hwnd_listview, 1, &lvc);

    lvc.pszText = (LPWSTR)I18n_Get(STR_COL_PROCESS); lvc.cx = 100;
    ListView_InsertColumn(s_gui.hwnd_listview, 2, &lvc);

    lvc.pszText = (LPWSTR)I18n_Get(STR_COL_PID);    lvc.cx = 50;
    ListView_InsertColumn(s_gui.hwnd_listview, 3, &lvc);

    lvc.pszText = (LPWSTR)I18n_Get(STR_COL_HWND);   lvc.cx = 80;
    ListView_InsertColumn(s_gui.hwnd_listview, 4, &lvc);

    s_gui.hwnd_btn_refresh = CreateCtrl(L"BUTTON", I18n_Get(STR_BTN_REFRESH),
        BS_PUSHBUTTON, CTRL_MARGIN, 250, 100, 28, page, IDC_BTN_REFRESH);

    s_gui.hwnd_btn_select = CreateCtrl(L"BUTTON", I18n_Get(STR_BTN_SELECT),
        BS_PUSHBUTTON, 125, 250, 130, 28, page, IDC_BTN_SELECT);
}

/* ─── Log Page ────────────────────────────────────────────────────── */

static void CreateLogPage(HWND parent)
{
    HWND page = CreateWindowExW(0, PANEL_CLASS, L"",
        WS_CHILD | WS_CLIPCHILDREN,
        0, 0, 0, 0, parent, NULL, s_gui.hInstance, NULL);
    s_gui.pages[2] = page;

    s_gui.hwnd_log_edit = CreateCtrlEx(WS_EX_CLIENTEDGE, L"EDIT", L"",
        ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
        CTRL_MARGIN, 10, 460, 248, page, IDC_LOG_EDIT);

    s_gui.hwnd_btn_clear = CreateCtrl(L"BUTTON", I18n_Get(STR_BTN_CLEAR_LOG),
        BS_PUSHBUTTON, CTRL_MARGIN, 266, 100, 28, page, IDC_BTN_CLEAR_LOG);
}

/* ─── Settings Page ───────────────────────────────────────────────── */

static void CreateSettingsPage(HWND parent)
{
    int y = 10;
    HWND page = CreateWindowExW(0, PANEL_CLASS, L"",
        WS_CHILD | WS_CLIPCHILDREN,
        0, 0, 0, 0, parent, NULL, s_gui.hInstance, NULL);
    s_gui.pages[3] = page;

    CreateCtrl(L"STATIC", I18n_Get(STR_SETTINGS_INTERVAL), SS_LEFT, CTRL_MARGIN, y, 170, 20, page, IDC_LBL_INTERVAL);
    s_gui.hwnd_edit_interval = CreateCtrlEx(WS_EX_CLIENTEDGE, L"EDIT", L"30",
        ES_NUMBER, 185, y - 2, 50, 22, page, IDC_EDIT_INTERVAL);

    y += 36;
    CreateCtrl(L"STATIC", I18n_Get(STR_SETTINGS_OPTIONS), SS_LEFT, CTRL_MARGIN, y, 80, 20, page, IDC_LBL_OPTIONS);
    y += 24;

    s_gui.hwnd_chk_autofind = CreateCtrl(L"BUTTON", I18n_Get(STR_SETTINGS_AUTOFIND),
        BS_AUTOCHECKBOX, CTRL_MARGIN + 10, y, 300, 20, page, IDC_CHK_AUTOFIND);
    y += 26;

    s_gui.hwnd_chk_tray = CreateCtrl(L"BUTTON", I18n_Get(STR_SETTINGS_TRAY),
        BS_AUTOCHECKBOX, CTRL_MARGIN + 10, y, 300, 20, page, IDC_CHK_TRAY);
    y += 26;

    s_gui.hwnd_chk_logfile = CreateCtrl(L"BUTTON", I18n_Get(STR_SETTINGS_LOGFILE),
        BS_AUTOCHECKBOX, CTRL_MARGIN + 10, y, 300, 20, page, IDC_CHK_LOGFILE);

    y += 36;
    CreateCtrl(L"STATIC", I18n_Get(STR_SETTINGS_LANGUAGE), SS_LEFT, CTRL_MARGIN, y, 60, 20, page, IDC_LBL_LANGUAGE);
    CreateCtrl(L"BUTTON", I18n_Get(STR_SETTINGS_LANG_AUTO),
        BS_AUTORADIOBUTTON | WS_GROUP, 75, y, 100, 20, page, IDC_RADIO_LANG_AUTO);
    CreateCtrl(L"BUTTON", I18n_Get(STR_SETTINGS_LANG_ZH),
        BS_AUTORADIOBUTTON, 180, y, 60, 20, page, IDC_RADIO_LANG_ZH);
    CreateCtrl(L"BUTTON", I18n_Get(STR_SETTINGS_LANG_EN),
        BS_AUTORADIOBUTTON, 245, y, 80, 20, page, IDC_RADIO_LANG_EN);

    y += 36;
    s_gui.hwnd_btn_save = CreateCtrl(L"BUTTON", I18n_Get(STR_BTN_SAVE),
        BS_PUSHBUTTON, CTRL_MARGIN, y, 110, 28, page, IDC_BTN_SAVE);

    /* Apply current settings to controls */
    if (s_gui.settings) {
        WCHAR interval_str[16];
        _snwprintf(interval_str, 15, L"%lu", s_gui.settings->replay_interval);
        SetWindowTextW(s_gui.hwnd_edit_interval, interval_str);

        SendMessage(s_gui.hwnd_chk_autofind, BM_SETCHECK,
            s_gui.settings->auto_find ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessage(s_gui.hwnd_chk_tray, BM_SETCHECK,
            s_gui.settings->minimize_to_tray ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessage(s_gui.hwnd_chk_logfile, BM_SETCHECK,
            s_gui.settings->log_to_file ? BST_CHECKED : BST_UNCHECKED, 0);

        /* Language radio buttons */
        HWND lang_btn;
        switch (s_gui.settings->language) {
        case 1:  lang_btn = GetDlgItem(page, IDC_RADIO_LANG_ZH); break;
        case 2:  lang_btn = GetDlgItem(page, IDC_RADIO_LANG_EN); break;
        default: lang_btn = GetDlgItem(page, IDC_RADIO_LANG_AUTO); break;
        }
        if (lang_btn) SendMessage(lang_btn, BM_SETCHECK, BST_CHECKED, 0);
    }
}

/* ─── Page Layout and Switching ───────────────────────────────────── */

static void LayoutPages(void)
{
    RECT rc;

    if (!s_gui.hwnd_tab) return;

    GetClientRect(s_gui.hwnd_tab, &rc);
    TabCtrl_AdjustRect(s_gui.hwnd_tab, FALSE, &rc);

    /* Convert to parent coordinates */
    MapWindowPoints(s_gui.hwnd_tab, s_gui.hwnd_main, (POINT *)&rc, 2);

    for (int i = 0; i < 4; i++) {
        if (s_gui.pages[i]) {
            SetWindowPos(s_gui.pages[i], NULL,
                rc.left, rc.top,
                rc.right - rc.left, rc.bottom - rc.top,
                SWP_NOZORDER);
        }
    }
}

static void SwitchPage(int index)
{
    if (index < 0 || index > 3) return;

    for (int i = 0; i < 4; i++) {
        if (s_gui.pages[i]) {
            ShowWindow(s_gui.pages[i], (i == index) ? SW_SHOW : SW_HIDE);
        }
    }
    s_gui.current_page = index;
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
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    return 0;
}
