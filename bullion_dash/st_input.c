/*
 * Bullion Dash - Atari ST input (replaces the Amiga input.c): the same
 * API on the IKBD handler.  input_key() takes the Amiga raw key codes
 * the game uses (input.h, editor.c); they are mapped to ST scancodes.
 * Presses are latched by the IKBD handler and taken once per game step
 * (input_update), as main.c's loop read them once per frame.
 */
#include <string.h>
#include "st_ikbd.h"
#include "input.h"

/* Amiga raw key code -> ST scancode, for the keys the game reads */
static UBYTE st_code(int raw)
{
	switch (raw) {
	case 0x45: return 0x01;	/* Esc */
	case 0x40: return 0x39;	/* Space */
	case 0x44: return 0x1c;	/* Return */
	case 0x31: return 0x2c;	/* Z */
	case 0x32: return 0x2d;	/* X */
	case 0x12: return 0x12;	/* E */
	case 0x21: return 0x1f;	/* S */
	case 0x19: return 0x19;	/* P */
	case 0x50: return 0x3b;	/* F1 */
	case 0x51: return 0x3c;	/* F2 */
	case 0x52: return 0x3d;	/* F3 */
	case 0x4c: return 0x48;	/* cursor up */
	case 0x4d: return 0x50;	/* cursor down */
	case 0x4f: return 0x4b;	/* cursor left */
	case 0x4e: return 0x4d;	/* cursor right */
	case 0x0a: return 0x0b;	/* 0 */
	}
	if (raw >= 0x01 && raw <= 0x09)
		return (UBYTE)(raw + 1);	/* 1..9 */
	return 0;
}

static UBYTE pressed[128];		/* this step */

/* the ST scancodes of every key the game reads (st_code's targets) */
static const UBYTE used[] = {
	0x01, 0x39, 0x1c, 0x2c, 0x2d, 0x12, 0x1f, 0x19, 0x3b, 0x3c, 0x3d,
	0x48, 0x50, 0x4b, 0x4d, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
	0x09, 0x0a, 0x0b,
};
static int dx_val, dy_val, fire_val, prev_fire;

void input_init(struct Window *win) { (void)win; ikbd_init(IKBD_JOYSTICK); }
void input_exit(void) { ikbd_exit(); }

void input_update(void)
{
	volatile UBYTE *k = ikbd_keys;
	UBYTE j = ikbd_joy1();
	int i;

	for (i = 0; i < (int)sizeof(used); i++)
		pressed[used[i]] = (UBYTE)ikbd_key_hit(used[i]);

	prev_fire = fire_val;
	dx_val = (j & 0x04) ? -1 : (j & 0x08) ? 1 : 0;
	dy_val = (j & 0x01) ? -1 : (j & 0x02) ? 1 : 0;
	fire_val = (j & 0x80) != 0;
	if (k[SC_UP])    dy_val = -1;
	if (k[SC_DOWN])  dy_val = 1;
	if (k[SC_LEFT])  dx_val = -1;
	if (k[SC_RIGHT]) dx_val = 1;
	if (k[SC_SPACE] || k[SC_RETURN] || pressed[SC_SPACE] || pressed[SC_RETURN])
		fire_val = 1;
}

int input_dx(void) { return dx_val; }
int input_dy(void) { return dy_val; }
int input_fire(void) { return fire_val && !prev_fire; }
int input_fire_held(void) { return fire_val; }

int input_key(int rawkey)
{
	UBYTE c = st_code(rawkey);
	return c ? pressed[c] : 0;
}

int input_any_key(void)
{
	int i;
	for (i = 0; i < (int)sizeof(used); i++)
		if (pressed[used[i]])
			return 1;
	return 0;
}
