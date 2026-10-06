/*
 * STE blitter for the ST ports: copies between interleaved 4-plane low
 * resolution bitmaps (16 pixel groups of 4 words, any width), shifted to
 * any pixel position.  Used for scrolling layers: pre-render a strip
 * once, then blit the visible window every frame.
 *
 * Needs supervisor mode and an STE / Mega STE (blit_available()).
 */
#ifndef ST_BLIT_H
#define ST_BLIT_H

#include "amiga_types.h"

#define BLIT_COPY 3	/* D = S */
#define BLIT_OR   7	/* D = S | D */

/* 1 if the machine has a blitter (_MCH: STE / Mega STE) */
int blit_available(void);

/* Copy a w x h pixel area, all 4 planes, from (sx, sy) of src to
 * (dx, dy) of dst.  Strides are in bytes per row (160 for a screen).
 * x/y in physical pixels; the caller clips.  op: BLIT_COPY or BLIT_OR. */
void blit_area(const UWORD *src, int src_stride, int sx, int sy,
	       UWORD *dst, int dst_stride, int dx, int dy, int w, int h, int op);

#endif
