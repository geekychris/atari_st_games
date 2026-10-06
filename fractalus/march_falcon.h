/* Atari Falcon port: terrain column renderer, see march_falcon.c */
#ifndef MARCH_FALCON_H
#define MARCH_FALCON_H

#include "amiga_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* per sample along a ray: PROJ/dist << 12 and the fog colour offset */
struct march_sample {
	LONG recip;
	LONG doff;			/* fog bin * 4 (bytes into colour table) */
};

/* field offsets are used by the assembly */
struct march_state {
	LONG wx, wz;			/*  0 ray position << 12 */
	const struct march_sample *s;	/*  8 current sample */
	LONG n;				/* 12 samples left in the stretch */
	LONG thr;			/* 16 visible when q > thr */
	LONG cam_y;			/* 20 */
	const UBYTE *H;			/* 24 padded heights, 256 byte rows */
	LONG dx, dz;			/* 28 advance per sample in this stretch */
	LONG y_top;			/* 36 logical row the column is drawn up to */
	LONG horizon_y;			/* 40 */
	UBYTE *col;			/* 44 screen column (row 0, 16 bpp) */
	const WORD *ymap;		/* 48 logical -> physical row */
	const ULONG *colours;		/* 52 [height bin][fog bin], 2 pixels */
	const WORD *hoff;		/* 56 height -> height bin * 32 */
	LONG view_y, view_y2;		/* 60, 64 */
};

/* marches one stretch of equal-step samples, painting each visible
 * sample's 8 pixel strip.  Returns 1 when the column is full (y_top
 * reached view_y), 0 when the stretch is done (state saved after it). */
int march_falcon(struct march_state *m);

#ifdef __cplusplus
}
#endif

#endif
