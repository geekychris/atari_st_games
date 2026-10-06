/*
 * Atari ST keyboard / joystick / mouse input, see st_ikbd.h.
 */
#include <osbind.h>
#include "st_ikbd.h"

#define ACIA_VEC  ((void * volatile *)0x118)

volatile UBYTE ikbd_keys[128];
static volatile UBYTE key_hits[128];
static volatile UBYTE joy1, joy1_hits, mouse_buttons;
static volatile WORD mouse_dx, mouse_dy;
static void *old_vec;

static UBYTE pkt_header;
static WORD pkt_left, pkt_pos;
static UBYTE pkt[8];

void ikbd_byte(UBYTE b);
void ikbd_byte(UBYTE b)
{
	if (pkt_left > 0)
	{
		pkt[pkt_pos++] = b;
		if (--pkt_left)
			return;
		if (pkt_header == 0xff || pkt_header == 0xfd)
		{
			UBYTE j = pkt_header == 0xff ? pkt[0] : pkt[1];
			joy1_hits |= j & ~joy1;
			joy1 = j;
		}
		else if (pkt_header >= 0xf8 && pkt_header <= 0xfb)
		{
			mouse_buttons = ((pkt_header & 2) ? 1 : 0) | ((pkt_header & 1) ? 2 : 0);
			mouse_dx += (signed char)pkt[0];
			mouse_dy += (signed char)pkt[1];
		}
		return;
	}
	if (b >= 0xf6)
	{
		static const UBYTE follow[10] = { 7, 5, 2, 2, 2, 2, 6, 2, 1, 1 };
		pkt_header = b;
		pkt_left = follow[b - 0xf6];
		pkt_pos = 0;
		return;
	}
	ikbd_keys[b & 0x7f] = !(b & 0x80);
	if (!(b & 0x80))
		key_hits[b] = 1;
}

/* keyboard ACIA interrupt (drains MIDI ACIA too, software EOI) */
void ikbd_irq(void);
__asm__(
	"	.text\n"
	"	.globl	ikbd_irq\n"
	"ikbd_irq:\n"
	"	movem.l	%d0-%d1/%a0-%a1,-(%sp)\n"
	"1:	btst	#0,0xfffffc00.w\n"
	"	beq.s	2f\n"
	"	moveq	#0,%d0\n"
	"	move.b	0xfffffc02.w,%d0\n"
	"	move.l	%d0,-(%sp)\n"
	"	jsr	ikbd_byte\n"
	"	addq.l	#4,%sp\n"
	"	bra.s	1b\n"
	"2:	btst	#0,0xfffffc04.w\n"
	"	beq.s	3f\n"
	"	tst.b	0xfffffc06.w\n"
	"	bra.s	2b\n"
	"3:	move.b	#0xbf,0xfffffa11.w\n"
	"	movem.l	(%sp)+,%d0-%d1/%a0-%a1\n"
	"	rte\n"
);

static void ikbd_send(UBYTE cmd)
{
	char c = cmd;
	Ikbdws(0, &c);
}

void ikbd_init(int mode)
{
	for (int i = 0; i < 128; i++)
		ikbd_keys[i] = key_hits[i] = 0;
	joy1 = joy1_hits = mouse_buttons = 0;
	mouse_dx = mouse_dy = 0;
	pkt_left = 0;
	old_vec = *ACIA_VEC;
	*ACIA_VEC = (void *)ikbd_irq;
	if (mode == IKBD_JOYSTICK)
	{
		ikbd_send(0x12);	/* mouse off */
		ikbd_send(0x14);	/* joystick events */
	}
	else
	{
		ikbd_send(0x14);	/* joystick events (turns mouse off)... */
		ikbd_send(0x08);	/* ...relative mouse back on */
	}
}

void ikbd_exit(void)
{
	ikbd_send(0x15);	/* stop joystick events */
	ikbd_send(0x08);	/* relative mouse for GEM */
	*ACIA_VEC = old_vec;
}

UBYTE ikbd_joy1(void)
{
	/* fire as right mouse button when the mouse is on, see header */
	return joy1 | ((mouse_buttons & 2) ? 0x80 : 0);
}

int ikbd_key_hit(int sc)
{
	int hit = key_hits[sc & 0x7f];
	key_hits[sc & 0x7f] = 0;
	return hit;
}

UBYTE ikbd_joy1_hits(void)
{
	UBYTE h = joy1_hits;
	joy1_hits &= ~h;
	return h;
}

UBYTE ikbd_mouse_buttons(void)
{
	return mouse_buttons;
}

void ikbd_mouse_delta(WORD *dx, WORD *dy)
{
	WORD x, y;
	/* interrupt can update both between reads; fine for a game */
	x = mouse_dx;
	y = mouse_dy;
	mouse_dx -= x;
	mouse_dy -= y;
	*dx = x;
	*dy = y;
}
