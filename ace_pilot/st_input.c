/*
 * Ace Pilot - Atari ST input (replaces the Amiga input.c): the same keys
 * (cursor keys / WASD, Space / Alt fire, Q / E throttle, P display mode,
 * F1 / F2 one or two players, Esc), joystick 1 for player 1 and joystick
 * 0 (the mouse port) for player 2, from the IKBD handler.
 */
#include "st_ikbd.h"
#include "input.h"

#define SC_E  0x12
#define SC_F1 0x3b
#define SC_F2 0x3c

void input_init(void) { ikbd_init(IKBD_JOYSTICK); }
void input_exit(void) { ikbd_exit(); }

/* the Amiga RAWKEY handlers: the IKBD handler keeps the key state */
void input_key_down(UWORD code) { (void)code; }
void input_key_up(UWORD code) { (void)code; }
void input_reset(void) { }

static UWORD joy_bits(UBYTE j)
{
	UWORD r = 0;
	if (j & 0x01) r |= INPUT_UP;
	if (j & 0x02) r |= INPUT_DOWN;
	if (j & 0x04) r |= INPUT_LEFT;
	if (j & 0x08) r |= INPUT_RIGHT;
	if (j & 0x80) r |= INPUT_FIRE;
	return r;
}

UWORD input_read_p1(void)
{
	volatile UBYTE *k = ikbd_keys;
	UWORD r = joy_bits(ikbd_joy1());

	if (k[SC_LEFT] || k[SC_A]) r |= INPUT_LEFT;
	if (k[SC_RIGHT] || k[SC_D]) r |= INPUT_RIGHT;
	if (k[SC_UP] || k[SC_W]) r |= INPUT_UP;
	if (k[SC_DOWN] || k[SC_S]) r |= INPUT_DOWN;
	if (k[SC_SPACE] || k[SC_ALT]) r |= INPUT_FIRE;
	if (k[SC_ESC]) r |= INPUT_ESC;
	if (k[SC_Q]) r |= INPUT_THROT_UP;
	if (k[SC_E]) r |= INPUT_THROT_DN;
	if (k[SC_P]) r |= INPUT_MODE;
	if (k[SC_F1]) r |= INPUT_START;
	if (k[SC_F2]) r |= INPUT_TWO_P;
	return r;
}

UWORD input_read_p2(void)
{
	return joy_bits(ikbd_joy0());
}
