/*
 * tray.c - System Tray Icon Management
 *
 * Manages the notification area icon with state-dependent appearance
 * and a right-click context menu.
 */

#include "tray.h"
#include "resource.h"
#include "i18n.h"
#include <shellapi.h>

#define TRAY_ID 1

/* ─── Module State ────────────────────────────────────────────────── */
static struct {
    NOTIFYICONDATAW nid;
    HWND            owner;
    HINSTANCE       hInstance;
    TrayState       state;
    BOOL            created;
} s_tray = {0};

/* ─── Public API ──────────────────────────────────────────────────── */

BOOL Tray_Create(HWND owner_hwnd, HINSTANCE hInstance)
{
    if (s_tray.created) return TRUE;

    s_tray.owner = owner_hwnd;
    s_tray.hInstance = hInstance;

    ZeroMemory(&s_tray.nid, sizeof(s_tray.nid));
    s_tray.nid.cbSize = sizeof(NOTIFYICONDATAW);
    s_tray.nid.hWnd = owner_hwnd;
    s_tray.nid.uID = TRAY_ID;
    s_tray.nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    s_tray.nid.uCallbackMessage = WM_TRAY_CALLBACK;
    s_tray.nid.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APP_ICON));

    /* Fallback to default application icon if resource not found */
    if (!s_tray.nid.hIcon) {
        s_tray.nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    }

    wcscpy(s_tray.nid.szTip, I18n_Get(STR_TRAY_TIP_IDLE));

    if (!Shell_NotifyIconW(NIM_ADD, &s_tray.nid)) {
        return FALSE;
    }

    /* Enable balloon notifications (Vista+) */
    s_tray.nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &s_tray.nid);

    s_tray.created = TRUE;
    return TRUE;
}

void Tray_Destroy(void)
{
    if (!s_tray.created) return;

    Shell_NotifyIconW(NIM_DELETE, &s_tray.nid);
    if (s_tray.nid.hIcon) {
        DestroyIcon(s_tray.nid.hIcon);
    }
    s_tray.created = FALSE;
}

void Tray_SetState(TrayState state)
{
    if (!s_tray.created) return;

    s_tray.state = state;

    switch (state) {
    case TRAY_STATE_ACTIVE:
        wcscpy(s_tray.nid.szTip, I18n_Get(STR_TRAY_TIP_ACTIVE));
        break;
    case TRAY_STATE_ERROR:
        wcscpy(s_tray.nid.szTip, L"FH6 FocusKeeper - Error");
        break;
    case TRAY_STATE_IDLE:
    default:
        wcscpy(s_tray.nid.szTip, I18n_Get(STR_TRAY_TIP_IDLE));
        break;
    }

    s_tray.nid.uFlags = NIF_TIP | NIF_ICON;
    Shell_NotifyIconW(NIM_MODIFY, &s_tray.nid);
}

void Tray_ShowBalloon(const WCHAR *title, const WCHAR *message, DWORD icon_type)
{
    if (!s_tray.created) return;

    s_tray.nid.uFlags = NIF_INFO;
    s_tray.nid.dwInfoFlags = icon_type;
    wcsncpy(s_tray.nid.szInfoTitle, title, 63);
    s_tray.nid.szInfoTitle[63] = L'\0';
    wcsncpy(s_tray.nid.szInfo, message, 255);
    s_tray.nid.szInfo[255] = L'\0';

    Shell_NotifyIconW(NIM_MODIFY, &s_tray.nid);
}

void Tray_HandleMessage(HWND hwnd, WPARAM wParam, LPARAM lParam)
{
    (void)wParam;

    switch (LOWORD(lParam)) {
    case WM_LBUTTONDBLCLK:
        /* Double-click: show/restore main window */
        ShowWindow(hwnd, SW_RESTORE);
        SetForegroundWindow(hwnd);
        break;

    case WM_RBUTTONUP: {
        /* Right-click: show context menu */
        HMENU hMenu = CreatePopupMenu();
        if (hMenu) {
            POINT pt;
            GetCursorPos(&pt);

            AppendMenuW(hMenu, MF_STRING, IDM_TRAY_SHOW, I18n_Get(STR_TRAY_SHOW));
            AppendMenuW(hMenu, MF_STRING, IDM_TRAY_TOGGLE,
                s_tray.state == TRAY_STATE_ACTIVE ? I18n_Get(STR_TRAY_TOGGLE_OFF) : I18n_Get(STR_TRAY_TOGGLE_ON));
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, IDM_TRAY_EXIT, I18n_Get(STR_TRAY_EXIT));

            SetForegroundWindow(hwnd);
            TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
            PostMessage(hwnd, WM_NULL, 0, 0);
            DestroyMenu(hMenu);
        }
        break;
    }
    }
}
