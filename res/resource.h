#ifndef FOCUSKEEPER_RESOURCE_H
#define FOCUSKEEPER_RESOURCE_H

/* ─── Dialog and Control IDs ──────────────────────────────────────── */

/* Main dialog */
#define IDD_MAIN            100

/* Tab control */
#define IDC_TAB             1001

/* Status page */
#define IDC_STATUS_ICON     1010
#define IDC_STATUS_TEXT     1011
#define IDC_GAME_TITLE      1012
#define IDC_GAME_VERSION    1013
#define IDC_GAME_HWND       1014
#define IDC_GAME_PID        1015
#define IDC_STAT_KILLFOCUS  1016
#define IDC_STAT_ACTIVATEAPP 1017
#define IDC_STAT_NCACTIVATE 1018
#define IDC_STAT_ACTIVATE   1019
#define IDC_BTN_ENABLE      1020
#define IDC_BTN_DISABLE     1021
#define IDC_BTN_FIND        1022
#define IDC_BTN_MUTE_TOGGLE 1023

/* Window list page */
#define IDC_WINDOW_LIST     1030
#define IDC_BTN_REFRESH     1031
#define IDC_BTN_SELECT      1032

/* Log page */
#define IDC_LOG_EDIT        1040
#define IDC_BTN_CLEAR_LOG   1041
#define IDC_BTN_EXPORT_LOG  1042

/* Settings page */
#define IDC_RADIO_AUTO      1050
#define IDC_RADIO_STEAM     1051
#define IDC_RADIO_STORE     1052
#define IDC_HOTKEY_CTRL     1053
#define IDC_HOTKEY_ALT      1054
#define IDC_HOTKEY_SHIFT    1055
#define IDC_HOTKEY_KEY      1056
#define IDC_CHK_AUTOFIND    1057
#define IDC_CHK_TRAY        1058
#define IDC_CHK_AUTOSTART   1059
#define IDC_CHK_LOGFILE     1060
#define IDC_EDIT_INTERVAL   1061
#define IDC_BTN_SAVE        1062
#define IDC_BTN_DEFAULT     1063
#define IDC_RADIO_LANG_AUTO 1064
#define IDC_RADIO_LANG_ZH   1065
#define IDC_RADIO_LANG_ZH_TW 1088
#define IDC_RADIO_LANG_EN   1066
#define IDC_LBL_INTERVAL    1067
#define IDC_LBL_OPTIONS     1068
#define IDC_LBL_LANGUAGE    1069
#define IDC_LBL_ABOUT_BRIEF 1070
#define IDC_LBL_ABOUT_AUTHOR 1071
#define IDC_LBL_ABOUT_REPO  1072
#define IDC_LBL_TIP         1073
#define IDC_CHK_PREVENT_SLEEP 1074
#define IDC_LBL_UPDATE       1075

/* Auto Race page */
#define IDC_COMBO_PROFILE   1080
#define IDC_BTN_RACE_START  1081
#define IDC_BTN_RACE_STOP   1082
#define IDC_LBL_RACE_STATUS 1083
#define IDC_LBL_RACE_STEP   1084
#define IDC_LBL_RACE_LAPS   1085
#define IDC_LBL_RACE_TIME   1086
#define IDC_LBL_RACE_TIP    1087

/* Tray icon */
#define IDI_APP_ICON        200
#define IDI_TRAY_ACTIVE     201
#define IDI_TRAY_IDLE       202

/* Tray menu */
#define IDM_TRAY_SHOW       2001
#define IDM_TRAY_TOGGLE     2002
#define IDM_TRAY_EXIT       2003

/* Timer IDs */
#define IDT_STATS_UPDATE    3001
#define IDT_AUTO_FIND       3002

/* Hotkey ID */
#define IDH_TOGGLE          4001
#define IDH_AUTO_RACE       4002

/* Custom messages */
#define WM_TRAY_CALLBACK    (WM_USER + 1)

#endif /* FOCUSKEEPER_RESOURCE_H */
