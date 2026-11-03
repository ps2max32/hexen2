/* in_ps2.c: the input, pad 0. The buttons are keys: in the menus and the
   console the d-pad is the arrows, cross Enter, circle Escape; in the game
   most buttons are AUX keys with Hexen II's actions bound to them
   (IN_PS2_Binds, only where the key has no binding yet: config.cfg keeps
   the player's own). The left stick moves, the right one looks. */

#include <mx/mx.h>
#include <mx/pad.h>
#include <mxgl.h>

#include "quakedef.h"

static int pad_ok;

/* a button's key in the game and in the menus (0: the game's) */
static const struct {
	u32 bit;
	int game, menu;
} pad_keys[] = {
	{MX_PAD_UP, K_UPARROW, 0},
	{MX_PAD_DOWN, K_DOWNARROW, 0},
	{MX_PAD_LEFT, K_LEFTARROW, 0},
	{MX_PAD_RIGHT, K_RIGHTARROW, 0},
	{MX_PAD_R1, K_CTRL, 0},
	{MX_PAD_CROSS, K_AUX1, K_ENTER},
	{MX_PAD_CIRCLE, K_AUX2, K_ESCAPE},
	{MX_PAD_SQUARE, K_AUX3, K_BACKSPACE},
	{MX_PAD_TRIANGLE, K_AUX4, 0},
	{MX_PAD_L1, K_AUX5, 0},
	{MX_PAD_L2, K_AUX6, 0},
	{MX_PAD_R2, K_AUX7, 0},
	{MX_PAD_L3, K_AUX8, 0},
	{MX_PAD_START, K_ESCAPE, 0},
	{MX_PAD_SELECT, K_TAB, 0},
};
#define NUM_KEYS (sizeof pad_keys / sizeof pad_keys[0])
static int pad_down[NUM_KEYS]; /* the key a held button sent */

/* the game's actions on the AUX keys */
static const struct {
	int key;
	const char *cmd;
} pad_binds[] = {
	{K_AUX1, "+jump"},     {K_AUX2, "+crouch"},  {K_AUX3, "invuse"},
	{K_AUX4, "impulse 10"}, {K_AUX5, "invleft"}, {K_AUX6, "invright"},
	{K_AUX7, "+speed"},    {K_AUX8, "centerview"},
};

static float stick[4]; /* right x, y, left x, y: -1 .. 1 */

void IN_Init(void) {
	pad_ok = mx_pad_init() >= 0;
	Con_Printf("pad: %s\n", pad_ok ? "ok" : "no driver");
}

/* after the configs (main, after Host_Init) */
void IN_PS2_Binds(void) {
	for (unsigned i = 0; i < sizeof pad_binds / sizeof pad_binds[0]; i++) {
		int k = pad_binds[i].key;
		if (!keybindings[k] || !keybindings[k][0])
			Key_SetBinding(k, pad_binds[i].cmd);
	}
}

void IN_ReInit(void) {}
void IN_Shutdown(void) {}
void IN_Commands(void) {}
void IN_SendKeyEvents(void) {}
void IN_ActivateMouse(void) {}
void IN_DeactivateMouse(void) {}
void IN_ShowMouse(void) {}
void IN_HideMouse(void) {}

void IN_ClearStates(void) {
	memset(stick, 0, sizeof stick);
}

void Sys_SendKeyEvents(void) {
	if (!pad_ok)
		return;
	static u32 last;
	u32 held = mx_pad_buttons(0);
	int menu = Key_GetDest() != key_game;
	for (unsigned i = 0; i < NUM_KEYS; i++) {
		u32 b = pad_keys[i].bit;
		if (!((held ^ last) & b))
			continue;
		if (held & b) {
			pad_down[i] = menu && pad_keys[i].menu ? pad_keys[i].menu : pad_keys[i].game;
			Key_Event(pad_down[i], true);
		} else if (pad_down[i]) {
			Key_Event(pad_down[i], false);
			pad_down[i] = 0;
		}
	}
	/* R3 (the right stick pressed): the counters on and off */
	if (held & ~last & MX_PAD_R3)
		mxgl_show_stats = !mxgl_show_stats;
	last = held;
	u8 st[4];
	if (mx_pad_sticks(0, st))
		for (int k = 0; k < 4; k++) {
			int v = (int)st[k] - 128;
			v = v > -32 && v < 32 ? 0 : v > 0 ? v - 32 : v + 32;
			stick[k] = v / 96.0f;
		}
}

/* the left stick moves, the right one turns (180 degrees a second at full
   turn) and looks up and down */
void IN_Move(usercmd_t *cmd) {
	if (cl.v.cameramode) {
		/* stuck in a different camera so don't move */
		memset(cmd, 0, sizeof(*cmd));
		return;
	}
	if (Key_GetDest() != key_game)
		return;
	cmd->forwardmove -= stick[3] * cl_forwardspeed.value;
	cmd->sidemove += stick[2] * cl_sidespeed.value;
	cl.viewangles[YAW] -= stick[0] * 180.0f * host_frametime;
	cl.viewangles[PITCH] += stick[1] * 135.0f * host_frametime;
	if (stick[1])
		V_StopPitchDrift();
	if (cl.viewangles[PITCH] > 80)
		cl.viewangles[PITCH] = 80;
	if (cl.viewangles[PITCH] < -70)
		cl.viewangles[PITCH] = -70;
}
