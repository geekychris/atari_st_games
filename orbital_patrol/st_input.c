/*
 * Orbital Patrol - Atari ST input (replaces the Amiga input.c, which
 * installs an input.device handler): the same key map and edge-triggered
 * smart bomb / hyperspace, from the shared IKBD handler and joystick 1.
 */
#include "st_ikbd.h"
#include "game.h"
#include "input.h"

#define SC_H      0x23
#define SC_RSHIFT 0x36

static WORD bomb_edge, hyper_edge;

void input_init(InputData *id)
{
	(void)id;
	bomb_edge = hyper_edge = 0;
	ikbd_init(IKBD_JOYSTICK);
}

void input_key_event(InputData *id, UWORD code)
{
	(void)id; (void)code;		/* keys come from the IKBD handler */
}

void input_cleanup(void)
{
	ikbd_exit();
}

WORD input_read(InputData *id)
{
	volatile UBYTE *k = ikbd_keys;
	UBYTE j = ikbd_joy1();
	WORD result = 0;
	(void)id;

	if (k[SC_LEFT]  || k[SC_A] || (j & 0x04)) result |= INPUT_LEFT;
	if (k[SC_RIGHT] || k[SC_D] || (j & 0x08)) result |= INPUT_RIGHT;
	if (k[SC_UP]    || k[SC_W] || (j & 0x01)) result |= INPUT_UP;
	if (k[SC_DOWN]  || k[SC_S] || (j & 0x02)) result |= INPUT_DOWN;
	if (k[SC_SPACE] || k[SC_ALT] || k[SC_LSHIFT] || k[SC_RSHIFT])
		result |= INPUT_FIRE;
	if (k[SC_ESC])                    result |= INPUT_QUIT;
	if (k[SC_RETURN] || k[SC_SPACE])  result |= INPUT_START;

	/* Smart bomb: edge-triggered */
	{
		WORD bomb_held = k[SC_Z];
		if (bomb_held && !bomb_edge) result |= INPUT_BOMB;
		bomb_edge = bomb_held;
	}
	/* Hyperspace: edge-triggered */
	{
		WORD hyper_held = k[SC_H] || k[SC_X];
		if (hyper_held && !hyper_edge) result |= INPUT_HYPER;
		hyper_edge = hyper_held;
	}
	/* joystick fire also starts, as on the Amiga */
	if (j & 0x80)
		result |= INPUT_FIRE | INPUT_START;
	return result;
}
