/*
 * Uranus Lander - Atari ST input: key/joystick mapping on top of the
 * shared IKBD handler (../st_port/st_ikbd.c).  Same interface as the
 * Amiga input.c.
 */
#include "st_ikbd.h"
#include "input.h"
#include "st_sound.h"

void input_init(void) { ikbd_init(IKBD_JOYSTICK); }
void input_exit(void) { ikbd_exit(); }

/* Amiga version API: keys are tracked by the interrupt handler */
void input_key_down(UWORD code) { (void)code; }
void input_key_up(UWORD code) { (void)code; }
void input_reset(void) { }

UWORD input_read(void)
{
	volatile UBYTE *k = ikbd_keys;
	UBYTE j = ikbd_joy1();
	UWORD r = 0;

	if (k[SC_LEFT] || k[SC_A])     r |= INPUT_LEFT;
	if (k[SC_RIGHT] || k[SC_D])    r |= INPUT_RIGHT;
	if (k[SC_UP])                  r |= INPUT_UP;
	if (k[SC_DOWN] || k[SC_S])     r |= INPUT_DOWN;
	if (k[SC_SPACE] || k[SC_W])    r |= INPUT_THRUST;
	if (k[SC_ESC])                 r |= INPUT_ESC;
	if (k[SC_M])                   r |= INPUT_MUSIC;

	if (j & 0x01) r |= INPUT_UP;
	if (j & 0x02) r |= INPUT_DOWN;
	if (j & 0x04) r |= INPUT_LEFT;
	if (j & 0x08) r |= INPUT_RIGHT;
	if (j & 0x80) r |= INPUT_THRUST;
	return r;
}
