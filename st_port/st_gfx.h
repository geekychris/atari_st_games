/*
 * Atari ST graphics layer for the Uranus Lander port.
 *
 * Provides the subset of Amiga graphics.library calls the game uses
 * (SetAPen, SetBPen, Move, Draw, RectFill, WritePixel, Text) on ST low
 * resolution (320x200, 16 colours, interleaved bitplanes), so the
 * original draw.c works nearly unchanged.
 *
 * The game keeps its Amiga PAL coordinate system (320x256): every Y
 * coordinate is mapped to 200 lines here, so physics and layout stay
 * identical to the Amiga version.
 *
 * Speed: an 8 MHz ST can't clear and redraw 32 KB every 50 Hz frame.
 * Static scenery (terrain, title page) is drawn once into a background
 * buffer; each screen buffer remembers the rectangles drawn into it
 * and only those are restored from the background next time.
 */
#ifndef ST_GFX_H
#define ST_GFX_H

#include "amiga_types.h"

struct DirtyList;

struct RastPort {
	UWORD *base;			/* 32000 byte screen */
	WORD cx, cy;			/* graphics cursor (logical coords) */
	UBYTE apen, bpen;
	struct DirtyList *dirty;	/* NULL: don't record (background) */
	UBYTE record;			/* HUD layer: record ops, see gfx_hud() */
};

/* graphics.library subset (logical 320x256 coordinates) */
void SetAPen(struct RastPort *rp, ULONG pen);
void SetBPen(struct RastPort *rp, ULONG pen);
void Move(struct RastPort *rp, WORD x, WORD y);
void Draw(struct RastPort *rp, WORD x, WORD y);
void RectFill(struct RastPort *rp, WORD x0, WORD y0, WORD x1, WORD y1);
void WritePixel(struct RastPort *rp, WORD x, WORD y);
void Text(struct RastPort *rp, const char *str, ULONG len);

/* Cached composite drawing for the HUD layer.  'fn' draws something
 * made of many primitives (scaled text, a shield bitmap...) and is
 * recorded as ONE operation identified by fn + key bytes + x/y/arg:
 * it's only re-rendered when that identity changes.  Up to 40 key
 * bytes are copied; 'ctx' (if not NULL) is passed to fn instead of the
 * key copy and must stay valid until gfx_hud_commit().  w/h give the
 * logical size for the bounding box.  On a non-recording RastPort fn
 * is simply called.
 */
typedef void (*gfx_draw_fn)(struct RastPort *rp, const void *ctx, WORD x, WORD y, WORD arg);
void gfx_cached(struct RastPort *rp, gfx_draw_fn fn, const void *key, int keylen,
                const void *ctx, WORD x, WORD y, WORD arg, WORD w, WORD h);

/* convenience: big block-letter string, see Uranus Lander draw.c */
typedef void (*gfx_big_fn)(struct RastPort *rp, const char *str, WORD x, WORD y, WORD scale);
void gfx_big(struct RastPort *rp, const char *str, WORD x, WORD y, WORD scale,
             WORD width, gfx_big_fn draw);

/* 16 pixel wide single colour sprite: rows[] are bit masks (MSB =
 * left pixel), h logical rows starting at logical x, y.  Fast path
 * for invaders, frogs, cars...  Clipped; marks the dirty area.
 */
void gfx_mask16(struct RastPort *rp, WORD x, WORD y, const UWORD *rows, int h, int col);

/* clear a RastPort's whole buffer to colour 0 (scenery layer use) */
void gfx_clear(struct RastPort *rp);

/* Draw n single pixel "stars" into the back buffer: 'pts' points to
 * x, y, colour WORDs, 'stride' WORDs apart.  Points at or below
 * ground[x] are skipped (ground may be NULL).  The previous frame's
 * stars in that buffer are undone first.  Much cheaper than WritePixel.
 */
void gfx_stars(struct RastPort *rp, const WORD *pts, int n, int stride, const WORD *ground);

/* ST display management.  Layers, bottom to top:
 *   scenery  static picture (terrain, title page), drawn rarely
 *   HUD      text/bars drawn every frame through gfx_hud(), but only
 *            changes since the previous frame are actually rendered
 *   sprites  drawn into the back buffer each frame, undone next time
 * The background buffer = scenery + HUD; screens = background + sprites.
 */
int  gfx_init(const UWORD *amiga_palette, int ncolors);
void gfx_exit(void);
struct RastPort *gfx_bg(void);		/* scenery layer */
void gfx_bg_clear(void);
void gfx_bg_to_screens(void);		/* after scenery changes */
struct RastPort *gfx_hud(void);		/* HUD layer (records operations) */
void gfx_hud_commit(void);		/* render HUD changes of this frame */
void gfx_restore_back(void);		/* undo last sprites in back buffer */
struct RastPort *gfx_back(void);	/* sprite layer: back buffer */
void gfx_swap(void);			/* show back buffer from next VBL */
void gfx_set_frame_vbls(int n);		/* frame pacing: VBLs per frame */

/* Small sprintf for the HUD (%s, %c, %ld/%d with optional zero padded
 * width): mintlib's stdio sprintf costs ~10000 cycles per call on a
 * 68000, which is too much to spend five times every frame.
 */
int gfx_sprintf(char *buf, const char *fmt, ...);
#define sprintf gfx_sprintf

/* logical (Amiga PAL) -> physical (ST) Y */
#define GFX_Y(y) (((y) * 25) >> 5)

#endif
