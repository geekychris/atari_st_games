/*
 * Dot Chase - Atari ST input (replaces the Amiga input.c): the same keys
 * (cursor keys / WASD, Space / Return / F1 start, Esc) and joystick 1,
 * from the shared IKBD handler.
 */
#include "st_ikbd.h"
#include "input.h"

#define SC_F1 0x3b

void input_init(void) { ikbd_init(IKBD_JOYSTICK); }
void input_exit(void) { ikbd_exit(); }
void input_key_down(UWORD code) { (void)code; }
void input_key_up(UWORD code) { (void)code; }
void input_reset(void) { }

void input_read(InputState *input)
{
	volatile UBYTE *k = ikbd_keys;
	UBYTE j = ikbd_joy1();

	input->dir = DIR_NONE;
	if (k[SC_UP]    || k[SC_W] || (j & 0x01)) input->dir = DIR_UP;
	if (k[SC_DOWN]  || k[SC_S] || (j & 0x02)) input->dir = DIR_DOWN;
	if (k[SC_LEFT]  || k[SC_A] || (j & 0x04)) input->dir = DIR_LEFT;
	if (k[SC_RIGHT] || k[SC_D] || (j & 0x08)) input->dir = DIR_RIGHT;
	/* held, or tapped since the last read (frames can be longer than a
	 * short key press) */
	input->start = (k[SC_SPACE] || k[SC_RETURN] || k[SC_F1] || (j & 0x80) |
	                ikbd_key_hit(SC_SPACE) | ikbd_key_hit(SC_RETURN) |
	                ikbd_key_hit(SC_F1) | (ikbd_joy1_hits() & 0x80)) ? 1 : 0;
	input->quit = k[SC_ESC] ? 1 : 0;
}
