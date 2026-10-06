/*
 * STE blitter, see st_blit.h.
 *
 * Each plane is one blit: source and destination step 8 bytes from one
 * word of the plane to the next, the skew register shifts the source to
 * the destination's pixel position, end masks protect the pixels left
 * and right of the area.  When the source starts further right in its
 * word than the destination, one extra source word is fetched first
 * (FXSR); when the last source word isn't needed it is not fetched
 * (NFSR).  HOG mode: the CPU waits until the blit is done.
 */
#include "st_blit.h"

#define B_HALFTONE ((volatile UWORD *)0xffff8a00)
#define B_SRC_XINC (*(volatile WORD *)0xffff8a20)
#define B_SRC_YINC (*(volatile WORD *)0xffff8a22)
#define B_SRC_ADDR (*(volatile ULONG *)0xffff8a24)
#define B_ENDMASK1 (*(volatile UWORD *)0xffff8a28)
#define B_ENDMASK2 (*(volatile UWORD *)0xffff8a2a)
#define B_ENDMASK3 (*(volatile UWORD *)0xffff8a2c)
#define B_DST_XINC (*(volatile WORD *)0xffff8a2e)
#define B_DST_YINC (*(volatile WORD *)0xffff8a30)
#define B_DST_ADDR (*(volatile ULONG *)0xffff8a32)
#define B_XCOUNT   (*(volatile UWORD *)0xffff8a36)
#define B_YCOUNT   (*(volatile UWORD *)0xffff8a38)
#define B_HOP      (*(volatile UBYTE *)0xffff8a3a)
#define B_OP       (*(volatile UBYTE *)0xffff8a3b)
#define B_CTRL     (*(volatile UBYTE *)0xffff8a3c)
#define B_SKEW     (*(volatile UBYTE *)0xffff8a3d)

int blit_available(void)
{
	long *jar = *(long **)0x5a0;
	if (!jar)
		return 0;
	for (; jar[0]; jar += 2)
		if (jar[0] == 0x5f4d4348)	/* '_MCH' */
			return jar[1] == 0x00010000 || jar[1] == 0x00010010;
	return 0;
}

void blit_area(const UWORD *src, int src_stride, int sx, int sy,
	       UWORD *dst, int dst_stride, int dx, int dy, int w, int h, int op)
{
	int sxo = sx & 15, dxo = dx & 15;
	int xcount = (dxo + w + 15) >> 4;	/* destination words per line */
	int swords = (sxo + w + 15) >> 4;	/* source words needed */
	int fxsr = sxo > dxo;
	int nfsr = swords < xcount + fxsr;
	UWORD em1 = 0xffff >> dxo;
	UWORD em3 = 0xffff << (15 - ((dx + w - 1) & 15));
	const UBYTE *s = (const UBYTE *)src + (long)sy * src_stride + (sx >> 4) * 8;
	UBYTE *d = (UBYTE *)dst + (long)dy * dst_stride + (dx >> 4) * 8;
	int sreads = xcount + fxsr - nfsr;

	if (w <= 0 || h <= 0)
		return;
	if (xcount == 1)
		em1 &= em3;

	B_SRC_XINC = 8;
	B_SRC_YINC = src_stride - (sreads - 1) * 8;
	B_DST_XINC = 8;
	B_DST_YINC = dst_stride - (xcount - 1) * 8;
	B_ENDMASK1 = em1;
	B_ENDMASK2 = 0xffff;
	B_ENDMASK3 = em3;
	B_HOP = 2;				/* source only */
	B_OP = op;
	for (int p = 0; p < 4; p++)
	{
		B_SRC_ADDR = (ULONG)(s + p * 2);
		B_DST_ADDR = (ULONG)(d + p * 2);
		B_XCOUNT = xcount;
		B_YCOUNT = h;
		B_SKEW = (UBYTE)(((dxo - sxo) & 15) | (fxsr ? 0x80 : 0) | (nfsr ? 0x40 : 0));
		B_CTRL = 0xc0;			/* busy + HOG: runs to the end */
		while (B_CTRL & 0x80)		/* (restart if interrupted) */
			B_CTRL = 0xc0;
	}
}
