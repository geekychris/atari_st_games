| SPDX-License-Identifier: MIT
| Copyright (c) 2026 Chris Collins <chris@hitorro.com>
|
| a3d_rows.S - the Falcon's polygon fill (a3d_draw.c's raster): rows of
| spans straight into the 8-plane interleaved screen. One loop, all in
| registers and well inside the 68030's 256-byte instruction cache: in C
| the loop came to 418 bytes with half its values on the stack, and every
| row missed the cache.
|
| void a3f_rows(const int16_t *xl, const int16_t *xr, int n,
|               const int32_t *rowoff, uint8_t *base, const uint32_t *pat)
|
| Row k (0..n-1) fills xl[k]..xr[k] (clamped to 0..319, nothing if
| xl > xr) on the row at base + rowoff[k] (nothing if rowoff[k] < 0) with
| pat's three longs (planes 0-1, 2-3, 4-5) in each 16-pixel group of 16
| bytes; planes 6 and 7 are left alone. Edge groups keep the pixels
| outside the span: new = ((old ^ pat) & ~mask) ^ pat.

	.text
	.globl	a3f_rows
a3f_rows:
	movem.l	%d2-%d7/%a2-%a6,-(%sp)
	move.l	48(%sp),%a0		| xl
	move.l	52(%sp),%a1		| xr
	move.l	56(%sp),%d7		| n
	move.l	60(%sp),%a2		| rowoff
	move.l	64(%sp),%a3		| base
	move.l	68(%sp),%a4		| pat
	move.l	(%a4)+,%d4
	move.l	(%a4)+,%d5
	move.l	(%a4),%d6
	lea	nlmask(%pc),%a4
	lea	nrmask(%pc),%a5
	subq.l	#1,%d7
	bmi	done
row:
	move.w	(%a0)+,%d0		| xl
	move.w	(%a1)+,%d1		| xr
	move.l	(%a2)+,%d2		| the row's offset
	bmi	next
	tst.w	%d0
	bpl.s	1f
	moveq	#0,%d0
1:	cmp.w	#319,%d1
	ble.s	2f
	move.w	#319,%d1
2:	cmp.w	%d1,%d0
	bgt	next
	lea	(%a3,%d2.l),%a6
	move.w	%d0,%d3
	and.w	#-16,%d3		| the first group's byte offset (x / 16 * 16)
	add.w	%d3,%a6
	move.w	%d1,%d2
	and.w	#-16,%d2
	sub.w	%d3,%d2			| 16 x (groups after the first)
	and.w	#15,%d0
	move.l	(%a4,%d0.w*4),%d0	| ~ the left edge's mask
	and.w	#15,%d1
	move.l	(%a5,%d1.w*4),%d1	| ~ the right edge's mask
	tst.w	%d2
	bne.s	wide
	or.l	%d1,%d0			| one group: ~(left & right)
	move.l	(%a6),%d3
	eor.l	%d4,%d3
	and.l	%d0,%d3
	eor.l	%d4,%d3
	move.l	%d3,(%a6)+
	move.l	(%a6),%d3
	eor.l	%d5,%d3
	and.l	%d0,%d3
	eor.l	%d5,%d3
	move.l	%d3,(%a6)+
	move.l	(%a6),%d3
	eor.l	%d6,%d3
	and.l	%d0,%d3
	eor.l	%d6,%d3
	move.l	%d3,(%a6)
	dbra	%d7,row
	bra.s	done
wide:					| the left edge
	move.l	(%a6),%d3
	eor.l	%d4,%d3
	and.l	%d0,%d3
	eor.l	%d4,%d3
	move.l	%d3,(%a6)+
	move.l	(%a6),%d3
	eor.l	%d5,%d3
	and.l	%d0,%d3
	eor.l	%d5,%d3
	move.l	%d3,(%a6)+
	move.l	(%a6),%d3
	eor.l	%d6,%d3
	and.l	%d0,%d3
	eor.l	%d6,%d3
	move.l	%d3,(%a6)+
	addq.l	#4,%a6
	lsr.w	#4,%d2
	subq.w	#2,%d2			| the full groups between, less one
	bmi.s	3f
4:	move.l	%d4,(%a6)+
	move.l	%d5,(%a6)+
	move.l	%d6,(%a6)+
	addq.l	#4,%a6
	dbra	%d2,4b
3:					| the right edge
	move.l	(%a6),%d3
	eor.l	%d4,%d3
	and.l	%d1,%d3
	eor.l	%d4,%d3
	move.l	%d3,(%a6)+
	move.l	(%a6),%d3
	eor.l	%d5,%d3
	and.l	%d1,%d3
	eor.l	%d5,%d3
	move.l	%d3,(%a6)+
	move.l	(%a6),%d3
	eor.l	%d6,%d3
	and.l	%d1,%d3
	eor.l	%d6,%d3
	move.l	%d3,(%a6)
next:
	dbra	%d7,row
done:
	movem.l	(%sp)+,%d2-%d7/%a2-%a6
	rts

| int a3f_poly_spans(const int16_t *xs, const int16_t *ys, int n,
|                    int16_t *xl, int16_t *xr, int *ymin)
|
| a3d_math.c's poly_spans (the same spans, row for row) in one tight
| loop: a convex polygon's rows ymin..ymax get xl..xr. Returns the row
| count, or -1 if there are more than 1024 (SPAN_MAX: poly_spans then).
	.globl	a3f_poly_spans
a3f_poly_spans:
	movem.l	%d2-%d7/%a2-%a6,-(%sp)
	move.l	48(%sp),%a4		| xs
	move.l	52(%sp),%a5		| ys
	move.l	56(%sp),%a1		| n
	move.l	60(%sp),%a2		| xl
	move.l	64(%sp),%a3		| xr
	move.w	(%a5),%d5		| ymin
	move.w	%d5,%d1			| ymax
	move.w	%a1,%d2
	subq.w	#2,%d2
	lea	2(%a5),%a6
1:	move.w	(%a6)+,%d3
	cmp.w	%d5,%d3
	bge.s	2f
	move.w	%d3,%d5
2:	cmp.w	%d1,%d3
	ble.s	3f
	move.w	%d3,%d1
3:	dbra	%d2,1b
	move.l	68(%sp),%a6
	move.w	%d5,%d0
	ext.l	%d0
	move.l	%d0,(%a6)		| *ymin
	sub.w	%d5,%d1
	ext.l	%d1
	addq.l	#1,%d1			| rows
	cmp.l	#1024,%d1
	ble.s	4f
	moveq	#-1,%d0
	bra	9f
4:	move.l	%d1,-(%sp)		| kept for the return
	move.l	%a2,%a6
	move.l	%a3,%a0
	move.w	%d1,%d2
	subq.w	#1,%d2
	move.w	#32767,%d3
	move.w	#-32768,%d4
5:	move.w	%d3,(%a6)+
	move.w	%d4,(%a0)+
	dbra	%d2,5b
	move.l	%a3,%a0
	sub.l	%a2,%a0			| xr - xl: (a6,a0) is xr's row
	moveq	#0,%d7			| the edge from vertex d7
edge_loop:
	move.w	(%a4,%d7.w*2),%d0	| x0
	move.w	(%a5,%d7.w*2),%d1	| y0
	move.w	%d7,%d6
	addq.w	#1,%d6
	cmp.w	%a1,%d6
	bne.s	1f
	moveq	#0,%d6
1:	move.w	(%a4,%d6.w*2),%d2	| x1
	move.w	(%a5,%d6.w*2),%d3	| y1
	cmp.w	%d3,%d1
	ble.s	2f
	exg	%d0,%d2
	exg	%d1,%d3
2:	ext.l	%d0
	ext.l	%d2
	move.w	%d0,%d4
	swap	%d4
	clr.w	%d4
	add.l	#0x8000,%d4		| x, rounded as poly_spans does per row
	moveq	#0,%d6			| dx
	move.l	%d2,%d6
	sub.l	%d0,%d6
	swap	%d6
	clr.w	%d6			| (x1 - x0) << 16
	move.w	%d3,%d0
	sub.w	%d1,%d0
	ext.l	%d0
	beq.s	3f
	divs.l	%d0,%d6
	bra.s	4f
3:	moveq	#0,%d6
4:	move.w	%d1,%d0
	sub.w	%d5,%d0
	lea	(%a2,%d0.w*2),%a6	| row y0
	move.w	%d3,%d0
	sub.w	%d1,%d0
	subq.w	#1,%d0			| the rows before y1
	bmi.s	last
row_loop:
	move.l	%d4,%d1
	swap	%d1			| px
	cmp.w	(%a6),%d1
	bge.s	5f
	move.w	%d1,(%a6)
5:	cmp.w	(%a6,%a0.l),%d1
	ble.s	6f
	move.w	%d1,(%a6,%a0.l)
6:	addq.l	#2,%a6
	add.l	%d6,%d4
	dbra	%d0,row_loop
last:	cmp.w	(%a6),%d2		| row y1: x1 itself
	bge.s	7f
	move.w	%d2,(%a6)
7:	cmp.w	(%a6,%a0.l),%d2
	ble.s	8f
	move.w	%d2,(%a6,%a0.l)
8:	addq.w	#1,%d7
	cmp.w	%a1,%d7
	bne	edge_loop
	move.l	(%sp)+,%d0
9:	movem.l	(%sp)+,%d2-%d7/%a2-%a6
	rts

| ~ the pixels from x to the group's end, doubled for two planes' words
	.even
nlmask:
	.long	0x00000000,0x80008000,0xc000c000,0xe000e000
	.long	0xf000f000,0xf800f800,0xfc00fc00,0xfe00fe00
	.long	0xff00ff00,0xff80ff80,0xffc0ffc0,0xffe0ffe0
	.long	0xfff0fff0,0xfff8fff8,0xfffcfffc,0xfffefffe
| ~ the pixels from the group's start to x
nrmask:
	.long	0x7fff7fff,0x3fff3fff,0x1fff1fff,0x0fff0fff
	.long	0x07ff07ff,0x03ff03ff,0x01ff01ff,0x00ff00ff
	.long	0x007f007f,0x003f003f,0x001f001f,0x000f000f
	.long	0x00070007,0x00030003,0x00010001,0x00000000
