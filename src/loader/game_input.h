/*
 * game_input.h - Unified background input for FH6
 *
 * Provides game_click (hover + enter), game_hover, hw_press, hw_hold
 * all via PostMessage - works in background without stealing focus.
 *
 * Coordinate system: client coordinates = WGC capture pixel coordinates.
 */

#ifndef FOCUSKEEPER_GAME_INPUT_H
#define FOCUSKEEPER_GAME_INPUT_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GameInput GameInput;

/* Create/destroy */
GameInput* GameInput_Create(void);
void       GameInput_Destroy(GameInput *gi);

/* Initialize with game window handle */
BOOL GameInput_Init(GameInput *gi, HWND game_hwnd);
void GameInput_Shutdown(GameInput *gi);
BOOL GameInput_IsReady(GameInput *gi);
HWND GameInput_GetHwnd(GameInput *gi);

/*
 * game_click: hover to (x,y) then press Enter.
 * This is the background equivalent of FH6Auto's game_click(pos).
 * Activates the UI element at the given client coordinate.
 */
void GameInput_Click(GameInput *gi, int x, int y);

/*
 * game_click with custom confirm key (e.g. VK_SPACE instead of VK_RETURN)
 */
void GameInput_ClickKey(GameInput *gi, int x, int y, DWORD confirm_vk);

/*
 * game_hover: move mouse cursor to (x,y) without clicking.
 * Updates the game's hover/focus highlight.
 */
void GameInput_Hover(GameInput *gi, int x, int y);

/*
 * hw_press: press and release a key (background PostMessage).
 * delay_ms: hold duration in ms (default ~80).
 */
void GameInput_Press(GameInput *gi, DWORD vk, int delay_ms);

/*
 * hw_key_down / hw_key_up: for hold behaviors (e.g. holding W during race).
 */
void GameInput_KeyDown(GameInput *gi, DWORD vk);
void GameInput_KeyUp(GameInput *gi, DWORD vk);

/* Release all held keys */
void GameInput_ReleaseAll(GameInput *gi);

/*
 * Move mouse away to a safe corner to avoid hover tooltips.
 * Equivalent to FH6Auto's "move to (gx+5, gy+5)" after click.
 */
void GameInput_MoveAway(GameInput *gi);

/*
 * Real mouse click via PostMessage (WM_LBUTTONDOWN/UP with MK_LBUTTON).
 * Tests whether background mouse clicks activate the element under cursor.
 */
void GameInput_MouseClick(GameInput *gi, int x, int y);
void GameInput_MouseDoubleClick(GameInput *gi, int x, int y);

#ifdef __cplusplus
}
#endif

#endif /* FOCUSKEEPER_GAME_INPUT_H */
