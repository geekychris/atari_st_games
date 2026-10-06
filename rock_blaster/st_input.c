/*
 * Rock Blaster - Atari ST input (replaces the Amiga input.c): the same
 * key map (cursor keys, A/Z left, D/C right, W thrust, Space/Alt fire,
 * Esc) and joystick port 1, from the shared IKBD handler.
 */
#include "st_ikbd.h"
#include "input.h"

#define SC_C 0x2e

void input_init(void)
{
	ikbd_init(IKBD_JOYSTICK);
}

void input_exit(void)
{
	ikbd_exit();
}

UWORD input_read(void)
{
	volatile UBYTE *k = ikbd_keys;
	UBYTE j = ikbd_joy1();
	UWORD r = 0;

	if (k[SC_LEFT] || k[SC_A] || k[SC_Z] || (j & 0x04))  r |= INPUT_LEFT;
	if (k[SC_RIGHT] || k[SC_D] || k[SC_C] || (j & 0x08)) r |= INPUT_RIGHT;
	if (k[SC_UP] || k[SC_W] || (j & 0x01))               r |= INPUT_UP;
	if (k[SC_SPACE] || k[SC_ALT] || (j & 0x80))          r |= INPUT_FIRE;
	if (k[SC_ESC])                                       r |= INPUT_ESC;
	return r;
}

/* IDCMP raw key hooks of the Amiga version: keys come from the IKBD */
void input_key_down(UWORD code) { (void)code; }
void input_key_up(UWORD code) { (void)code; }
void input_reset(void) { }
