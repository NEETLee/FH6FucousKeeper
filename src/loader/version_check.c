/*
 * version_check.c - GitHub Release version checker
 *
 * Uses WinHTTP to query GitHub API for latest release.
 * Runs in a background thread to avoid blocking the UI.
 */

#include "version_check.h"
#include <winhttp.h>
#include <wchar.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GITHUB_HOST     L"api.github.com"
#define GITHUB_PATH     L"/repos/NEETLee/FH6FucousKeeper/releases/latest"
#define USER_AGENT      L"FH6FocusKeeper/1.0"

static WCHAR s_latest_version[64] = {0};
static WCHAR s_release_url[256] = {0};
static BOOL  s_has_update = FALSE;

typedef struct {
    HWND notify_hwnd;
    UINT notify_msg;
} CheckContext;

/* Simple JSON value extractor (no full parser needed) */
static BOOL ExtractJsonString(const char *json, const char *key, WCHAR *out, int max_len)
{
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);

    const char *pos = strstr(json, search);
    if (!pos) return FALSE;

    pos += strlen(search);
    while (*pos == ' ' || *pos == ':' || *pos == ' ') pos++;
    if (*pos != '"') return FALSE;
    pos++;

    int i = 0;
    while (*pos && *pos != '"' && i < max_len - 1) {
        out[i++] = (WCHAR)(unsigned char)*pos++;
    }
    out[i] = L'\0';
    return i > 0;
}

/* Compare version strings like "v1.2.3" or "1.2.3" */
static BOOL IsNewerVersion(const WCHAR *latest, const WCHAR *current)
{
    const WCHAR *l = latest;
    const WCHAR *c = current;

    if (*l == L'v' || *l == L'V') l++;
    if (*c == L'v' || *c == L'V') c++;

    int lparts[4] = {0}, cparts[4] = {0};

    swscanf(l, L"%d.%d.%d.%d", &lparts[0], &lparts[1], &lparts[2], &lparts[3]);
    swscanf(c, L"%d.%d.%d.%d", &cparts[0], &cparts[1], &cparts[2], &cparts[3]);

    for (int i = 0; i < 4; i++) {
        if (lparts[i] > cparts[i]) return TRUE;
        if (lparts[i] < cparts[i]) return FALSE;
    }
    return FALSE;
}

static DWORD WINAPI CheckThreadProc(LPVOID param)
{
    CheckContext *ctx = (CheckContext *)param;
    HWND hwnd = ctx->notify_hwnd;
    UINT msg = ctx->notify_msg;
    free(ctx);

    HINTERNET hSession = NULL, hConnect = NULL, hRequest = NULL;
    char *buffer = NULL;

    hSession = WinHttpOpen(USER_AGENT, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                           WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) goto done;

    /* Set timeouts: 10s connect, 15s send/receive */
    WinHttpSetTimeouts(hSession, 10000, 10000, 15000, 15000);

    hConnect = WinHttpConnect(hSession, GITHUB_HOST, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) goto done;

    hRequest = WinHttpOpenRequest(hConnect, L"GET", GITHUB_PATH,
                                  NULL, WINHTTP_NO_REFERER,
                                  WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) goto done;

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
        goto done;

    if (!WinHttpReceiveResponse(hRequest, NULL))
        goto done;

    /* Read response body */
    DWORD total_size = 0;
    DWORD buf_cap = 8192;
    buffer = (char *)malloc(buf_cap);
    if (!buffer) goto done;

    DWORD bytes_read = 0;
    while (WinHttpReadData(hRequest, buffer + total_size, buf_cap - total_size - 1, &bytes_read)) {
        if (bytes_read == 0) break;
        total_size += bytes_read;
        if (total_size >= buf_cap - 1) {
            buf_cap *= 2;
            if (buf_cap > 1024 * 1024) break;
            char *newbuf = (char *)realloc(buffer, buf_cap);
            if (!newbuf) break;
            buffer = newbuf;
        }
    }
    buffer[total_size] = '\0';

    /* Extract tag_name and html_url */
    WCHAR tag[64] = {0};
    WCHAR url[256] = {0};

    if (ExtractJsonString(buffer, "tag_name", tag, 64) &&
        ExtractJsonString(buffer, "html_url", url, 256)) {

        if (IsNewerVersion(tag, APP_VERSION)) {
            wcsncpy(s_latest_version, tag, 63);
            wcsncpy(s_release_url, url, 255);
            s_has_update = TRUE;
        }
    }

done:
    if (buffer) free(buffer);
    if (hRequest) WinHttpCloseHandle(hRequest);
    if (hConnect) WinHttpCloseHandle(hConnect);
    if (hSession) WinHttpCloseHandle(hSession);

    if (IsWindow(hwnd)) {
        PostMessage(hwnd, msg, 0, 0);
    }
    return 0;
}

void VersionCheck_Start(HWND notify_hwnd, UINT notify_msg)
{
    CheckContext *ctx = (CheckContext *)calloc(1, sizeof(CheckContext));
    if (!ctx) return;

    ctx->notify_hwnd = notify_hwnd;
    ctx->notify_msg = notify_msg;

    HANDLE hThread = CreateThread(NULL, 0, CheckThreadProc, ctx, 0, NULL);
    if (hThread) {
        CloseHandle(hThread);
    } else {
        free(ctx);
    }
}

BOOL VersionCheck_GetResult(WCHAR *version_out, int ver_len, WCHAR *url_out, int url_len)
{
    if (!s_has_update) return FALSE;

    if (version_out && ver_len > 0)
        wcsncpy(version_out, s_latest_version, ver_len - 1);
    if (url_out && url_len > 0)
        wcsncpy(url_out, s_release_url, url_len - 1);

    return TRUE;
}
