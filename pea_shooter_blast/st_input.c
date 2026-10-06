/*
 * Pea Shooter Blast - Atari ST input (replaces the Amiga input.c): the
 * same keys (cursor keys or A/D move, up / W jump, down / Z, Space / Alt
 * fire, Esc) and joystick 1, from the IKBD handler.
 */
#include "st_ikbd.h"
#include "input.h"

void input_init(void) { ikbd_init(IKBD_JOYSTICK); }
void input_exit(void) { ikbd_exit(); }

/* the Amiga RAWKEY handlers: unused, the IKBD handler keeps the keys */
void input_key_down(UWORD code) { (void)code; }
void input_key_up(UWORD code) { (void)code; }
void input_reset(void) { }

UWORD input_read(void)
{
	volatile UBYTE *k = ikbd_keys;
	UBYTE j = ikbd_joy1();
	UWORD r = 0;

	if (k[SC_LEFT] || k[SC_A] || (j & 0x04)) r |= INPUT_LEFT;
	if (k[SC_RIGHT] || k[SC_D] || (j & 0x08)) r |= INPUT_RIGHT;
	if (k[SC_UP] || k[SC_W] || (j & 0x01)) r |= INPUT_JUMP;
	if (k[SC_DOWN] || k[SC_Z] || (j & 0x02)) r |= INPUT_DOWN;
	if (k[SC_SPACE] || k[SC_ALT] || (j & 0x80)) r |= INPUT_FIRE;
	if (k[SC_ESC]) r |= INPUT_ESC;
	return r;
}
