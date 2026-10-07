/*
 * RJ's Birthday Bash - Atari ST input (replaces the Amiga input.c): the
 * same keys and joystick 1 from the IKBD handler.  Name entry gets the
 * typed character (US layout, as the Amiga version maps it) and the
 * edges of up, down, Return, Backspace and Delete.
 */
#include "st_ikbd.h"
#include "game.h"

#define SC_BACKSPACE 0x0e
#define SC_DELETE    0x53

void input_init(void) { ikbd_init(IKBD_JOYSTICK); }
void input_exit(void) { ikbd_exit(); }

/* ST scancode -> ASCII for the keys the Amiga version maps (unshifted) */
static UBYTE st_ascii(int sc)
{
	static const char row0[] = "1234567890-=";	/* 0x02-0x0d */
	static const char row1[] = "qwertyuiop[]";	/* 0x10-0x1b */
	static const char row2[] = "asdfghjkl;'";	/* 0x1e-0x28 */
	static const char row3[] = "zxcvbnm,./";	/* 0x2c-0x35 */
	if (sc >= 0x02 && sc <= 0x0d) return row0[sc - 0x02];
	if (sc >= 0x10 && sc <= 0x1b) return row1[sc - 0x10];
	if (sc >= 0x1e && sc <= 0x28) return row2[sc - 0x1e];
	if (sc >= 0x2c && sc <= 0x35) return row3[sc - 0x2c];
	if (sc == 0x39) return ' ';
	return 0;
}

void input_read(InputState *inp, struct Window *win)
{
	volatile UBYTE *k = ikbd_keys;
	UBYTE j = ikbd_joy1();
	UWORD prev = inp->prev_fire;
	(void)win;

	inp->key_up = (UBYTE)ikbd_key_hit(SC_UP);
	inp->key_down = (UBYTE)ikbd_key_hit(SC_DOWN);
	inp->key_return = (UBYTE)ikbd_key_hit(SC_RETURN);
	inp->key_backspace = (UBYTE)ikbd_key_hit(SC_BACKSPACE);
	inp->key_delete = (UBYTE)ikbd_key_hit(SC_DELETE);
	inp->last_char = st_ascii(ikbd_last_hit());

	inp->bits = 0;
	if (k[SC_LEFT] || (j & 0x04)) inp->bits |= INP_LEFT;
	if (k[SC_RIGHT] || (j & 0x08)) inp->bits |= INP_RIGHT;
	if (k[SC_UP] || (j & 0x01)) inp->bits |= INP_UP;
	if (k[SC_DOWN] || (j & 0x02)) inp->bits |= INP_DOWN;
	if (k[SC_SPACE] || k[SC_ALT] || (j & 0x80)) inp->bits |= INP_FIRE;
	if (k[SC_ESC]) inp->bits |= INP_ESC;

	/* Edge detect fire */
	inp->fire_edge = (inp->bits & INP_FIRE) && !prev;
	inp->prev_fire = (inp->bits & INP_FIRE) ? 1 : 0;
}
