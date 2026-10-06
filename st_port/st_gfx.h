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
	UBYTE ormode;			/* lines OR their colour, see gfx_or_mode */
};

/* graphics.library subset (logical 320x256 coordinates) */
void SetAPen(struct RastPort *rp, ULONG pen);
void SetBPen(struct RastPort *rp, ULONG pen);
void Move(struct RastPort *rp, WORD x, WORD y);
void Draw(struct RastPort *rp, WORD x, WORD y);
void RectFill(struct RastPort *rp, WORD x0, WORD y0, WORD x1, WORD y1);
void WritePixel(struct RastPort *rp, WORD x, WORD y);
void Text(struct RastPort *rp, const char *str, ULONG len);
void SetRast(struct RastPort *rp, ULONG pen);	/* no-op on the HUD layer */

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

/* Fastest sprite path for many small sprites over colour 0 areas:
 * ORs the mask into the planes where 'col' has a 1 bit (no
 * read-modify-write of the other planes) and doesn't mark dirty areas:
 * call gfx_mark() once for the whole group's bounding box instead.
 */
void gfx_or16(struct RastPort *rp, WORD x, WORD y, const UWORD *rows, int h, int col);
void gfx_mark(struct RastPort *rp, WORD x0, WORD y0, WORD x1, WORD y1);	/* logical */

/* Same for a horizontal row of identical sprites (an invader row):
 * the Y mapping, colour planes and pre-shifted masks (sprites at a
 * fixed spacing need only two shifts) are worked out once; xs[] holds
 * the n sprite x positions.  Marks the row's dirty area itself.
 */
void gfx_or16_row(struct RastPort *rp, const WORD *xs, int n, WORD y,
                  const UWORD *rows, int h, int col);

/* Full-width bands that are completely redrawn every frame (e.g. lanes
 * of traffic): copy logical rows y0..y1 from the background into the
 * back buffer, then draw into gfx_back_nomark(), which doesn't record
 * dirty rectangles.  Cheaper than many small dirty rectangles.
 */
void gfx_copy_band(WORD y0, WORD y1);

/* Heightmap band (terrain): column x0+i gets top_col at logical row
 * y[i] and body_col below it for body_h rows.  Much cheaper than a
 * RectFill per height change. */
void gfx_column_band(struct RastPort *rp, const WORD *y, int n, int x0,
		     int top_col, int body_col, int body_h);

/* Same for a scrolling world heightmap of n columns (wrapping): the
 * physical rows are prepared once (redo when the terrain changes), each
 * frame draws the 320 columns from world column 'start' (~5x faster). */
typedef struct gfx_heightmap gfx_heightmap;
gfx_heightmap *gfx_heightmap_prepare(const WORD *y, int n, int body_h);
void gfx_heightmap_free(gfx_heightmap *h);
void gfx_heightmap_draw(struct RastPort *rp, const gfx_heightmap *h, int start,
			int top_col, int body_col);
/* same, for rows just cleared to colour 0 (gfx_fill_rows) with nothing
 * else drawn in them yet: plain writes, no read-modify-write */
void gfx_heightmap_draw_fresh(struct RastPort *rp, const gfx_heightmap *h, int start,
			      int top_col, int body_col);
/* physical rows the terrain from 'start' covers (e.g. to clear them) */
void gfx_heightmap_rows(const gfx_heightmap *h, int start, int *y0, int *y1);
/* fill rows y0..y1 of the back buffer with a solid colour (movem
 * stores: about twice as fast as copying from the background) */
void gfx_fill_band(WORD y0, WORD y1, int col);
void gfx_fill_rows(int py0, int py1, int col);	/* same, physical rows */
struct RastPort *gfx_back_nomark(void);

/* Pre-shifted masked sprites (the classic ST technique): 'fn' draws the
 * object with the normal RastPort calls at logical (x, y) = (0..w-1,
 * y..y+h-1); it's rendered once into a scratch buffer, over colour 0 and
 * over colour 15 to derive the transparency mask, and stored in 16
 * pre-shifted copies so drawing needs no shifting.  y is part of the
 * sprite (the 256 -> 200 line mapping depends on it).  w <= 112.
 */
typedef struct gfx_sprite gfx_sprite;
gfx_sprite *gfx_sprite_build(gfx_draw_fn fn, const void *ctx, WORD arg, WORD y, WORD w, WORD h);
/* same, but composited onto solid colour 'bg': every group is opaque
 * (a plain copy, ~5x faster to draw).  For objects that only ever
 * appear on that colour and don't overlap (logs on water...). */
gfx_sprite *gfx_sprite_build_on(gfx_draw_fn fn, const void *ctx, WORD arg, WORD y, WORD w, WORD h, int bg);
void gfx_sprite_draw(struct RastPort *rp, const gfx_sprite *spr, WORD x);
/* at logical x, y (top left of the build area) for objects that also
 * move vertically; clipped; ORs into colour 0 areas in OR mode */
void gfx_sprite_draw_xy(struct RastPort *rp, const gfx_sprite *spr, WORD x, WORD y);

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
/* scenery rows y0..y1 (logical) into the background only, for ports
 * that copy those rows into the back buffer every frame (gfx_copy_band) */
void gfx_bg_commit_rows(WORD y0, WORD y1);
/* after gfx_bg_commit_rows: have gfx_restore_back() copy those rows into
 * each screen (instead of copying them every frame) */
void gfx_bg_dirty_rows(WORD y0, WORD y1);
struct RastPort *gfx_hud(void);		/* HUD layer (records operations) */
void gfx_hud_commit(void);		/* render HUD changes of this frame */
void gfx_hud_keep(void);		/* instead of drawing + commit: HUD unchanged */
void gfx_restore_back(void);		/* undo last sprites in back buffer */
struct RastPort *gfx_back(void);	/* sprite layer: back buffer */
void gfx_swap(void);			/* show back buffer from next VBL */
void gfx_set_frame_vbls(int n);		/* frame pacing: VBLs per frame */

/* Sprite layer lines in OR mode: Draw() only sets the planes where the
 * pen has 1 bits instead of rewriting all four.  Correct over colour 0
 * (vector graphics on a black background); where lines cross, colours
 * mix.  Halves the cost of line-heavy frames. */
void gfx_or_mode(int on);

/* Small sprintf for the HUD (%s, %c, %ld/%d with optional zero padded
 * width): mintlib's stdio sprintf costs ~10000 cycles per call on a
 * 68000, which is too much to spend five times every frame.
 */
int gfx_sprintf(char *buf, const char *fmt, ...);
#define sprintf gfx_sprintf

/* logical (Amiga PAL) -> physical (ST) Y */
#define GFX_Y(y) (((y) * 25) >> 5)

#endif
