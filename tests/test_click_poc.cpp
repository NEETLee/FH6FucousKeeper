/*
 * test_click_poc.cpp - Background mouse-click feasibility POC
 *
 * Goal: determine whether FH6's MENU layer processes window mouse messages
 *       (WM_MOUSEMOVE / WM_LBUTTONDOWN / WM_LBUTTONUP) posted in the
 *       background (window NOT foreground / minimized), so that we can reuse
 *       FH6Auto's click-based flows instead of converting everything to
 *       keyboard navigation.
 *
 * Strategy (non-destructive primary test):
 *   FH6 menus highlight an item on mouse HOVER. We post WM_MOUSEMOVE to a
 *   sweep of points across the menu and use the lime focus-ring detector to
 *   observe whether the highlight follows the cursor. If it tracks, mouse
 *   messages are processed -> clicks will work in background.
 *
 *   We test posting to BOTH the top-level window and the deepest child
 *   window under the point (the render surface), since the message target
 *   was likely wrong in earlier attempts.
 *
 * Build:  make test-click-poc
 * Run:    build/test_click_poc.exe   (game must be running, in roaming/menu)
 */

#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

extern "C" {
#include "screen_capture.h"
}
#include "input_backend.h"
#include "input_hook_backend.h"

#ifndef WM_MOUSEACTIVATE
#define WM_MOUSEACTIVATE 0x0021
#endif

static FILE *g_log = nullptr;
static InputBackend *g_input = nullptr;
static HWND g_top = nullptr;

/* client mapping */
static POINT g_client_origin = { 0, 0 };
static int   g_client_w = 0, g_client_h = 0;
static int   g_cap_w = 0, g_cap_h = 0;

static void logmsg(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    char buf[2048];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (g_log) { fputs(buf, g_log); fflush(g_log); }
    fputs(buf, stdout); fflush(stdout);
}

/* ─── Lime focus-ring detection (from test_focus_live.cpp) ───────────── */
static inline bool IsLime(BYTE b, BYTE g, BYTE r) {
    return (g >= 200) && (b <= 100) && (r >= 120) && (r <= 245)
        && ((int)g - (int)b >= 120) && ((int)g - (int)r >= 15);
}
struct FocusBox { int x0, y0, x1, y1; double fill; bool found; };

static FocusBox DetectFocus(const BYTE *px, int w, int h, int stride) {
    FocusBox fb = { 0, 0, 0, 0, 0.0, false };
    unsigned char *mask = (unsigned char *)calloc((size_t)w * h, 1);
    int *label = (int *)calloc((size_t)w * h, sizeof(int));
    int *stack = (int *)malloc((size_t)w * h * sizeof(int));
    if (!mask || !label || !stack) { free(mask); free(label); free(stack); return fb; }

    for (int y = 0; y < h; y++) {
        const BYTE *row = px + (size_t)y * stride;
        for (int x = 0; x < w; x++)
            if (IsLime(row[x*4+0], row[x*4+1], row[x*4+2])) mask[(size_t)y*w+x] = 1;
    }
    int cur = 0, minArea = (w * h) / 400;
    double bestScore = -1;
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        size_t idx = (size_t)y*w + x;
        if (!mask[idx] || label[idx]) continue;
        cur++; int sp = 0; stack[sp++] = (int)idx; label[idx] = cur;
        int mnx=x,mny=y,mxx=x,mxy=y,cnt=0;
        while (sp > 0) {
            int p = stack[--sp]; int py=p/w, pxx=p%w; cnt++;
            if(pxx<mnx)mnx=pxx; if(pxx>mxx)mxx=pxx; if(py<mny)mny=py; if(py>mxy)mxy=py;
            if(pxx>0){int q=p-1; if(mask[q]&&!label[q]){label[q]=cur;stack[sp++]=q;}}
            if(pxx<w-1){int q=p+1; if(mask[q]&&!label[q]){label[q]=cur;stack[sp++]=q;}}
            if(py>0){int q=p-w; if(mask[q]&&!label[q]){label[q]=cur;stack[sp++]=q;}}
            if(py<h-1){int q=p+w; if(mask[q]&&!label[q]){label[q]=cur;stack[sp++]=q;}}
        }
        int bw=mxx-mnx+1, bh=mxy-mny+1, area=bw*bh;
        if (area < minArea) continue;
        double fill = (double)cnt/area;
        if (fill < 0.55) {
            double score = (double)area*(1.0-fill);
            if (score > bestScore){ bestScore=score; fb.x0=mnx;fb.y0=mny;fb.x1=mxx;fb.y1=mxy;fb.fill=fill;fb.found=true; }
        }
    }
    free(stack); free(mask); free(label);
    return fb;
}

/* grab a stable focus reading with retries */
static FocusBox SenseFocus(int tries = 6) {
    FocusBox fb = {0,0,0,0,0,false};
    for (int i = 0; i < tries; i++) {
        CaptureFrame f = {};
        if (ScreenCapture_GrabFrame(&f)) {
            fb = DetectFocus(f.pixels, f.width, f.height, f.stride);
            g_cap_w = f.width; g_cap_h = f.height;
            if (fb.found) return fb;
        }
        Sleep(200);
    }
    return fb;
}

/* keyboard fwd decl */
static void key(DWORD vk, int hold);

/* make sure we are in a menu (focus ring present); ESC to open if needed */
static FocusBox EnsureMenu(void);

/* ─── keyboard helper ────────────────────────────────────────────────── */
static void key(DWORD vk, int hold) {
    g_input->key_down(g_input, vk); Sleep(hold); g_input->key_up(g_input, vk);
}

static FocusBox EnsureMenu(void) {
    FocusBox fb = SenseFocus(3);
    for (int i = 0; i < 4 && !fb.found; i++) {
        key(VK_ESCAPE, 80); Sleep(2200);
        fb = SenseFocus();
    }
    return fb;
}

/* Post mouse messages using CLIENT coordinates directly (== capture coords).
   This works regardless of the window's on-screen position or minimized
   state, because PostMessage delivers to the window's queue and the game
   reads the lParam client coords we provide. */
static void PostMoveC(HWND target, int capX, int capY, bool extras) {
    LPARAM lp = MAKELPARAM((WORD)capX, (WORD)capY);
    if (extras) {
        PostMessageW(target, WM_SETCURSOR, (WPARAM)target, MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
    }
    PostMessageW(target, WM_MOUSEMOVE, 0, lp);
}

static void PostClickC(HWND target, int capX, int capY) {
    LPARAM lp = MAKELPARAM((WORD)capX, (WORD)capY);
    PostMessageW(target, WM_MOUSEMOVE, 0, lp);
    Sleep(60);
    PostMessageW(target, WM_LBUTTONDOWN, MK_LBUTTON, lp);
    Sleep(60);
    PostMessageW(target, WM_LBUTTONUP, 0, lp);
}

/* ─── robust game-window finder (by process name, like FocusKeeper) ────
   Uses a process snapshot (no OpenProcess -> works without elevation). */
static BOOL IsForzaPid(DWORD pid) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return FALSE;
    PROCESSENTRY32W pe = {}; pe.dwSize = sizeof(pe);
    BOOL found = FALSE;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (pe.th32ProcessID == pid) {
                if (_wcsnicmp(pe.szExeFile, L"forzahorizon6", 13) == 0) found = TRUE;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

/* check if a window has a child of class "ForzaRenderingWindow" */
static BOOL CALLBACK HasForzaChildProc(HWND child, LPARAM lp) {
    WCHAR cls[64] = {};
    GetClassNameW(child, cls, 64);
    if (_wcsicmp(cls, L"ForzaRenderingWindow") == 0) { *(BOOL*)lp = TRUE; return FALSE; }
    return TRUE;
}
static BOOL HasForzaRenderChild(HWND h) {
    BOOL found = FALSE;
    EnumChildWindows(h, HasForzaChildProc, (LPARAM)&found);
    return found;
}

static BOOL CALLBACK FindGameProc(HWND h, LPARAM lp) {
    if (!IsWindowVisible(h)) return TRUE;
    if (HasForzaRenderChild(h)) { *(HWND*)lp = h; return FALSE; }
    DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
    if (pid && IsForzaPid(pid)) { *(HWND*)lp = h; return FALSE; }
    return TRUE;
}

static HWND FindGameWindow(void) {
    HWND found = NULL;
    EnumWindows(FindGameProc, (LPARAM)&found);
    return found;
}

/* ─── child window enumeration (diagnostic) ──────────────────────────── */
static int g_child_count = 0;
static BOOL CALLBACK EnumChildCb(HWND h, LPARAM) {
    if (g_child_count >= 40) return FALSE;
    WCHAR cls[128] = {}; GetClassNameW(h, cls, 128);
    RECT rc; GetWindowRect(h, &rc);
    char clsA[256]; WideCharToMultiByte(CP_UTF8,0,cls,-1,clsA,256,NULL,NULL);
    logmsg("  child[%d] hwnd=%p class=%-24s rect=(%ld,%ld %ldx%ld) vis=%d\n",
        g_child_count, (void*)h, clsA, rc.left, rc.top,
        rc.right-rc.left, rc.bottom-rc.top, IsWindowVisible(h));
    g_child_count++;
    return TRUE;
}

/* ─── hover sweep: post WM_MOUSEMOVE across the menu, watch focus ────── */
static void HoverSweep(const char *label, bool extras) {
    logmsg("\n--- HOVER SWEEP: %s (extras=%d) ---\n", label, extras);
    FocusBox base = SenseFocus();
    if (!base.found) { logmsg("  [skip] no focus ring (not in menu?)\n"); return; }
    int rowY = (base.y0 + base.y1) / 2;
    logmsg("  baseline focus center=(%d,%d)\n", (base.x0+base.x1)/2, rowY);

    double fracs[] = { 0.18, 0.38, 0.50, 0.62, 0.82 };
    int moved = 0; int n = (int)(sizeof(fracs)/sizeof(fracs[0]));
    int prevFocusX = (base.x0+base.x1)/2;
    for (int i = 0; i < n; i++) {
        int capX = (int)(g_cap_w * fracs[i]);
        PostMoveC(g_top, capX, rowY, extras);
        Sleep(450);
        FocusBox fb = SenseFocus(2);
        int fx = fb.found ? (fb.x0+fb.x1)/2 : -1;
        int fy = fb.found ? (fb.y0+fb.y1)/2 : -1;
        bool tracked = fb.found && abs(fx - capX) < (g_cap_w / 10);
        if (tracked || (fb.found && abs(fx - prevFocusX) > g_cap_w/20)) moved++;
        logmsg("  hover capX=%4d -> focus=(%d,%d) %s\n", capX, fx, fy,
            tracked ? "TRACKS!" : (fb.found ? "(moved?)" : "no-focus"));
        if (fb.found) prevFocusX = fx;
    }
    logmsg("  => %d/%d sweep points caused focus change. %s\n", moved, n,
        moved >= 2 ? ">>> MOUSE MESSAGES APPEAR TO WORK <<<" : "no clear response");
}

/* full luma diff between two frames, 0-100 */
static double FrameDiff(const BYTE *a, const BYTE *b, int w, int h, int stride) {
    long long acc = 0; long cnt = 0;
    for (int y = 0; y < h; y += 4) {
        const BYTE *ra = a + (size_t)y*stride, *rb = b + (size_t)y*stride;
        for (int x = 0; x < w; x += 4) {
            int la = (ra[x*4]+ra[x*4+1]+ra[x*4+2]);
            int lb = (rb[x*4]+rb[x*4+1]+rb[x*4+2]);
            acc += abs(la - lb); cnt++;
        }
    }
    if (!cnt) return 0;
    return (double)acc / cnt / 765.0 * 100.0;
}

/* save BGRA buffer as 24-bit BMP (bottom-up) for visual verification */
static void SaveBMP(const char *path, const BYTE *px, int w, int h, int stride) {
    int rowSize = (w * 3 + 3) & ~3;
    int dataSize = rowSize * h;
    BITMAPFILEHEADER fh = {};
    BITMAPINFOHEADER ih = {};
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(fh) + sizeof(ih);
    fh.bfSize = fh.bfOffBits + dataSize;
    ih.biSize = sizeof(ih);
    ih.biWidth = w; ih.biHeight = h; ih.biPlanes = 1; ih.biBitCount = 24;
    ih.biCompression = BI_RGB; ih.biSizeImage = dataSize;
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fwrite(&fh, sizeof(fh), 1, f);
    fwrite(&ih, sizeof(ih), 1, f);
    BYTE *row = (BYTE*)calloc(rowSize, 1);
    for (int y = h - 1; y >= 0; y--) {
        const BYTE *src = px + (size_t)y * stride;
        for (int x = 0; x < w; x++) {
            row[x*3+0] = src[x*4+0];
            row[x*3+1] = src[x*4+1];
            row[x*3+2] = src[x*4+2];
        }
        fwrite(row, rowSize, 1, f);
    }
    free(row);
    fclose(f);
}

/* grab a guaranteed-fresh frame by pumping the capture a few times */
static bool GrabFresh(CaptureFrame *f, int pumps) {
    bool ok = false;
    for (int i = 0; i < pumps; i++) { ok = ScreenCapture_GrabFrame(f); Sleep(140); }
    if (ok) { g_cap_w = f->width; g_cap_h = f->height; }
    return ok;
}

/* fraction (%) of sampled pixels that changed notably - robust on dark UIs */
static double ChangedFraction(const BYTE *a, const BYTE *b, int w, int h, int stride) {
    long ch = 0, cnt = 0;
    for (int y = 0; y < h; y += 4) {
        const BYTE *ra = a + (size_t)y*stride, *rb = b + (size_t)y*stride;
        for (int x = 0; x < w; x += 4) {
            int la = ra[x*4]+ra[x*4+1]+ra[x*4+2];
            int lb = rb[x*4]+rb[x*4+1]+rb[x*4+2];
            if (abs(la - lb) > 40) ch++;
            cnt++;
        }
    }
    return cnt ? 100.0 * ch / cnt : 0.0;
}

int wmain(int argc, wchar_t **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    g_log = fopen("build/click_poc_log.txt", "w");
    if (!g_log) g_log = stdout;

    g_top = FindGameWindow();
    if (!g_top && argc > 1) g_top = FindWindowW(NULL, argv[1]);
    if (!g_top) {
        logmsg("Game window not found! Dumping visible top-level windows:\n");
        struct DumpCtx { int n; } dc = { 0 };
        EnumWindows([](HWND h, LPARAM lp)->BOOL{
            DumpCtx *d = (DumpCtx*)lp;
            if (!IsWindowVisible(h)) return TRUE;
            WCHAR title[128]={}, cls[128]={};
            GetWindowTextW(h, title, 128);
            GetClassNameW(h, cls, 128);
            if (title[0]==0 && cls[0]==0) return TRUE;
            DWORD pid=0; GetWindowThreadProcessId(h,&pid);
            BOOL fc = HasForzaRenderChild(h);
            char t[256], c[256];
            WideCharToMultiByte(CP_UTF8,0,title,-1,t,256,NULL,NULL);
            WideCharToMultiByte(CP_UTF8,0,cls,-1,c,256,NULL,NULL);
            logmsg("  win hwnd=%p pid=%lu forzaChild=%d class='%s' title='%s'\n",
                   (void*)h, pid, fc, c, t);
            d->n++;
            return TRUE;
        }, (LPARAM)&dc);
        logmsg("  total visible windows dumped: %d\n", dc.n);
        return 1;
    }

    POINT o = {0,0}; ClientToScreen(g_top, &o); g_client_origin = o;
    RECT rc; GetClientRect(g_top, &rc);
    g_client_w = rc.right; g_client_h = rc.bottom;
    logmsg("top hwnd=%p client_origin=(%ld,%ld) client=%dx%d\n",
        (void*)g_top, o.x, o.y, g_client_w, g_client_h);

    logmsg("\n=== child windows ===\n");
    EnumChildWindows(g_top, EnumChildCb, 0);
    if (g_child_count == 0) logmsg("  (no child windows)\n");

    if (!ScreenCapture_Init() || !ScreenCapture_StartCapture(g_top)) {
        logmsg("capture start fail\n"); return 1;
    }
    Sleep(700);
    g_input = HookBackend_Create();
    if (!g_input || !g_input->init(g_input, g_top)) { logmsg("input init fail\n"); return 1; }

    /* Ensure we are in the menu (focus ring present). */
    logmsg("\n=== ensure menu (ESC) ===\n");
    FocusBox fb = EnsureMenu();
    if (!fb.found) { logmsg("  could not reach a menu with focus ring. Abort.\n"); }
    else logmsg("  in menu, focus=(%d,%d)-(%d,%d)\n", fb.x0,fb.y0,fb.x1,fb.y1);

    /* Hover sweep - the key non-destructive test (top-level window) */
    if (fb.found) {
        EnsureMenu();
        HoverSweep("TOP-LEVEL window, plain move", false);

        /* ── DETERMINISTIC CLICK on the 设置 (Settings) tile ──────────────
           Fixed client coordinate (bottom-right tile), posted DIRECTLY in
           client coords (works background/minimized). After clicking we
           FORCE a fresh WGC frame by hovering (move updates frames), which
           rules out the stale-frame artifact, then save before/after. */
        logmsg("\n=== DETERMINISTIC CLICK: bottom-right (设置) tile ===\n");
        EnsureMenu();
        CaptureFrame bf = {};
        GrabFresh(&bf, 4);
        int w = bf.width, h = bf.height, st = bf.stride;
        BYTE *sb = (BYTE*)malloc((size_t)st*h);
        memcpy(sb, bf.pixels, (size_t)st*h);
        SaveBMP("build/click_settings_before.bmp", sb, w, h, st);

        int capX = (int)(g_cap_w * 0.90), capY = (int)(g_cap_h * 0.90);

        /* ---- Method A: raw PostMessage click (button down/up) ---- */
        logmsg("  [A] raw CLICK 设置 tile client=(%d,%d)\n", capX, capY);
        PostClickC(g_top, capX, capY);
        Sleep(2500);
        for (int i = 0; i < 3; i++) {                 /* force fresh frames */
            PostMoveC(g_top, capX, capY, false); Sleep(180);
            PostMoveC(g_top, capX-3, capY-3, false); Sleep(180);
        }
        CaptureFrame afA = {}; GrabFresh(&afA, 4);
        double chA = ChangedFraction(sb, afA.pixels, w, h, st);
        SaveBMP("build/click_A_rawclick_after.bmp", afA.pixels, afA.width, afA.height, afA.stride);
        logmsg("      changed=%.1f%% => %s\n", chA,
               (chA > 12.0) ? "RAW CLICK ACTIVATED" : "raw click NO effect");

        /* recover to menu */
        key(VK_ESCAPE, 80); Sleep(1500);
        EnsureMenu();

        /* ---- Method B: hover to highlight 设置, then ENTER ---- */
        logmsg("  [B] HOVER 设置 tile then ENTER\n");
        for (int i = 0; i < 3; i++) { PostMoveC(g_top, capX, capY, false); Sleep(250); }
        FocusBox hb = SenseFocus();
        logmsg("      after hover, focus=%s (%d,%d)\n", hb.found?"yes":"no",
               hb.found?(hb.x0+hb.x1)/2:-1, hb.found?(hb.y0+hb.y1)/2:-1);
        SaveBMP("build/click_B_hover_before.bmp", sb, w, h, st); /* menu baseline */
        CaptureFrame hbf = {}; GrabFresh(&hbf, 2);
        BYTE *hsnap = (BYTE*)malloc((size_t)hbf.stride*hbf.height);
        memcpy(hsnap, hbf.pixels, (size_t)hbf.stride*hbf.height);
        SaveBMP("build/click_B_hover.bmp", hsnap, hbf.width, hbf.height, hbf.stride);

        key(VK_RETURN, 80);
        Sleep(2500);
        for (int i = 0; i < 3; i++) {                 /* force fresh frames */
            PostMoveC(g_top, (int)(g_cap_w*0.4), (int)(g_cap_h*0.4), false); Sleep(180);
            PostMoveC(g_top, (int)(g_cap_w*0.5), (int)(g_cap_h*0.5), false); Sleep(180);
        }
        CaptureFrame afB = {}; GrabFresh(&afB, 4);
        double chB = ChangedFraction(hsnap, afB.pixels, hbf.width, hbf.height, hbf.stride);
        SaveBMP("build/click_B_enter_after.bmp", afB.pixels, afB.width, afB.height, afB.stride);
        logmsg("      changed=%.1f%% => %s\n", chB,
               (chB > 12.0) ? ">>> HOVER+ENTER ACTIVATED 设置 <<<" : "hover+enter NO effect");
        free(hsnap);
        free(sb);

        key(VK_ESCAPE, 80); Sleep(1700);
        key(VK_ESCAPE, 80); Sleep(1200);
    }

    logmsg("\n=== DONE ===\n");
    g_input->release_all(g_input);
    g_input->shutdown(g_input);
    g_input->destroy(g_input);
    ScreenCapture_StopCapture();
    ScreenCapture_Shutdown();
    if (g_log != stdout) fclose(g_log);
    return 0;
}
