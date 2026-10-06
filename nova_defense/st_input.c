/*
 * Nova Defense - Atari ST input (replaces the Amiga input.c):
 * keyboard (cursor keys / A,D, Space / Alt to fire, Esc), mouse
 * (horizontal movement, left button fires) and joystick port 1, all
 * from the shared IKBD handler (../st_port/st_ikbd.c).
 */
#include "st_ikbd.h"
#include "input.h"

static BOOL prev_fire;

void input_init(void)
{
	ikbd_init(IKBD_MOUSE_JOYSTICK);
	prev_fire = FALSE;
}

void input_exit(void)
{
	ikbd_exit();
}

void input_read(InputState *input)
{
	volatile UBYTE *k = ikbd_keys;
	UBYTE j = ikbd_joy1();
	WORD dx, dy;
	BOOL fire;

	input->left = k[SC_LEFT] || k[SC_A] || (j & 0x04);
	input->right = k[SC_RIGHT] || k[SC_D] || (j & 0x08);
	if (k[SC_ESC])
		input->quit = TRUE;

	ikbd_mouse_delta(&dx, &dy);
	input->mouse_dx = dx;

	fire = k[SC_SPACE] || k[SC_ALT] || (j & 0x80) || (ikbd_mouse_buttons() & 1);
	input->fire = fire;
	input->fire_pressed = fire && !prev_fire;
	prev_fire = fire;
}
