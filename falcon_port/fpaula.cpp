/*
 * Amiga Paula on the Atari Falcon's DMA sound, see fpaula.h.
 */
#include <osbind.h>
#include <mint/falcon.h>
#include <string.h>
#include "fpaula.h"

struct Custom custom;

#define RATE     9834			/* CLK10K */
#define FRAMES   2048			/* ring buffer, stereo 8 bit frames */
#define AHEAD    512			/* mix this far ahead of playback */
#define PAULA_HZ 3546895UL		/* PAL Paula clock */

struct voice {
	const signed char *ptr;
	ULONG len;			/* bytes */
	ULONG pos;			/* 16.16 */
	int on;
	int silent;			/* short all-zero block (one-shot end) */
};

static struct voice v[4];
static UWORD enabled;
static signed char *ring;
static void *ring_mem;
static ULONG wpos;			/* next frame to mix */
static ULONG step_k;			/* (PAULA_HZ << 16) / RATE */
static void (*tick_fn)(void);
static int tick_hz, vbl_hz, tick_acc;
static volatile int running;

UWORD paula_lock(void)
{
	UWORD sr;
	__asm__ volatile ("move.w %%sr,%0\n\tor.w #0x0700,%%sr" : "=d"(sr) : : "memory");
	return sr;
}

void paula_unlock(UWORD sr)
{
	__asm__ volatile ("move.w %0,%%sr" : : "d"(sr) : "memory");
}

static void latch(int ch)
{
	v[ch].ptr = (const signed char *)custom.aud[ch].ac_ptr;
	v[ch].len = (ULONG)custom.aud[ch].ac_len * 2;
	v[ch].pos = 0;
	v[ch].on = v[ch].ptr && v[ch].len;
	/* players end one-shot samples by pointing Paula at a few bytes of
	 * zeros; looping over those every few frames is wasted work */
	v[ch].silent = 0;
	if (v[ch].on && v[ch].len <= 16)
	{
		ULONG i;
		for (i = 0; i < v[ch].len && !v[ch].ptr[i]; i++)
			;
		v[ch].silent = i == v[ch].len;
	}
}

void paula_dmacon(UWORD val)
{
	UWORD sr = paula_lock();
	UWORD bits = val & 0x0f;
	if (val & 0x8000)
	{
		UWORD start = bits & ~enabled;
		enabled |= bits;
		for (int ch = 0; ch < 4; ch++)
			if (start & (1 << ch))
				latch(ch);
	}
	else
	{
		enabled &= ~bits;
		for (int ch = 0; ch < 4; ch++)
			if (bits & (1 << ch))
				v[ch].on = 0;
	}
	paula_unlock(sr);
}

/*
 * Mixing goes straight into the 8 bit stereo ring: each side sums two
 * channels pre-scaled by a volume table to 7 bits ((sample * vol) >> 7),
 * the first channel stored, the second added.  The inner loop is the
 * classic 68k one: integer and fraction position stepped with add/addx.
 */
static signed char voltab[65][256];

/* n frames from src at pos (16.16) into every other byte of dst;
 * add = 0 stores, 1 adds.  Returns the new position. */
extern "C" ULONG paula_run(signed char *dst, int n, const signed char *src,
			   ULONG pos, ULONG inc, const signed char *vt, int add);
__asm__(
	"	.text\n"
	"	.globl	paula_run\n"
	"paula_run:\n"
	"	movem.l	%d2-%d6/%a2,-(%sp)\n"
	"	move.l	28(%sp),%a0\n"		/* dst */
	"	move.l	32(%sp),%d0\n"		/* n */
	"	move.l	36(%sp),%a1\n"		/* src */
	"	move.l	40(%sp),%d1\n"		/* pos */
	"	move.l	44(%sp),%d4\n"		/* inc: d4.w = fraction */
	"	move.l	48(%sp),%a2\n"		/* vt */
	"	move.w	%d1,%d3\n"		/* d3.w = position fraction */
	"	swap	%d1\n"			/* d1.w = position integer */
	"	move.l	%d4,%d5\n"
	"	swap	%d5\n"			/* d5.w = inc integer */
	"	moveq	#0,%d2\n"
	"	subq.l	#1,%d0\n"
	"	bmi.s	9f\n"
	"	tst.l	52(%sp)\n"
	"	bne.s	2f\n"
	"1:	move.b	(%a1,%d1.w),%d2\n"
	"	move.b	(%a2,%d2.w),(%a0)\n"
	"	addq.l	#2,%a0\n"
	"	add.w	%d4,%d3\n"
	"	addx.w	%d5,%d1\n"
	"	dbra	%d0,1b\n"
	"	bra.s	9f\n"
	"2:	move.b	(%a1,%d1.w),%d2\n"
	"	move.b	(%a2,%d2.w),%d6\n"
	"	add.b	%d6,(%a0)\n"
	"	addq.l	#2,%a0\n"
	"	add.w	%d4,%d3\n"
	"	addx.w	%d5,%d1\n"
	"	dbra	%d0,2b\n"
	"9:	swap	%d1\n"
	"	move.w	%d3,%d1\n"
	"	move.l	%d1,%d0\n"
	"	movem.l	(%sp)+,%d2-%d6/%a2\n"
	"	rts\n"
);

/* channel ch into one side of the ring from frame f, n frames.
 * add = 0: store (silence where the channel is quiet), 1: add */
static void mix_voice(int ch, signed char *dst, int n, int add)
{
	struct voice *c = &v[ch];
	UWORD per = custom.aud[ch].ac_per;
	int vol = custom.aud[ch].ac_vol;
	if (vol > 64)
		vol = 64;
	ULONG inc = per >= 64 ? step_k / per : 0;
	if (c->on && c->silent
	    && (const signed char *)custom.aud[ch].ac_ptr != c->ptr)
		latch(ch);	/* registers changed while looping silence */
	while (n > 0)
	{
		if (!c->on || !inc || c->silent)
		{
			if (!add)
				for (int i = 0; i < n; i++)
					dst[i * 2] = 0;
			return;
		}
		ULONG end = c->len << 16;
		ULONG left = (end - c->pos + inc - 1) / inc;	/* frames to block end */
		int k = left < (ULONG)n ? (int)left : n;
		if (vol || !add)
			c->pos = paula_run(dst, k, c->ptr, c->pos, inc, voltab[vol], add);
		else
			c->pos += inc * k;
		dst += 2 * k;
		n -= k;
		if (c->pos >= end)
			latch(ch);	/* block done: reload, as Paula does */
	}
}

static void mix(ULONG from, int n)
{
	signed char *l = ring + from * 2;
	mix_voice(0, l, n, 0);
	mix_voice(3, l, n, 1);
	mix_voice(1, l + 1, n, 0);
	mix_voice(2, l + 1, n, 1);
}

static ULONG play_frame(void)
{
	/* DMA sound frame address counter (as on the STE) */
	ULONG a = ((ULONG)*(volatile UBYTE *)0xffff8909 << 16)
		| ((ULONG)*(volatile UBYTE *)0xffff890b << 8)
		| *(volatile UBYTE *)0xffff890d;
	return ((a - (ULONG)ring) >> 1) & (FRAMES - 1);
}

extern "C" void paula_vbl(void);
extern "C" void paula_vbl(void)
{
	if (!running)
		return;
	if (tick_fn)
	{
		tick_acc += tick_hz;
		while (tick_acc >= vbl_hz)
		{
			tick_acc -= vbl_hz;
			tick_fn();
		}
	}
	ULONG p = play_frame();
	ULONG target = (p + AHEAD) & (FRAMES - 1);
	ULONG n = (target - wpos) & (FRAMES - 1);
	if (n > FRAMES / 2)
	{
		/* fell behind (interrupts were off for long): resync */
		wpos = (p + 16) & (FRAMES - 1);
		n = (target - wpos) & (FRAMES - 1);
	}
	while (n)
	{
		ULONG k = FRAMES - wpos;		/* up to the wrap */
		if (k > n)
			k = n;
		mix(wpos, (int)k);
		wpos = (wpos + k) & (FRAMES - 1);
		n -= k;
	}
}

/* VBL vector stub: C scratch registers saved, then the old handler */
extern "C" void paula_vbl_irq(void);
__asm__(
	"	.text\n"
	"	.globl	paula_vbl_irq\n"
	"paula_vbl_irq:\n"
	"	movem.l	%d0-%d1/%a0-%a1,-(%sp)\n"
	"	jsr	paula_vbl\n"
	"	movem.l	(%sp)+,%d0-%d1/%a0-%a1\n"
	"	move.l	paula_old_vbl,-(%sp)\n"
	"	rts\n"
);
extern "C" { void *paula_old_vbl; }

int paula_init(void (*tick)(void), int hz)
{
	ring_mem = (void *)Mxalloc(FRAMES * 2 + 16, 0);	/* ST RAM for DMA */
	if (!ring_mem || (long)ring_mem < 0)
		return 1;
	ring = (signed char *)(((ULONG)ring_mem + 15) & ~15UL);
	memset(ring, 0, FRAMES * 2);
	memset(v, 0, sizeof(v));
	enabled = 0;
	for (int vol = 0; vol <= 64; vol++)
		for (int u = 0; u < 256; u++)
			voltab[vol][u] = (signed char)(((signed char)u * vol) >> 7);
	step_k = (ULONG)(((unsigned long long)PAULA_HZ << 16) / RATE);
	tick_fn = tick;
	tick_hz = hz;
	vbl_hz = VgetMonitor() == MON_VGA ? 60 : 50;
	tick_acc = 0;

	Sndstatus(1);
	Setmode(STEREO8);
	Settracks(0, 0);
	Setmontracks(0);
	Devconnect(DMAPLAY, DAC, CLK25M, CLK10K, NO_SHAKE);
	Soundcmd(ADDERIN, MATIN);
	Soundcmd(LTATTEN, 0);
	Soundcmd(RTATTEN, 0);
	Setbuffer(SR_PLAY, ring, ring + FRAMES * 2);
	Buffoper(SB_PLA_ENA | SB_PLA_RPT);
	wpos = (play_frame() + 16) & (FRAMES - 1);

	UWORD sr = paula_lock();
	paula_old_vbl = *(void **)0x70;
	*(void **)0x70 = (void *)paula_vbl_irq;
	running = 1;
	paula_unlock(sr);
	return 0;
}

void paula_exit(void)
{
	if (!running)
		return;
	UWORD sr = paula_lock();
	running = 0;
	*(void **)0x70 = paula_old_vbl;
	paula_unlock(sr);
	Buffoper(0);
	Mfree(ring_mem);
	ring_mem = NULL;
}
