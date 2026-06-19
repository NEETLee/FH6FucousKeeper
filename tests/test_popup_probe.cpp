/*
 * test_popup_probe.cpp - Xbox share-code popup diagnostic
 *
 * Goal: figure out the share-code entry window on the Microsoft Store / Xbox
 * build of Forza, and whether we can inject digits into it.
 *
 * The Steam build is a normal Win32 window, so PostMessage to the main HWND
 * works. The Store build pops a SEPARATE top-level window (often a UWP
 * CoreWindow or the system text-input host) for code entry, which our normal
 * PostMessage-to-main-HWND never reaches. This tool helps us learn:
 *   1. what that popup window actually is (class / title / pid / process), and
 *   2. which injection method (PostMessage WM_CHAR vs focused SendInput) lands.
 *
 * Modes:
 *   test_popup_probe.exe watch                  (default)
 *        Loop forever. Prints the foreground window every second and prints any
 *        NEWLY appeared visible top-level window. Open the share-code popup in
 *        game and watch which window shows up -> note its HWND/class/pid.
 *
 *   test_popup_probe.exe list
 *        One-shot dump of all visible top-level windows.
 *
 *   test_popup_probe.exe children <hwndHex>
 *        Dump child windows of <hwnd> (UWP frames nest a CoreWindow inside).
 *
 *   test_popup_probe.exe post <hwndHex> <digits>
 *        Try PostMessage WM_CHAR + WM_KEYDOWN/UP of each digit to <hwnd>.
 *
 *   test_popup_probe.exe sendinput <digits>
 *        3s countdown (focus the popup yourself), then SendInput each digit as
 *        a real keystroke to whatever has focus.
 *
 * Run as Administrator (same as the main app).
 */

#include <windows.h>
#include <psapi.h>
#include <objbase.h>
#include <uiautomation.h>
#include <stdio.h>
#include <string.h>

static void ProcName(DWORD pid, char *out, int len) {
    out[0] = 0;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) { snprintf(out, len, "<pid %lu>", pid); return; }
    WCHAR w[MAX_PATH] = {0};
    DWORD sz = MAX_PATH;
    if (QueryFullProcessImageNameW(h, 0, w, &sz)) {
        WCHAR *slash = wcsrchr(w, L'\\');
        WideCharToMultiByte(CP_UTF8, 0, slash ? slash + 1 : w, -1, out, len, NULL, NULL);
    }
    CloseHandle(h);
}

static void PrintWindow(const char *tag, HWND hwnd) {
    if (!hwnd) { printf("%s <null>\n", tag); return; }
    WCHAR clsW[256] = {0}, titW[256] = {0};
    GetClassNameW(hwnd, clsW, 256);
    GetWindowTextW(hwnd, titW, 256);
    char cls[256] = {0}, tit[512] = {0};
    WideCharToMultiByte(CP_UTF8, 0, clsW, -1, cls, sizeof(cls), NULL, NULL);
    WideCharToMultiByte(CP_UTF8, 0, titW, -1, tit, sizeof(tit), NULL, NULL);
    DWORD pid = 0; GetWindowThreadProcessId(hwnd, &pid);
    char pname[128]; ProcName(pid, pname, sizeof(pname));
    RECT r = {0}; GetWindowRect(hwnd, &r);
    LONG style = GetWindowLong(hwnd, GWL_STYLE);
    printf("%s hwnd=%p pid=%lu(%s) vis=%d cls=\"%s\" title=\"%s\" rect=(%ld,%ld %ldx%ld) style=%08lX\n",
           tag, (void*)hwnd, pid, pname, IsWindowVisible(hwnd) ? 1 : 0,
           cls, tit, r.left, r.top, r.right - r.left, r.bottom - r.top, (unsigned long)style);
}

/* ── list / watch ─────────────────────────────────────────────────────── */

static BOOL CALLBACK ListProc(HWND hwnd, LPARAM lp) {
    (void)lp;
    if (!IsWindowVisible(hwnd)) return TRUE;
    WCHAR tit[256] = {0};
    GetWindowTextW(hwnd, tit, 256);
    if (tit[0] == 0) return TRUE;            /* skip untitled */
    PrintWindow("  ", hwnd);
    return TRUE;
}

#define MAX_SEEN 1024
static HWND g_seen[MAX_SEEN];
static int  g_seen_n;

static BOOL Seen(HWND h) {
    for (int i = 0; i < g_seen_n; i++) if (g_seen[i] == h) return TRUE;
    if (g_seen_n < MAX_SEEN) g_seen[g_seen_n++] = h;
    return FALSE;
}

static BOOL CALLBACK NewProc(HWND hwnd, LPARAM lp) {
    (void)lp;
    if (!IsWindowVisible(hwnd)) return TRUE;
    if (!Seen(hwnd)) PrintWindow("[NEW]", hwnd);
    return TRUE;
}

static BOOL CALLBACK ChildProc(HWND hwnd, LPARAM lp) {
    (void)lp;
    PrintWindow("  child", hwnd);
    return TRUE;
}

static void PostDigits(HWND hwnd, const char *digits);   /* defined below */

/* ── auto: find the share-code popup by title ──────────────────────────── */

/* Title substrings that identify the in-game text-entry overlay. The user
 * reports the Xbox build labels it "游戏用户界面" (Game User Interface). */
static const WCHAR *g_title_keys[] = {
    L"游戏用户界面", L"用户界面", L"Game User Interface", L"User Interface"
};

static HWND g_found;

static BOOL CALLBACK FindByTitleProc(HWND hwnd, LPARAM lp) {
    (void)lp;
    if (!IsWindowVisible(hwnd)) return TRUE;
    WCHAR tit[256] = {0};
    GetWindowTextW(hwnd, tit, 256);
    if (tit[0] == 0) return TRUE;
    for (size_t i = 0; i < sizeof(g_title_keys) / sizeof(g_title_keys[0]); i++) {
        if (wcsstr(tit, g_title_keys[i])) { g_found = hwnd; return FALSE; }
    }
    return TRUE;
}

static HWND FindPopup(void) {
    g_found = NULL;
    EnumWindows(FindByTitleProc, 0);
    return g_found;
}

static BOOL CALLBACK PostChildProc(HWND hwnd, LPARAM lp) {
    const char *digits = (const char *)lp;
    PostDigits(hwnd, digits);
    return TRUE;
}

static void FocusSendInput(HWND hwnd, const char *digits) {
    printf("Focus+SendInput to "); PrintWindow("->", hwnd);
    SetForegroundWindow(hwnd);
    Sleep(400);
    for (const char *p = digits; *p; p++) {
        if (*p < '0' || *p > '9') continue;
        INPUT in[2] = {0};
        in[0].type = INPUT_KEYBOARD; in[0].ki.wVk = (WORD)*p;
        in[1] = in[0]; in[1].ki.dwFlags = KEYEVENTF_KEYUP;
        UINT sent = SendInput(2, in, sizeof(INPUT));
        printf("  '%c' sent=%u err=%lu\n", *p, sent, GetLastError());
        Sleep(150);
    }
}

static void AutoMode(const char *digits) {
    printf("=== AUTO: waiting for the share-code popup (title match) ===\n");
    printf("Matching titles:");
    for (size_t i = 0; i < sizeof(g_title_keys)/sizeof(g_title_keys[0]); i++) {
        char k[128]; WideCharToMultiByte(CP_UTF8, 0, g_title_keys[i], -1, k, sizeof(k), NULL, NULL);
        printf(" \"%s\"", k);
    }
    printf("\nOpen the share-code input in game now. (Ctrl+C to stop)\n\n");

    HWND handled = NULL;
    for (;;) {
        HWND p = FindPopup();
        if (p && p != handled) {
            handled = p;
            printf("\n>>> POPUP DETECTED <<<\n");
            PrintWindow("popup=", p);
            printf("--- children ---\n");
            EnumChildWindows(p, ChildProc, 0);

            printf("\n[try 1] PostMessage to popup window:\n");
            PostDigits(p, digits);
            Sleep(1500);

            printf("\n[try 2] PostMessage to each child window:\n");
            EnumChildWindows(p, PostChildProc, (LPARAM)digits);
            Sleep(1500);

            printf("\n[try 3] Focus + SendInput to popup:\n");
            FocusSendInput(p, digits);

            printf("\n>>> Check the game: which try put the digits in? <<<\n");
            printf("    (close & reopen the popup to run again)\n\n");
        } else if (!p) {
            handled = NULL;   /* popup closed; allow re-trigger */
        }
        Sleep(500);
    }
}

/* ── injection ────────────────────────────────────────────────────────── */

static void PostDigits(HWND hwnd, const char *digits) {
    printf("PostMessage digits to "); PrintWindow("->", hwnd);
    for (const char *p = digits; *p; p++) {
        if (*p < '0' || *p > '9') continue;
        UINT sc = MapVirtualKey((UINT)*p, MAPVK_VK_TO_VSC);
        LPARAM down = 1 | (sc << 16);
        LPARAM up   = down | (1u << 30) | (1u << 31);
        SetLastError(0);
        BOOL a = PostMessageW(hwnd, WM_KEYDOWN, (WPARAM)*p, down);
        BOOL b = PostMessageW(hwnd, WM_CHAR,    (WPARAM)*p, down);
        Sleep(40);
        BOOL c = PostMessageW(hwnd, WM_KEYUP,   (WPARAM)*p, up);
        printf("  '%c' keydown=%d char=%d keyup=%d err=%lu\n",
               *p, a, b, c, GetLastError());
        Sleep(120);
    }
}

static void SendInputDigits(const char *digits) {
    printf("Focus the popup now. SendInput in 3s...\n");
    for (int i = 3; i > 0; i--) { printf("  %d\n", i); Sleep(1000); }
    HWND fg = GetForegroundWindow();
    PrintWindow("foreground=", fg);
    for (const char *p = digits; *p; p++) {
        if (*p < '0' || *p > '9') continue;
        INPUT in[2] = {0};
        in[0].type = INPUT_KEYBOARD;
        in[0].ki.wVk = (WORD)*p;            /* '0'..'9' == VK_0..VK_9 */
        in[1] = in[0];
        in[1].ki.dwFlags = KEYEVENTF_KEYUP;
        UINT sent = SendInput(2, in, sizeof(INPUT));
        printf("  '%c' sent=%u err=%lu\n", *p, sent, GetLastError());
        Sleep(120);
    }
}

/* ── UIA: focus-free inspection + SetValue ─────────────────────────────── */

static const char *CtName(CONTROLTYPEID t) {
    switch (t) {
    case UIA_ButtonControlTypeId:   return "Button";
    case UIA_EditControlTypeId:     return "Edit";
    case UIA_TextControlTypeId:     return "Text";
    case UIA_DocumentControlTypeId: return "Document";
    case UIA_GroupControlTypeId:    return "Group";
    case UIA_PaneControlTypeId:     return "Pane";
    case UIA_ListControlTypeId:     return "List";
    case UIA_ListItemControlTypeId: return "ListItem";
    case UIA_WindowControlTypeId:   return "Window";
    case UIA_CustomControlTypeId:   return "Custom";
    case UIA_ImageControlTypeId:    return "Image";
    default:                        return "?";
    }
}

static IUIAutomation             *g_uia;
static IUIAutomationTreeWalker    *g_walk;
static const WCHAR               *g_set;
static int                        g_set_done;

static BOOL HasPattern(IUIAutomationElement *el, PROPERTYID pid) {
    VARIANT v; VariantInit(&v);
    BOOL r = FALSE;
    if (SUCCEEDED(el->GetCurrentPropertyValue(pid, &v)) && v.vt == VT_BOOL)
        r = (v.boolVal != 0);
    VariantClear(&v);
    return r;
}

static void UiaDump(IUIAutomationElement *el, int depth) {
    if (!el || depth > 14) return;
    BSTR name = NULL; el->get_CurrentName(&name);
    CONTROLTYPEID ct = 0; el->get_CurrentControlType(&ct);
    BOOL hVal = HasPattern(el, UIA_IsValuePatternAvailablePropertyId);
    BOOL hInv = HasPattern(el, UIA_IsInvokePatternAvailablePropertyId);
    BOOL hTxt = HasPattern(el, UIA_IsTextPatternAvailablePropertyId);
    char nm[256] = {0};
    if (name) WideCharToMultiByte(CP_UTF8, 0, name, -1, nm, sizeof(nm), NULL, NULL);

    printf("%*s[%s] name=\"%s\" pat:%s%s%s\n", depth * 2, "",
           CtName(ct), nm,
           hVal ? "Value " : "", hInv ? "Invoke " : "", hTxt ? "Text" : "");
    if (name) SysFreeString(name);

    if (hVal && g_set && !g_set_done &&
        (ct == UIA_EditControlTypeId || ct == UIA_DocumentControlTypeId ||
         ct == UIA_TextControlTypeId || ct == UIA_CustomControlTypeId)) {
        IUIAutomationValuePattern *vp = NULL;
        el->GetCurrentPatternAs(UIA_ValuePatternId,
                                __uuidof(IUIAutomationValuePattern), (void **)&vp);
        if (vp) {
            BSTR b = SysAllocString(g_set);
            HRESULT hr = vp->SetValue(b);
            printf("%*s  -> SetValue hr=%08lX %s\n", depth * 2, "",
                   (unsigned long)hr, SUCCEEDED(hr) ? "(OK)" : "(FAIL)");
            SysFreeString(b);
            vp->Release();
            if (SUCCEEDED(hr)) g_set_done = 1;
        }
    }

    IUIAutomationElement *child = NULL;
    if (SUCCEEDED(g_walk->GetFirstChildElement(el, &child)) && child) {
        while (child) {
            UiaDump(child, depth + 1);
            IUIAutomationElement *next = NULL;
            g_walk->GetNextSiblingElement(child, &next);
            child->Release();
            child = next;
        }
    }
}

static void UiaMode(const char *digits) {
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    (void)hr;
    if (FAILED(CoCreateInstance(__uuidof(CUIAutomation), NULL, CLSCTX_INPROC_SERVER,
                                __uuidof(IUIAutomation), (void **)&g_uia)) || !g_uia) {
        printf("CUIAutomation create failed\n");
        return;
    }
    g_uia->get_ControlViewWalker(&g_walk);
    static WCHAR w[64];
    MultiByteToWideChar(CP_UTF8, 0, digits, -1, w, 64);
    g_set = w;

    printf("=== UIA: waiting for popup, will dump tree + try SetValue(\"%s\") ===\n", digits);
    printf("Open the share-code input in game. (Ctrl+C to stop)\n");
    HWND handled = NULL;
    for (;;) {
        HWND p = FindPopup();
        if (p && p != handled) {
            handled = p; g_set_done = 0;
            printf("\n>>> popup %p ===\n", (void *)p);
            IUIAutomationElement *root = NULL;
            if (SUCCEEDED(g_uia->ElementFromHandle(p, &root)) && root) {
                UiaDump(root, 0);
                root->Release();
            } else {
                printf("ElementFromHandle failed\n");
            }
            printf(">>> done. %s. Reopen popup to retry.\n",
                   g_set_done ? "SetValue succeeded - check the box"
                              : "no settable Value control found");
        } else if (!p) {
            handled = NULL;
        }
        Sleep(500);
    }
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    SetConsoleOutputCP(CP_UTF8);   /* so Chinese window titles render */
    const char *mode = argc > 1 ? argv[1] : "auto";

    if (strcmp(mode, "uia") == 0) {
        UiaMode(argc > 2 ? argv[2] : "362177064");
        return 0;
    }

    if (strcmp(mode, "auto") == 0) {
        const char *digits = argc > 2 ? argv[2] : "362177064";
        AutoMode(digits);
        return 0;
    }
    if (strcmp(mode, "list") == 0) {
        printf("=== visible top-level windows ===\n");
        EnumWindows(ListProc, 0);
        return 0;
    }
    if (strcmp(mode, "children") == 0 && argc > 2) {
        HWND h = (HWND)(uintptr_t)strtoull(argv[2], NULL, 16);
        printf("=== children of %p ===\n", (void*)h);
        EnumChildWindows(h, ChildProc, 0);
        return 0;
    }
    if (strcmp(mode, "post") == 0 && argc > 3) {
        HWND h = (HWND)(uintptr_t)strtoull(argv[2], NULL, 16);
        PostDigits(h, argv[3]);
        return 0;
    }
    if (strcmp(mode, "sendinput") == 0 && argc > 2) {
        SendInputDigits(argv[2]);
        return 0;
    }

    /* watch (default) */
    printf("=== WATCH: foreground + newly-appeared windows (Ctrl+C to stop) ===\n");
    printf("Open the share-code popup in game; note the [NEW] line for it.\n\n");
    /* Prime the seen-set silently so only windows that appear AFTER start
     * are reported as [NEW]. */
    EnumWindows([](HWND h, LPARAM)->BOOL{ Seen(h); return TRUE; }, 0);
    HWND last_fg = NULL;
    for (;;) {
        HWND fg = GetForegroundWindow();
        if (fg != last_fg) { PrintWindow("[FG]", fg); last_fg = fg; }
        EnumWindows(NewProc, 0);
        Sleep(1000);
    }
    return 0;
}
