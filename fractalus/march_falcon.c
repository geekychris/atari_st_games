/*
 * Atari Falcon port: terrain column renderer in 68030 assembly.
 *
 * The C version of the per-sample loop compiled to more than the 030's
 * 256 byte instruction cache, so every sample re-fetched its code from
 * ST RAM (which the true colour video DMA keeps busy).  This routine
 * keeps the ray march and the strip fill in one small loop.  Per sample
 * it computes exactly what the original Renderer::draw_terrain does:
 *
 *   h = bilinear Terrain::height_at_world(W_x >> 12, W_z >> 12)
 *   q = (h - cam_y) * PROJ / dist      (truncated toward zero; PROJ/dist
 *                                       as a 12 bit fixed point table)
 *   visible when horizon_y - q < y_top, i.e. q > thr; then fill the
 *   8 pixel strip from the (clamped) projection down to y_top - 1 with
 *   the pen for (height bin, distance bin) and move y_top up.
 *
 * Heights come from a padded copy (256 byte rows, row 128 = row 0,
 * column 128 = column 0) so no wrap masking is needed.  One call covers
 * a stretch of samples with the same step (the original's step changes
 * only 6 times along a ray), with the per-sample advance in registers.
 *
 * Registers: d0/d1 ray x/z << 12, d6 cam_y, d7 count, a0 heights,
 * a1 sample table, a2 thr, a4 state, a5/a6 advance, d2-d5/a3 scratch.
 */
#include "march_falcon.h"

#define ROW_BYTES 640	/* 320 pixels * 2 */

__asm__(
	"	.text\n"
	"	.globl	march_falcon\n"
	"march_falcon:\n"
	"	move.l	4(%sp),%a0\n"
	"	movem.l	%d2-%d7/%a2-%a6,-(%sp)\n"
	"	move.l	%a0,%a4\n"
	"	movem.l	(%a4),%d0-%d1/%a1\n"	/* wx, wz, s */
	"	move.l	12(%a4),%d7\n"		/* n */
	"	move.l	16(%a4),%a2\n"		/* thr */
	"	move.l	20(%a4),%d6\n"		/* cam_y */
	"	move.l	24(%a4),%a0\n"		/* H */
	"	move.l	28(%a4),%a5\n"		/* dx */
	"	move.l	32(%a4),%a6\n"		/* dz */
	"	subq.l	#1,%d7\n"
	"	bmi	8f\n"
	"1:\n"
	"	bfextu	%d0{#7:#7},%d2\n"	/* gx */
	"	bfextu	%d1{#7:#7},%d3\n"	/* gz */
	"	lsl.w	#8,%d3\n"
	"	or.w	%d2,%d3\n"
	"	lea	(%a0,%d3.w),%a3\n"
	"	bfextu	%d0{#14:#6},%d3\n"	/* tx / 64 */
	"	moveq	#0,%d2\n"
	"	move.b	(%a3),%d2\n"		/* h00 */
	"	moveq	#0,%d4\n"
	"	move.b	1(%a3),%d4\n"		/* h10 */
	"	sub.w	%d2,%d4\n"
	"	muls.w	%d3,%d4\n"
	"	asr.l	#5,%d4\n"
	"	add.w	%d2,%d2\n"
	"	add.w	%d2,%d4\n"		/* a */
	"	moveq	#0,%d2\n"
	"	move.b	256(%a3),%d2\n"		/* h01 */
	"	moveq	#0,%d5\n"
	"	move.b	257(%a3),%d5\n"		/* h11 */
	"	sub.w	%d2,%d5\n"
	"	muls.w	%d3,%d5\n"
	"	asr.l	#5,%d5\n"
	"	add.w	%d2,%d2\n"
	"	add.w	%d2,%d5\n"		/* b */
	"	sub.w	%d4,%d5\n"
	"	bfextu	%d1{#14:#6},%d3\n"	/* tz / 64 */
	"	muls.w	%d3,%d5\n"
	"	asr.l	#6,%d5\n"
	"	add.w	%d4,%d5\n"
	"	ext.l	%d5\n"			/* h */
	"	move.l	%d5,%d2\n"
	"	sub.l	%d6,%d2\n"		/* dy */
	"	bmi.s	3f\n"
	"	muls.l	(%a1),%d2\n"
	"	asr.l	#8,%d2\n"
	"	asr.l	#4,%d2\n"
	"	bra.s	4f\n"
	"3:	neg.l	%d2\n"
	"	muls.l	(%a1),%d2\n"
	"	asr.l	#8,%d2\n"
	"	asr.l	#4,%d2\n"
	"	neg.l	%d2\n"
	"4:	cmp.l	%a2,%d2\n"		/* q > thr: visible */
	"	bgt.s	5f\n"
	"2:	add.l	%a5,%d0\n"
	"	add.l	%a6,%d1\n"
	"	addq.l	#8,%a1\n"
	"	dbra	%d7,1b\n"
	/* stretch done: save the state after it */
	"8:	movem.l	%d0-%d1/%a1,(%a4)\n"
	"	move.l	%a2,16(%a4)\n"
	"	moveq	#0,%d0\n"
	"9:	movem.l	(%sp)+,%d2-%d7/%a2-%a6\n"
	"	rts\n"

	/* visible sample: d2 = q, d5 = h */
	"5:	move.l	40(%a4),%d3\n"		/* projected = horizon - q */
	"	sub.l	%d2,%d3\n"
	"	cmp.l	60(%a4),%d3\n"
	"	bge.s	6f\n"
	"	move.l	60(%a4),%d3\n"
	"6:	cmp.l	64(%a4),%d3\n"
	"	ble.s	7f\n"
	"	move.l	64(%a4),%d3\n"
	"7:	move.l	56(%a4),%a3\n"		/* colour = colours[hoff[h] + doff] */
	"	move.w	(%a3,%d5.w*2),%d4\n"
	"	add.w	6(%a1),%d4\n"
	"	move.l	52(%a4),%a3\n"
	"	move.l	(%a3,%d4.w),%d4\n"
	"	move.l	36(%a4),%d5\n"		/* old y_top */
	"	move.l	%d3,36(%a4)\n"		/* y_top = projected */
	"	move.l	40(%a4),%d2\n"
	"	sub.l	%d3,%d2\n"
	"	move.l	%d2,%a2\n"		/* thr = horizon - y_top */
	"	move.l	48(%a4),%a3\n"
	"	move.w	-2(%a3,%d5.w*2),%d5\n"	/* last row: ymap[old y_top - 1] */
	"	move.w	(%a3,%d3.w*2),%d2\n"	/* first row: ymap[projected] */
	"	sub.w	%d2,%d5\n"		/* rows - 1 */
	"	bmi.s	11f\n"
	"	mulu.w	#640,%d2\n"
	"	move.l	44(%a4),%a3\n"
	"	add.l	%d2,%a3\n"
	"10:	move.l	%d4,(%a3)+\n"
	"	move.l	%d4,(%a3)+\n"
	"	move.l	%d4,(%a3)+\n"
	"	move.l	%d4,(%a3)\n"
	"	lea	640-12(%a3),%a3\n"
	"	dbra	%d5,10b\n"
	"11:	cmp.l	60(%a4),%d3\n"		/* column full? */
	"	bgt	2b\n"
	"	moveq	#1,%d0\n"
	"	bra.s	9b\n"
);
