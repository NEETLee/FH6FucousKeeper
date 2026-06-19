/*
 * xbox_textentry.h - Submit text into the FH6/Xbox TextEntryPage popup via UIA
 *
 * The share-code entry popup is a separate UWP process (tcui-app.exe CoreWindow),
 * so PostMessage to the game window never reaches it. UI Automation can set the
 * edit value and invoke the confirm button cross-process without typing.
 */

#ifndef FOCUSKEEPER_XBOX_TEXTENTRY_H
#define FOCUSKEEPER_XBOX_TEXTENTRY_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Find the Xbox "TextEntryPage" share-code popup, set its edit field to `code`,
 * and invoke the confirm (确定/OK) button. Polls up to timeout_ms for the popup
 * to appear. Returns TRUE only when the value was set AND confirm was invoked.
 * Does not steal foreground focus.
 */
BOOL XboxTextEntry_SubmitShareCode(const char *code, int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* FOCUSKEEPER_XBOX_TEXTENTRY_H */
