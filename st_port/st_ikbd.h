/*
 * Atari ST keyboard / joystick / mouse input for game ports.
 *
 * Takes over the keyboard ACIA interrupt (MFP vector $118) and decodes
 * the IKBD byte stream: make/break state for every key, joystick event
 * packets and relative mouse packets.  Needs supervisor mode.
 *
 * Lessons from testing on Hatari (all faithful to the hardware):
 *  - IKBD 0x14 (joystick events) turns the mouse off; 0x08 right after
 *    turns relative mouse reporting back on, giving both.
 *  - With the mouse on, joystick 1 fire is reported as the RIGHT mouse
 *    button (same signal line); st_ikbd folds that into joy1 fire.
 */
#ifndef ST_IKBD_H
#define ST_IKBD_H

#include "amiga_types.h"

enum {
	IKBD_JOYSTICK,		/* joystick events, mouse off */
	IKBD_MOUSE_JOYSTICK,	/* relative mouse + joystick 1 events */
};

void ikbd_init(int mode);
void ikbd_exit(void);		/* restores vector and GEM mouse mode */

/* key state by ST scancode, 1 = held */
extern volatile UBYTE ikbd_keys[128];

/* joystick port 1: ST bits up 1, down 2, left 4, right 8, fire 0x80 */
UBYTE ikbd_joy1(void);

/* Latched presses, for games that act on "pressed" rather than "held":
 * a tap shorter than one game frame is never lost.  Each call returns
 * and clears what happened since the previous call.
 */
int ikbd_key_hit(int scancode);		/* key pressed since last call */
UBYTE ikbd_joy1_hits(void);		/* joystick bits newly set since last call */

/* mouse: buttons (1 = left, 2 = right), movement since last call */
UBYTE ikbd_mouse_buttons(void);
void ikbd_mouse_delta(WORD *dx, WORD *dy);

/* ST scancodes used by the ports */
#define SC_ESC    0x01
#define SC_1      0x02
#define SC_2      0x03
#define SC_Q      0x10
#define SC_W      0x11
#define SC_P      0x19
#define SC_RETURN 0x1c
#define SC_CTRL   0x1d
#define SC_A      0x1e
#define SC_S      0x1f
#define SC_D      0x20
#define SC_L      0x26
#define SC_M      0x32
#define SC_LSHIFT 0x2a
#define SC_Z      0x2c
#define SC_X      0x2d
#define SC_ALT    0x38
#define SC_SPACE  0x39
#define SC_UP     0x48
#define SC_LEFT   0x4b
#define SC_RIGHT  0x4d
#define SC_DOWN   0x50

#endif
