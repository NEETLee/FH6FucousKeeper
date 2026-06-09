#ifndef FOCUSKEEPER_VERSION_CHECK_H
#define FOCUSKEEPER_VERSION_CHECK_H

#include <windows.h>

/*
 * Version Check - Asynchronous GitHub Release checker.
 *
 * Queries the GitHub API for the latest release tag in a background
 * thread. Notifies via callback when a newer version is found.
 */

#define APP_VERSION L"1.2.1"

typedef void (*VersionCheckCallback)(const WCHAR *latest_version, const WCHAR *release_url, void *user_data);

/* Start a background check (non-blocking). Callback fires on GUI thread via PostMessage. */
void VersionCheck_Start(HWND notify_hwnd, UINT notify_msg);

/* Call from WM handler: returns TRUE if new version available */
BOOL VersionCheck_GetResult(WCHAR *version_out, int ver_len, WCHAR *url_out, int url_len);

#endif /* FOCUSKEEPER_VERSION_CHECK_H */
