/*
 * Sky Knights - Atari ST input (replaces the Amiga input.c): player 1 on
 * the keyboard (cursor keys / A,D move, up / W / Space flap), player 2 on
 * joystick 1 (up or fire flaps), F1 / 1 and F2 / 2 start, Esc quits.
 */
#include "st_ikbd.h"
#include "input.h"

#define SC_F1 0x3b
#define SC_F2 0x3c

void input_init(void) { ikbd_init(IKBD_JOYSTICK); }
void input_exit(void) { ikbd_exit(); }

/* the Amiga RAWKEY handlers: the IKBD handler keeps the key state */
void input_key_down(UWORD code) { (void)code; }
void input_key_up(UWORD code) { (void)code; }
void input_reset(void) { }

void input_read(InputState *input)
{
	volatile UBYTE *k = ikbd_keys;
	UBYTE j = ikbd_joy1();

	input->p1 = 0;
	if (k[SC_LEFT] || k[SC_A]) input->p1 |= INP_LEFT;
	if (k[SC_RIGHT] || k[SC_D]) input->p1 |= INP_RIGHT;
	if (k[SC_UP] || k[SC_W] || k[SC_SPACE]) input->p1 |= INP_FLAP;

	input->sys = 0;
	if (k[SC_ESC]) input->sys |= INP_ESC;
	if (k[SC_F1] || k[SC_1]) input->sys |= INP_START1;
	if (k[SC_F2] || k[SC_2]) input->sys |= INP_START2;

	input->p2 = 0;
	if (j & 0x04) input->p2 |= INP_LEFT;
	if (j & 0x08) input->p2 |= INP_RIGHT;
	if (j & 0x81) input->p2 |= INP_FLAP;	/* up or fire */
}
