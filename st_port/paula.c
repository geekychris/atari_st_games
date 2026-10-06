/*
 * Amiga Paula on Atari DMA sound (STE/TT, Falcon030), see paula.h.
 */
#include <osbind.h>
#include <mint/falcon.h>
#include <string.h>
#include "paula.h"

struct Custom custom;

#define FRAMES   2048			/* ring buffer, stereo 8 bit frames */
#define PAULA_HZ 3546895UL		/* PAL Paula clock */

struct voice {
	const signed char *ptr;
	ULONG len;			/* bytes */
	ULONG pos;			/* 16.16 */
	int on;
	int silent;			/* short all-zero block (one-shot end) */
	UWORD per;			/* period the step below is for */
	ULONG inc;			/* 16.16 step per output frame */
};

static struct voice v[4];
static UWORD enabled;
static signed char *ring;
static void *ring_mem;
static ULONG wpos;			/* next frame to mix */
static ULONG ahead;			/* mix this far ahead of playback */
static ULONG step_k;			/* (PAULA_HZ << 16) / rate */
static int want_rate, rate, falcon;
static void (*tick_fn)(void);
static short tick_num, tick_den, vbl_hz;
static long tick_acc;
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
 * classic 68000 one: integer and fraction position stepped with add/addx.
 */
static signed char voltab[65][256];

/* n frames from src at pos (16.16, integer part < 32768) into every
 * other byte of dst; add = 0 stores, 1 adds.  Returns the new position. */
ULONG paula_run(signed char *dst, int n, const signed char *src,
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

/* frames (at most n) until 'dist' (16.16) is covered at 'inc' per frame,
 * rounded up.  Usually the whole run fits and no division is needed;
 * otherwise a 32/16 divu does it (both scaled by 1/256). */
static int frames_until(ULONG dist, ULONG inc, int n)
{
	UWORD i8 = (UWORD)(inc >> 8) + 1;
	ULONG d8 = dist >> 8;
	if (d8 >= (ULONG)(UWORD)n * i8)
		return n;
	/* d8 < n * i8 <= 2048 * 65536: quotient fits 16 bits */
	UWORD q;
	__asm__ ("divu.w %2,%0" : "=d"(q) : "0"(d8), "dm"(i8));
	q = (UWORD)(q + 1);
	return q < n ? q : n;
}

/* Same, for a block that loops on itself (short waveforms, the location
 * registers point at the block being played): the position wraps at
 * 'len' (bytes, < 32768) inside the loop instead of returning to C at
 * every block end.  Returns the new position. */
ULONG paula_run_loop(signed char *dst, int n, const signed char *src,
		     ULONG pos, ULONG inc, const signed char *vt, int add, int len);
__asm__(
	"	.text\n"
	"	.globl	paula_run_loop\n"
	"paula_run_loop:\n"
	"	movem.l	%d2-%d7/%a2,-(%sp)\n"
	"	move.l	32(%sp),%a0\n"		/* dst */
	"	move.l	36(%sp),%d0\n"		/* n */
	"	move.l	40(%sp),%a1\n"		/* src */
	"	move.l	44(%sp),%d1\n"		/* pos */
	"	move.l	48(%sp),%d4\n"		/* inc */
	"	move.l	52(%sp),%a2\n"		/* vt */
	"	move.l	60(%sp),%d7\n"		/* len */
	"	move.w	%d1,%d3\n"
	"	swap	%d1\n"
	"	move.l	%d4,%d5\n"
	"	swap	%d5\n"
	"	moveq	#0,%d2\n"
	"	subq.l	#1,%d0\n"
	"	bmi.s	9f\n"
	"	tst.l	56(%sp)\n"
	"	bne.s	3f\n"
	"1:	move.b	(%a1,%d1.w),%d2\n"
	"	move.b	(%a2,%d2.w),(%a0)\n"
	"	addq.l	#2,%a0\n"
	"	add.w	%d4,%d3\n"
	"	addx.w	%d5,%d1\n"
	"	cmp.w	%d7,%d1\n"
	"	bcs.s	2f\n"
	"	sub.w	%d7,%d1\n"
	"2:	dbra	%d0,1b\n"
	"	bra.s	9f\n"
	"3:	move.b	(%a1,%d1.w),%d2\n"
	"	move.b	(%a2,%d2.w),%d6\n"
	"	add.b	%d6,(%a0)\n"
	"	addq.l	#2,%a0\n"
	"	add.w	%d4,%d3\n"
	"	addx.w	%d5,%d1\n"
	"	cmp.w	%d7,%d1\n"
	"	bcs.s	4f\n"
	"	sub.w	%d7,%d1\n"
	"4:	dbra	%d0,3b\n"
	"9:	swap	%d1\n"
	"	move.w	%d3,%d1\n"
	"	move.l	%d1,%d0\n"
	"	movem.l	(%sp)+,%d2-%d7/%a2\n"
	"	rts\n"
);

/* channel ch into one side of the ring, n frames.
 * add = 0: store (silence where the channel is quiet), 1: add */
static void mix_voice(int ch, signed char *dst, int n, int add)
{
	struct voice *c = &v[ch];
	UWORD per = custom.aud[ch].ac_per;
	int vol = custom.aud[ch].ac_vol;
	if (vol > 64)
		vol = 64;
	ULONG inc;
	if (per != c->per)
	{
		c->per = per;
		c->inc = per >= 64 ? step_k / per : 0;
	}
	inc = c->inc;
	if (c->on && c->silent
	    && (const signed char *)custom.aud[ch].ac_ptr != c->ptr)
		latch(ch);	/* registers changed while looping silence */
	/* a short block looping on itself: wrap inside the asm loop */
	if (c->on && !c->silent && inc && c->len <= 0x4000 && (inc >> 16) < c->len && (vol || !add) &&
	    (const signed char *)custom.aud[ch].ac_ptr == c->ptr &&
	    (ULONG)custom.aud[ch].ac_len * 2 == c->len)
	{
		c->pos = paula_run_loop(dst, n, c->ptr, c->pos, inc, voltab[vol], add, (int)c->len);
		return;
	}
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
		/* the inner loop indexes with a 16 bit register: run from a
		 * base at most 16 KB behind the position and stop before the
		 * index would pass 32 KB (MOD samples can be 128 KB) */
		ULONG base = (c->pos >> 16) & ~0x3fffUL;
		ULONG lpos = c->pos - (base << 16);
		ULONG lend = end - (base << 16);
		if (lend > (0x7fffUL << 16))
			lend = 0x7fffUL << 16;
		int k = frames_until(lend - lpos, inc, n);
		if (vol || !add)
			lpos = paula_run(dst, k, c->ptr + base, lpos, inc, voltab[vol], add);
		else
			lpos += inc * k;
		c->pos = lpos + (base << 16);
		dst += 2 * k;
		n -= k;
		if (c->pos >= end)
			latch(ch);	/* block done: reload, as Paula does */
	}
}

static int audible(int ch)
{
	return v[ch].on && !v[ch].silent && custom.aud[ch].ac_per >= 64;
}

/* one side: the first audible channel is stored, the second added; a
 * side with nothing audible is cleared (silent channels still advance) */
static void mix_side(int a, int b, signed char *dst, int n)
{
	int aa = audible(a), ab = audible(b);
	if (aa)
	{
		mix_voice(a, dst, n, 0);
		mix_voice(b, dst, n, 1);
	}
	else if (ab)
	{
		mix_voice(b, dst, n, 0);
		mix_voice(a, dst, n, 1);
	}
	else
	{
		mix_voice(a, dst, n, 1);	/* advance only */
		mix_voice(b, dst, n, 1);
		for (int i = 0; i < n; i++)
			dst[i * 2] = 0;
	}
}

static void mix(ULONG from, int n)
{
	signed char *l = ring + from * 2;
	if (!audible(0) && !audible(1) && !audible(2) && !audible(3))
	{
		/* the common case in games: nothing playing */
		for (int ch = 0; ch < 4; ch++)
			mix_voice(ch, l, n, 1);
		memset(l, 0, n * 2);
		return;
	}
	mix_side(0, 3, l, n);
	mix_side(1, 2, l + 1, n);
}

static ULONG play_frame(void)
{
	/* DMA sound frame address counter (STE, TT and Falcon) */
	ULONG a = ((ULONG)*(volatile UBYTE *)0xffff8909 << 16)
		| ((ULONG)*(volatile UBYTE *)0xffff890b << 8)
		| *(volatile UBYTE *)0xffff890d;
	return ((a - (ULONG)ring) >> 1) & (FRAMES - 1);
}

void paula_vbl(void);
void paula_vbl(void)
{
	if (!running)
		return;
	if (tick_fn)
	{
		long th = vbl_hz * tick_den;	/* (vbl_hz, tick_den small) */
		tick_acc += tick_num;
		while (tick_acc >= th)
		{
			tick_acc -= th;
			tick_fn();
		}
	}
	ULONG p = play_frame();
	ULONG target = (p + ahead) & (FRAMES - 1);
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
void paula_vbl_irq(void);
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
void *paula_old_vbl;

static long cookie(ULONG id)
{
	long *jar = *(long **)0x5a0;
	if (!jar)
		return -1;
	for (; jar[0]; jar += 2)
		if ((ULONG)jar[0] == id)
			return jar[1];
	return -1;
}

void paula_set_rate(int hz)
{
	want_rate = hz;
}

void paula_set_tick_rate(int num, int den)
{
	UWORD sr = paula_lock();
	tick_num = num;
	tick_den = den;
	paula_unlock(sr);
}

int paula_init(void (*tick)(void), int hz)
{
	long snd = cookie(0x5f534e44);		/* '_SND' */
	long mch = cookie(0x5f4d4348);		/* '_MCH' */
	UBYTE ste_mode = 1;

	if (snd < 0 || !(snd & 2))
		return 1;			/* no DMA sound */
	falcon = (mch >> 16) == 3;

	if (falcon)
	{
		/* 25.175 MHz / 256 / (prescale + 1) */
		static const struct { int hz, pre; } fr[] = {
			{ 8195, CLK8K }, { 9834, CLK10K }, { 12292, CLK12K },
			{ 16390, CLK16K }, { 19668, CLK20K }, { 24585, CLK25K },
		};
		int w = want_rate ? want_rate : 9834, best = 1;
		for (int i = 0; i < 6; i++)
			if ((fr[i].hz > w ? fr[i].hz - w : w - fr[i].hz) <
			    (fr[best].hz > w ? fr[best].hz - w : w - fr[best].hz))
				best = i;
		rate = fr[best].hz;
		ste_mode = fr[best].pre;
		ring_mem = (void *)Mxalloc(FRAMES * 2 + 16, 0);	/* ST RAM */
	}
	else
	{
		/* STE / TT: 6258, 12517, 25033 Hz (50066 is too costly) */
		int w = want_rate ? want_rate : 12517;
		ste_mode = w < 9000 ? 0 : w < 18000 ? 1 : 2;
		rate = ste_mode == 0 ? 6258 : ste_mode == 1 ? 12517 : 25033;
		ring_mem = (void *)Malloc(FRAMES * 2 + 16);
	}
	if (!ring_mem || (long)ring_mem < 0)
		return 1;
	ring = (signed char *)(((ULONG)ring_mem + 15) & ~15UL);
	memset(ring, 0, FRAMES * 2);
	memset(v, 0, sizeof(v));
	enabled = 0;
	for (int vol = 0; vol <= 64; vol++)
		for (int u = 0; u < 256; u++)
			voltab[vol][u] = (signed char)(((signed char)u * vol) >> 7);
	step_k = (ULONG)(((unsigned long long)PAULA_HZ << 16) / rate);
	tick_fn = tick;
	tick_num = hz;
	tick_den = 1;
	tick_acc = 0;
	if (falcon)
		vbl_hz = VgetMonitor() == MON_VGA ? 60 : 50;
	else
		vbl_hz = (*(volatile UBYTE *)0xffff820a & 2) ? 50 : 60;
	ahead = (ULONG)rate / vbl_hz * 2 + 64;	/* two VBLs plus margin */

	if (falcon)
	{
		Sndstatus(1);
		Setmode(STEREO8);
		Settracks(0, 0);
		Setmontracks(0);
		Devconnect(DMAPLAY, DAC, CLK25M, ste_mode, NO_SHAKE);
		Soundcmd(ADDERIN, MATIN);
		Soundcmd(LTATTEN, 0);
		Soundcmd(RTATTEN, 0);
		Setbuffer(SR_PLAY, ring, ring + FRAMES * 2);
		Buffoper(SB_PLA_ENA | SB_PLA_RPT);
	}
	else
	{
		ULONG s = (ULONG)ring, e = s + FRAMES * 2;
		volatile UBYTE *r = (volatile UBYTE *)0xffff8900;
		r[0x01] = 0;			/* stop */
		r[0x03] = s >> 16; r[0x05] = s >> 8; r[0x07] = s;
		r[0x0f] = e >> 16; r[0x11] = e >> 8; r[0x13] = e;
		r[0x21] = ste_mode;		/* stereo 8 bit, rate */
		r[0x01] = 3;			/* play, repeat */
	}
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
	if (falcon)
		Buffoper(0);
	else
		*(volatile UBYTE *)0xffff8901 = 0;
	Mfree(ring_mem);
	ring_mem = NULL;
}
