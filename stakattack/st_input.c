/*
 * StakAttack - Atari ST input (replaces the Amiga input.c): the same key
 * map (cursor keys / WASD, Space, Alt / Return start, P pause, Esc) and
 * joystick 1 (fire = drop/start), from the shared IKBD handler.
 */
#include "st_ikbd.h"
#include "input.h"

static UWORD prev_input = 0;

void input_init(void) { ikbd_init(IKBD_JOYSTICK); }
void input_cleanup(void) { ikbd_exit(); }
void input_handle_key(UWORD code, UWORD qualifier) { (void)code; (void)qualifier; }

UWORD input_read(void)
{
	volatile UBYTE *k = ikbd_keys;
	UBYTE j = ikbd_joy1();
	UWORD r = 0;

	if (k[SC_LEFT]  || k[SC_A] || (j & 0x04)) r |= INPUT_LEFT;
	if (k[SC_RIGHT] || k[SC_D] || (j & 0x08)) r |= INPUT_RIGHT;
	if (k[SC_DOWN]  || k[SC_S] || (j & 0x02)) r |= INPUT_DOWN;
	if (k[SC_UP]    || k[SC_W] || (j & 0x01)) r |= INPUT_UP;
	if (k[SC_SPACE])                          r |= INPUT_FIRE;
	if (k[SC_ALT] || k[SC_RETURN])            r |= INPUT_START;
	if (k[SC_ESC])                            r |= INPUT_ESC;
	if (k[SC_P])                              r |= INPUT_PAUSE;
	if (j & 0x80)                             r |= INPUT_FIRE | INPUT_START;
	return r;
}

UWORD input_edge(UWORD current)
{
	UWORD edges = current & ~prev_input;
	prev_input = current;
	return edges;
}
