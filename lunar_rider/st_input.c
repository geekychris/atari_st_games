/*
 * Lunar Rider - Atari ST input (replaces the Amiga input.c): the same
 * keys (cursor left/right / D speed, up / W / Space jump, A fire, Esc)
 * and joystick 1 (up = jump, fire = shoot), from the IKBD handler.
 */
#include "st_ikbd.h"
#include "input.h"

void input_init(struct Window *win) { (void)win; ikbd_init(IKBD_JOYSTICK); }
void input_exit(void) { ikbd_exit(); }

void input_update(InputState *inp)
{
	volatile UBYTE *k = ikbd_keys;
	UBYTE j = ikbd_joy1();

	inp->left  = k[SC_LEFT] || (j & 0x04);
	inp->right = k[SC_RIGHT] || k[SC_D] || (j & 0x08);
	inp->jump  = k[SC_UP] || k[SC_SPACE] || k[SC_W] || (j & 0x01);
	inp->fire  = k[SC_A] || (j & 0x80);
	inp->quit  = k[SC_ESC];
}
