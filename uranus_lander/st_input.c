/*
 * Uranus Lander - Atari ST input: keyboard + joystick port 1.
 *
 * TOS doesn't report key releases to programs, so like most ST games we
 * take over the keyboard ACIA interrupt (MFP vector $118) and decode the
 * IKBD byte stream ourselves: make/break scancodes for every key, and
 * joystick event packets.  The IKBD is switched to joystick event mode
 * (mouse off, so joystick 1 fire is reported as joystick fire, not as the
 * right mouse button).  Everything is restored on exit.
 *
 * Needs supervisor mode.  Same interface as the Amiga input.c.
 */
#include <osbind.h>
#include "input.h"

#define ACIA_VEC  ((void * volatile *)0x118)

static void *old_vec;
static volatile UBYTE keys[128];
static volatile UBYTE joy1;

/* IKBD packet decoder state */
static UBYTE pkt_header;
static WORD pkt_left, pkt_pos;
static UBYTE pkt[8];

/* called from the interrupt handler for every byte from the IKBD */
void ikbd_byte(UBYTE b);
void ikbd_byte(UBYTE b)
{
	if (pkt_left > 0)
	{
		pkt[pkt_pos++] = b;
		if (--pkt_left == 0 && pkt_header == 0xff)
			joy1 = pkt[0];
		return;
	}
	if (b >= 0xf6)
	{
		/* packet header: number of bytes that follow */
		static const UBYTE follow[10] = {
			7,	/* f6 status report */
			5,	/* f7 absolute mouse */
			2, 2, 2, 2,	/* f8-fb relative mouse */
			6,	/* fc time of day */
			2,	/* fd joystick report (both) */
			1, 1	/* fe/ff joystick 0/1 event */
		};
		pkt_header = b;
		pkt_left = follow[b - 0xf6];
		pkt_pos = 0;
		return;
	}
	keys[b & 0x7f] = !(b & 0x80);
}

/* ACIA interrupt: drain keyboard ACIA (and MIDI ACIA so a stray byte
 * there can't keep the shared interrupt line asserted), then signal
 * end of interrupt to the MFP (TOS uses software EOI).
 */
void acia_irq(void);
__asm__(
	"	.text\n"
	"	.globl	acia_irq\n"
	"acia_irq:\n"
	"	movem.l	%d0-%d1/%a0-%a1,-(%sp)\n"
	"1:	btst	#0,0xfffffc00.w\n"	/* keyboard ACIA: byte received? */
	"	beq.s	2f\n"
	"	moveq	#0,%d0\n"
	"	move.b	0xfffffc02.w,%d0\n"
	"	move.l	%d0,-(%sp)\n"
	"	jsr	ikbd_byte\n"
	"	addq.l	#4,%sp\n"
	"	bra.s	1b\n"
	"2:	btst	#0,0xfffffc04.w\n"	/* MIDI ACIA */
	"	beq.s	3f\n"
	"	tst.b	0xfffffc06.w\n"
	"	bra.s	2b\n"
	"3:	move.b	#0xbf,0xfffffa11.w\n"	/* clear MFP in-service bit 6 */
	"	movem.l	(%sp)+,%d0-%d1/%a0-%a1\n"
	"	rte\n"
);

static void ikbd_send(UBYTE cmd)
{
	char c = cmd;
	Ikbdws(0, &c);
}

void input_init(void)
{
	for (int i = 0; i < 128; i++)
		keys[i] = 0;
	joy1 = 0;
	pkt_left = 0;
	old_vec = *ACIA_VEC;
	*ACIA_VEC = (void *)acia_irq;
	ikbd_send(0x12);	/* mouse off */
	ikbd_send(0x14);	/* joystick event reporting */
}

void input_exit(void)
{
	ikbd_send(0x15);	/* joystick interrogation mode (no events) */
	ikbd_send(0x08);	/* relative mouse mode again for GEM */
	*ACIA_VEC = old_vec;
}

/* Amiga version API: keys are tracked by the interrupt handler */
void input_key_down(UWORD code) { (void)code; }
void input_key_up(UWORD code) { (void)code; }

void input_reset(void)
{
	for (int i = 0; i < 128; i++)
		keys[i] = 0;
}

/* ST scancodes */
#define SC_ESC    0x01
#define SC_W      0x11
#define SC_A      0x1e
#define SC_S      0x1f
#define SC_D      0x20
#define SC_M      0x32
#define SC_SPACE  0x39
#define SC_UP     0x48
#define SC_LEFT   0x4b
#define SC_RIGHT  0x4d
#define SC_DOWN   0x50

UWORD input_read(void)
{
	UWORD r = 0;
	UBYTE j = joy1;

	if (keys[SC_LEFT] || keys[SC_A])     r |= INPUT_LEFT;
	if (keys[SC_RIGHT] || keys[SC_D])    r |= INPUT_RIGHT;
	if (keys[SC_UP])                     r |= INPUT_UP;
	if (keys[SC_DOWN] || keys[SC_S])     r |= INPUT_DOWN;
	if (keys[SC_SPACE] || keys[SC_W])    r |= INPUT_THRUST;
	if (keys[SC_ESC])                    r |= INPUT_ESC;
	if (keys[SC_M])                      r |= INPUT_MUSIC;

	if (j & 0x01) r |= INPUT_UP;
	if (j & 0x02) r |= INPUT_DOWN;
	if (j & 0x04) r |= INPUT_LEFT;
	if (j & 0x08) r |= INPUT_RIGHT;
	if (j & 0x80) r |= INPUT_THRUST;
	return r;
}
