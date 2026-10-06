/*
 * Atari Falcon030 graphics layer for Amiga game ports (AGA 8-bit games).
 *
 * The Falcon's 16 bit true colour mode is chunky (one RGB565 word per
 * pixel), so the Amiga graphics.library subset below is simple word
 * stores, with the game's 256 pens mapped through a palette table.
 * 320x240 on VGA (320x200 on RGB/TV); the game keeps its 320x256 Amiga
 * coordinates and Y is mapped to the screen height.
 */
#ifndef FGFX_H
#define FGFX_H

#include "amiga_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define JAM1       0
#define JAM2       1
#define COMPLEMENT 2

struct RastPort {
	UWORD *base;
	WORD cx, cy;
	UBYTE apen, bpen, drmd;
};

void SetAPen(struct RastPort *rp, ULONG pen);
void SetBPen(struct RastPort *rp, ULONG pen);
void SetDrMd(struct RastPort *rp, ULONG mode);
void Move(struct RastPort *rp, LONG x, LONG y);
void Draw(struct RastPort *rp, LONG x, LONG y);
void RectFill(struct RastPort *rp, LONG x0, LONG y0, LONG x1, LONG y1);
void WritePixel(struct RastPort *rp, LONG x, LONG y);
void Text(struct RastPort *rp, const char *str, ULONG len);
void SetRast(struct RastPort *rp, ULONG pen);
/* filled polygons (convex), no TmpRas / AreaInfo needed */
LONG AreaMove(struct RastPort *rp, LONG x, LONG y);
LONG AreaDraw(struct RastPort *rp, LONG x, LONG y);
LONG AreaEnd(struct RastPort *rp);

int  fgfx_init(void);			/* 0 = ok */
void fgfx_exit(void);
struct RastPort *fgfx_back(void);	/* buffer to draw this frame */
void fgfx_swap(void);			/* show it from the next VBL (triple
					 * buffered, never waits) */
void fgfx_set_rgb(int pen, int r, int g, int b);	/* 0-255 */
int  fgfx_height(void);			/* physical lines (240 or 200) */
int  fgfx_map_y(int y);			/* logical (0-255) -> physical */
UWORD fgfx_pen(int pen);		/* RGB565 of pen */
const WORD *fgfx_ymap(void);		/* logical row -> physical, [-32..287] */

/* direct fill helpers for the raycaster (logical coordinates) */
void fgfx_fill(struct RastPort *rp, int x0, int y0, int x1, int y1, int pen);
/* 8 pixel wide column x..x+7 (x even, inside the screen), y0..y1 */
void fgfx_strip8(struct RastPort *rp, int x, int y0, int y1, int pen);
/* same, pen per logical row from pens[] (a vertical gradient) */
void fgfx_strip8_rows(struct RastPort *rp, int x, int y0, int y1, const UBYTE *pens);

#ifdef __cplusplus
}
#endif

#endif
