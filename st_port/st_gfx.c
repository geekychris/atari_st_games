/*
 * Atari ST graphics layer for the Uranus Lander port, see st_gfx.h.
 * Expects to run in supervisor mode (game calls Super() at start).
 */
#include <osbind.h>
#include <string.h>
#include <stdarg.h>
#include "st_gfx.h"

#define SCR_W     320
#define SCR_H     200
#define LINE_W    80		/* words per line: 20 groups x 4 planes */
#define SCR_BYTES 32000
#define MAX_DIRTY 320

struct DirtyList {
	int n;
	int full;			/* overflow: restore everything */
	struct { WORD g0, g1, y0, y1; } r[MAX_DIRTY];
};

static void *mem;
static UWORD *screen[2], *bgscreen, *scenery;
static int back;			/* index of buffer drawn this frame */
static struct DirtyList dirty[2];
static struct RastPort rp_screen[2], rp_scenery, rp_hud, rp_draw;

/* HUD layer: operations recorded this frame vs. last frame */
enum { OP_PIXEL, OP_LINE, OP_RECT, OP_TEXT, OP_CUSTOM };
#define MAX_OPS  400
#define TEXT_MAX 40
/* (OP_CUSTOM keys longer than TEXT_MAX are truncated) */
struct Op {
	UBYTE type, apen, bpen, len;
	WORD x0, y0, x1, y1;		/* logical coordinates */
	WORD g0, g1, by0, by1;		/* physical bbox: groups, rows */
	UBYTE matched;
	gfx_draw_fn fn;			/* OP_CUSTOM: drawing function */
	const void *ctx;		/* OP_CUSTOM: context, or NULL = text */
	WORD arg;
	char text[TEXT_MAX];		/* text, or OP_CUSTOM key bytes */
};
static struct Op ops_a[MAX_OPS], ops_b[MAX_OPS];
static struct Op *cur_ops = ops_a, *prev_ops = ops_b;
static int n_cur, n_prev;

/* stars drawn into each screen buffer: word offsets, for undoing */
#define MAX_STARS_ST 128
static UWORD star_off[2][MAX_STARS_ST];
static int star_n[2];
static UWORD row_off[SCR_H];

/* logical (256 line) -> physical (200 line) Y, without a 32 bit multiply */
#define YMAP_MIN (-128)
#define YMAP_MAX 383
static WORD ymap[YMAP_MAX - YMAP_MIN + 1];

static inline int map_y(int y)
{
	if (y < YMAP_MIN || y > YMAP_MAX)
		return y < 0 ? -1 : SCR_H;
	return ymap[y - YMAP_MIN];
}

/* per-plane colour masks: 0xffff where the colour has that plane bit */
static UWORD cmask[16][4];

static inline void color_masks(int col, UWORD m[4])
{
	const UWORD *c = cmask[col & 15];
	m[0] = c[0]; m[1] = c[1]; m[2] = c[2]; m[3] = c[3];
}

/* set the pixels in 'mask' of one 16 pixel group to colour masks 'c' */
static inline void group_set(UWORD *p, UWORD mask, const UWORD *c)
{
	UWORD keep = ~mask;
	p[0] = (p[0] & keep) | (mask & c[0]);
	p[1] = (p[1] & keep) | (mask & c[1]);
	p[2] = (p[2] & keep) | (mask & c[2]);
	p[3] = (p[3] & keep) | (mask & c[3]);
}

static void *old_phys, *old_log;
static WORD old_rez;
static WORD old_pal[16];

/* system 8x8 font from Line-A */
static const UBYTE *font_data;
static UWORD font_width, font_first;

/* ------------------------------------------------------------------ */
/* low level */

static void linea_init(void)
{
	register void *fonts __asm__("a1");
	__asm__ volatile (".dc.w 0xa000" : "=r"(fonts) : : "d0", "d1", "d2", "a0", "a2", "memory");
	const UBYTE *f8 = ((const UBYTE **)fonts)[1];	/* 8x8 system font */
	font_first = *(const UWORD *)(f8 + 36);
	font_data = *(const UBYTE **)(f8 + 76);
	font_width = *(const UWORD *)(f8 + 80);
}

static void mouse_hide(void)
{
	__asm__ volatile (".dc.w 0xa00a" : : : "d0", "d1", "d2", "a0", "a1", "a2", "memory");
}

static void mouse_show(void)
{
	__asm__ volatile (".dc.w 0xa009" : : : "d0", "d1", "d2", "a0", "a1", "a2", "memory");
}

static void add_dirty(struct DirtyList *d, int g0, int g1, int y0, int y1);
static void copy_rect(UWORD *dst, const UWORD *src, int g0, int g1, int y0, int y1);

static void mark(struct RastPort *rp, short x0, short y0, short x1, short y1)
{
	struct DirtyList *d = rp->dirty;
	if (!d || d->full)
		return;
	short g0 = x0 >> 4, g1 = x1 >> 4;
	if (d->n)
	{
		/* merge into the previous mark when they touch (consecutive
		 * edges of a polygon, a sprite drawn in parts): one rectangle
		 * instead of many overlapping ones to restore */
		struct { WORD g0, g1, y0, y1; } *r = (void *)&d->r[d->n - 1];
		short ug0 = g0 < r->g0 ? g0 : r->g0, ug1 = g1 > r->g1 ? g1 : r->g1;
		short uy0 = y0 < r->y0 ? y0 : r->y0, uy1 = y1 > r->y1 ? y1 : r->y1;
		/* touching or overlapping only: a cheap test (the 68000 has no
		 * fast 32 bit multiply for an area heuristic) */
		int merge = g0 <= r->g1 + 1 && g1 >= r->g0 - 1 &&
			    y0 <= r->y1 + 1 && y1 >= r->y0 - 1;
		if (merge)
		{
			r->g0 = ug0;
			r->g1 = ug1;
			r->y0 = uy0;
			r->y1 = uy1;
			return;
		}
	}
	if (d->n >= MAX_DIRTY)
	{
		d->full = 1;
		return;
	}
	d->r[d->n].g0 = g0;
	d->r[d->n].g1 = g1;
	d->r[d->n].y0 = y0;
	d->r[d->n].y1 = y1;
	d->n++;
}

static inline void plot(UWORD *base, int x, int y, int col)
{
	UWORD *p = base + row_off[y] + ((x >> 4) << 2);
	UWORD bit = 0x8000 >> (x & 15);
	if (col & 1) p[0] |= bit; else p[0] &= ~bit;
	if (col & 2) p[1] |= bit; else p[1] &= ~bit;
	if (col & 4) p[2] |= bit; else p[2] &= ~bit;
	if (col & 8) p[3] |= bit; else p[3] &= ~bit;
}

/* set pixels selected by 'mask' in one 16 pixel group to 'col' */
static inline void group_fill(UWORD *p, UWORD mask, int col)
{
	UWORD inv = ~mask;
	p[0] = (col & 1) ? (p[0] | mask) : (p[0] & inv);
	p[1] = (col & 2) ? (p[1] | mask) : (p[1] & inv);
	p[2] = (col & 4) ? (p[2] | mask) : (p[2] & inv);
	p[3] = (col & 8) ? (p[3] | mask) : (p[3] & inv);
}

/* horizontal span x0..x1 (physical, clipped) */
static void span(UWORD *base, int x0, int x1, int y, int col)
{
	UWORD *p = base + row_off[y] + ((x0 >> 4) << 2);
	int g0 = x0 >> 4, g1 = x1 >> 4;
	UWORD lmask = 0xffff >> (x0 & 15);
	UWORD rmask = 0xffff << (15 - (x1 & 15));

	if (g0 == g1)
	{
		group_fill(p, lmask & rmask, col);
		return;
	}
	group_fill(p, lmask, col);
	for (p += 4, g0++; g0 < g1; g0++, p += 4)
		group_fill(p, 0xffff, col);
	group_fill(p, rmask, col);
}

/* ------------------------------------------------------------------ */
/* graphics.library subset */

void SetAPen(struct RastPort *rp, ULONG pen) { rp->apen = pen & 15; }

/* fill whole RastPort; on the HUD layer the background already is the
 * scenery, so it's ignored there (use scenery colour 0 = cleared) */
void fill_movem(void *end, long blocks, ULONG a, ULONG b);

void SetRast(struct RastPort *rp, ULONG pen)
{
	if (rp->record)
		return;
	/* whole buffer with movem stores (games that redraw every frame) */
	const UWORD *c = cmask[pen & 15];
	fill_movem((UBYTE *)rp->base + SCR_BYTES, SCR_BYTES / 32,
		   (ULONG)c[0] << 16 | c[1], (ULONG)c[2] << 16 | c[3]);
	if (rp->dirty)
		rp->dirty->full = 1;
}
void SetBPen(struct RastPort *rp, ULONG pen) { rp->bpen = pen & 15; }

void Move(struct RastPort *rp, WORD x, WORD y)
{
	rp->cx = x;
	rp->cy = y;
}

static void record(struct RastPort *rp, int type, int x0, int y0, int x1, int y1,
                   const char *text, int len);

/*
 * Bresenham line (physical coordinates).  The word pointer and bit mask
 * step along with x/y instead of being recomputed per pixel, and the
 * loop is specialised per colour so each plane is a plain OR or AND
 * (about 2x faster than a generic masked write: vector games draw many
 * lines).  Lines entirely on screen skip the per-pixel clipping test.
 * In OR mode (gfx_or_mode) only the colour's 1 planes are written.
 */
#define LINE_PLANE(i, c, OR) do {						\
		if ((c) >> (i) & 1) q[i] |= bit;				\
		else if (!(OR)) q[i] &= ~bit;					\
	} while (0)
#define LINE_LOOP(c, CLIP, OR) for (;;) {					\
		if (!(CLIP) || ((unsigned)x0 < SCR_W && (unsigned)y0 < SCR_H)) {	\
			LINE_PLANE(0, c, OR); LINE_PLANE(1, c, OR);		\
			LINE_PLANE(2, c, OR); LINE_PLANE(3, c, OR);		\
		}								\
		if (!n--)							\
			break;							\
		short e2 = err + err;						\
		if (e2 >= dy) {							\
			err += dy; x0 += sx;					\
			if (sx > 0) { if (!(bit >>= 1)) { bit = 0x8000; q += 4; } }	\
			else if (!(bit = (UWORD)(bit << 1))) { bit = 1; q -= 4; }	\
		}								\
		if (e2 <= dx) { err += dx; y0 += sy; q += rowstep; }		\
	}
#define LINE_CASE(c) case c:							\
	if (ormode) { if (clip) LINE_LOOP(c, 1, 1) else LINE_LOOP(c, 0, 1) }	\
	else { if (clip) LINE_LOOP(c, 1, 0) else LINE_LOOP(c, 0, 0) }		\
	break;

static void line(UWORD *base, short x0, short y0, short x1, short y1, int col, int ormode)
{
	short dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
	short dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
	short err = dx + dy, n = dx > -dy ? dx : -dy;
	short rowstep = sy > 0 ? SCR_BYTES / SCR_H / 2 : -(SCR_BYTES / SCR_H / 2);
	int clip = (unsigned)x0 >= SCR_W || (unsigned)x1 >= SCR_W ||
		   (unsigned)y0 >= SCR_H || (unsigned)y1 >= SCR_H;
	/* pointer for the start pixel even if it is off screen: it is only
	 * dereferenced for on-screen pixels, and it steps consistently */
	UWORD *q = base + (long)y0 * (SCR_BYTES / SCR_H / 2) + ((x0 >> 4) << 2);
	UWORD bit = 0x8000 >> (x0 & 15);

	switch (col & 15)
	{
	LINE_CASE(0) LINE_CASE(1) LINE_CASE(2) LINE_CASE(3)
	LINE_CASE(4) LINE_CASE(5) LINE_CASE(6) LINE_CASE(7)
	LINE_CASE(8) LINE_CASE(9) LINE_CASE(10) LINE_CASE(11)
	LINE_CASE(12) LINE_CASE(13) LINE_CASE(14) LINE_CASE(15)
	}
}

void WritePixel(struct RastPort *rp, WORD x, WORD y)
{
	if (rp->record)
	{
		record(rp, OP_PIXEL, x, y, x, y, NULL, 0);
		return;
	}
	int py = map_y(y);
	if ((unsigned)x >= SCR_W || (unsigned)py >= SCR_H)
		return;
	group_set(rp->base + row_off[py] + ((x >> 4) << 2), 0x8000 >> (x & 15), cmask[rp->apen]);
	mark(rp, x, py, x, py);
}

/* line from cursor to x,y (inclusive), cursor moves to x,y */
void Draw(struct RastPort *rp, WORD x, WORD y)
{
	if (rp->record)
	{
		record(rp, OP_LINE, rp->cx, rp->cy, x, y, NULL, 0);
		rp->cx = x;
		rp->cy = y;
		return;
	}
	short x0 = rp->cx, y0 = map_y(rp->cy), x1 = x, y1 = map_y(y);
	short bx0 = x0 < x1 ? x0 : x1, bx1 = x0 < x1 ? x1 : x0;
	short by0 = y0 < y1 ? y0 : y1, by1 = y0 < y1 ? y1 : y0;

	rp->cx = x;
	rp->cy = y;
	if (bx1 < 0 || bx0 >= SCR_W || by1 < 0 || by0 >= SCR_H)
		return;

	if (by0 == by1)
	{
		/* horizontal: one span */
		span(rp->base, bx0 < 0 ? 0 : bx0, bx1 >= SCR_W ? SCR_W - 1 : bx1, by0, rp->apen);
	}
	else if (bx0 == bx1)
	{
		/* vertical: same word & bit on every line */
		UWORD m[4], bit = 0x8000 >> (bx0 & 15), nbit = ~bit;
		UWORD *p = rp->base + ((bx0 >> 4) << 2);
		int ya = by0 < 0 ? 0 : by0, yb = by1 >= SCR_H ? SCR_H - 1 : by1;
		color_masks(rp->apen, m);
		for (int yy = ya; yy <= yb; yy++)
		{
			UWORD *q = p + row_off[yy];
			q[0] = (q[0] & nbit) | (bit & m[0]);
			q[1] = (q[1] & nbit) | (bit & m[1]);
			q[2] = (q[2] & nbit) | (bit & m[2]);
			q[3] = (q[3] & nbit) | (bit & m[3]);
		}
	}
	else
		line(rp->base, x0, y0, x1, y1, rp->apen, rp->ormode);
	if (bx0 < 0) bx0 = 0;
	if (by0 < 0) by0 = 0;
	if (bx1 >= SCR_W) bx1 = SCR_W - 1;
	if (by1 >= SCR_H) by1 = SCR_H - 1;
	mark(rp, bx0, by0, bx1, by1);
}

static void __attribute__((noinline)) rectfill_general(struct RastPort *rp, WORD x0, WORD y0, WORD x1, WORD y1);

/* Small rectangles inside one 16 pixel group (tiles, dots, particles)
 * are the common case: handled here with few registers; everything else
 * goes through the general code. */
void RectFill(struct RastPort *rp, WORD x0, WORD y0, WORD x1, WORD y1)
{
	if (!rp->record && (x0 >> 4) == (x1 >> 4) && x0 >= 0 && x1 < SCR_W && x0 <= x1 &&
	    y0 >= 0 && y1 <= YMAP_MAX && y0 <= y1)
	{
		short py0 = ymap[y0 - YMAP_MIN], py1 = ymap[y1 - YMAP_MIN];
		if (py1 >= SCR_H) py1 = SCR_H - 1;
		if (py0 > py1)
			return;
		UWORD m = (0xffff >> (x0 & 15)) & (0xffff << (15 - (x1 & 15))), k = ~m;
		const UWORD *c = cmask[rp->apen];
		UWORD c0 = c[0] & m, c1 = c[1] & m, c2 = c[2] & m, c3 = c[3] & m;
		UWORD *q = rp->base + row_off[py0] + ((x0 >> 4) << 2);
		short n = py1 - py0;
		do
		{
			q[0] = (q[0] & k) | c0;
			q[1] = (q[1] & k) | c1;
			q[2] = (q[2] & k) | c2;
			q[3] = (q[3] & k) | c3;
			q += LINE_W;
		} while (--n >= 0);
		if (rp->dirty)
			mark(rp, x0, py0, x1, py1);
		return;
	}
	rectfill_general(rp, x0, y0, x1, y1);
}

static void rectfill_general(struct RastPort *rp, WORD x0, WORD y0, WORD x1, WORD y1)
{
	if (rp->record)
	{
		record(rp, OP_RECT, x0, y0, x1, y1, NULL, 0);
		return;
	}
	short py0 = map_y(y0), py1 = map_y(y1);
	if (x0 == x1 && py0 == py1)
	{
		/* single pixel (particles, radar dots): no span setup */
		if ((unsigned)x0 < SCR_W && (unsigned)py0 < SCR_H)
		{
			group_set(rp->base + row_off[py0] + ((x0 >> 4) << 2), 0x8000 >> (x0 & 15), cmask[rp->apen]);
			mark(rp, x0, py0, x0, py0);
		}
		return;
	}
	if (x0 < 0) x0 = 0;
	if (x1 >= SCR_W) x1 = SCR_W - 1;
	if (py0 < 0) py0 = 0;
	if (py1 >= SCR_H) py1 = SCR_H - 1;
	if (x0 > x1 || py0 > py1)
		return;
	if (x0 == x1)
	{
		/* single column (terrain is drawn like this): one bit per line */
		UWORD m[4], bit = 0x8000 >> (x0 & 15), nbit = ~bit;
		UWORD *q = rp->base + row_off[py0] + ((x0 >> 4) << 2);
		color_masks(rp->apen, m);
		for (int y = py0; y <= py1; y++, q += LINE_W)
		{
			q[0] = (q[0] & nbit) | (bit & m[0]);
			q[1] = (q[1] & nbit) | (bit & m[1]);
			q[2] = (q[2] & nbit) | (bit & m[2]);
			q[3] = (q[3] & nbit) | (bit & m[3]);
		}
	}
	else
	{
		const UWORD *c = cmask[rp->apen];
		short g0 = x0 >> 4, g1 = x1 >> 4;
		UWORD lmask = 0xffff >> (x0 & 15), rmask = 0xffff << (15 - (x1 & 15));
		UWORD *row = rp->base + row_off[py0] + (g0 << 2);
		if (g0 == g1)
		{
			UWORD m = lmask & rmask;
			for (short y = py0; y <= py1; y++, row += LINE_W)
				group_set(row, m, c);
		}
		else
		{
			for (int y = py0; y <= py1; y++, row += LINE_W)
			{
				UWORD *q = row;
				group_set(q, lmask, c);
				for (int g = g0 + 1; g < g1; g++)
				{
					q += 4;
					q[0] = c[0]; q[1] = c[1]; q[2] = c[2]; q[3] = c[3];
				}
				group_set(q + 4, rmask, c);
			}
		}
	}
	mark(rp, x0, py0, x1, py1);
}

/* 8x8 text, Amiga JAM2 mode: background drawn in BPen.
 * Cursor y is the baseline like topaz/8 (glyph top = baseline - 6).
 */
/*
 * Text glyph cache: games redraw the same strings every frame, and
 * assembling shifted 8 pixel glyphs into plane words is slow on a 68000
 * (variable shifts).  The assembled bit rows of a string depend only on
 * its characters, its x position within a 16 pixel group and vertical
 * clipping, so they are kept (LRU) and reused wherever it is drawn.
 */
#define TC_ENTRIES 48
#define TC_MAXLEN  40
#define TC_WORDS   ((TC_MAXLEN * 8 + 15) / 16 + 1)
static struct {
	char s[TC_MAXLEN];
	UBYTE len, shift, r0, r1;
	ULONG used;
	UWORD bits[8 * TC_WORDS];
} tcache[TC_ENTRIES];
static ULONG tc_clock;
static UWORD tc_scratch[8 * (SCR_W / 16 + 2)];

static void text_assemble(UWORD *out, const char *str, int n, int shift0, int nw, int r0, int r1)
{
	for (int r = r0; r <= r1; r++, out += nw)
	{
		int sh = shift0;
		UWORD *bw = out;
		for (int w = 0; w < nw; w++)
			out[w] = 0;
		for (int c = 0; c < n; c++)
		{
			UWORD g = (UWORD)font_data[((UBYTE)str[c] - font_first) + r * font_width] << 8;
			bw[0] |= g >> sh;
			if (sh > 8)
				bw[1] |= g << (16 - sh);
			sh += 8;
			if (sh >= 16)
			{
				sh -= 16;
				bw++;
			}
		}
	}
}

static const UWORD *text_bits(const char *str, int n, int shift0, int nw, int r0, int r1)
{
	int i, lru = 0;
	if (n > TC_MAXLEN)
	{
		text_assemble(tc_scratch, str, n, shift0, nw, r0, r1);
		return tc_scratch;
	}
	tc_clock++;
	for (i = 0; i < TC_ENTRIES; i++)
	{
		if (tcache[i].len == n && tcache[i].shift == shift0 && tcache[i].r0 == r0 &&
		    tcache[i].r1 == r1 && !memcmp(tcache[i].s, str, n))
		{
			tcache[i].used = tc_clock;
			return tcache[i].bits;
		}
		if (tcache[i].used < tcache[lru].used)
			lru = i;
	}
	memcpy(tcache[lru].s, str, n);
	tcache[lru].len = n;
	tcache[lru].shift = shift0;
	tcache[lru].r0 = r0;
	tcache[lru].r1 = r1;
	tcache[lru].used = tc_clock;
	text_assemble(tcache[lru].bits, str, n, shift0, nw, r0, r1);
	return tcache[lru].bits;
}

void Text(struct RastPort *rp, const char *str, ULONG len)
{
	if (rp->record)
	{
		record(rp, OP_TEXT, rp->cx, rp->cy, rp->cx + 8 * len - 1, rp->cy, str, len);
		rp->cx += 8 * len;
		return;
	}
	int x = rp->cx, top = map_y(rp->cy) - 6;
	int x_start = x;
	UWORD fgm[4], bgm[4];
	int r0 = top < 0 ? -top : 0, r1 = top + 7 >= SCR_H ? SCR_H - 1 - top : 7;

	/* Whole string per scan row: the row's glyph bits go into a word
	 * buffer, then each plane word is written once (JAM2: inside the
	 * string the background is drawn too, so only the two edge words
	 * need a read).  Characters outside the screen are dropped, like
	 * before. */
	int c0 = x < 0 ? (-x + 7) / 8 : 0;		/* first visible char */
	int c1 = (int)len;
	if (x + 8 * c1 > SCR_W)
		c1 = (SCR_W - x) / 8;			/* chars that fit */
	x += 8 * len;
	if (c0 < c1 && r0 <= r1)
	{
		int px0 = x_start + 8 * c0, px1 = x_start + 8 * c1;	/* [px0, px1) */
		int g0 = px0 >> 4, g1 = (px1 - 1) >> 4, nw = g1 - g0 + 1;
		UWORD lmask = 0xffff >> (px0 & 15), rmask = 0xffff << (15 - ((px1 - 1) & 15));
		int shift0 = px0 - (g0 << 4);

		color_masks(rp->apen, fgm);
		color_masks(rp->bpen, bgm);
		const UWORD *cbits = text_bits(str + c0, c1 - c0, shift0, nw, r0, r1);
		UWORD *row = rp->base + row_off[top + r0] + (g0 << 2);
		for (int r = r0; r <= r1; r++, row += LINE_W, cbits += nw)
		{
			const UWORD *bits = cbits;
			UWORD *q = row;
			for (int w = 0; w < nw; w++, q += 4)
			{
				UWORD f = bits[w], b = ~f;
				UWORD cover = (w == 0 ? lmask : 0xffff) & (w == nw - 1 ? rmask : 0xffff);
				if (cover == 0xffff)
				{
					q[0] = (f & fgm[0]) | (b & bgm[0]);
					q[1] = (f & fgm[1]) | (b & bgm[1]);
					q[2] = (f & fgm[2]) | (b & bgm[2]);
					q[3] = (f & fgm[3]) | (b & bgm[3]);
				}
				else
				{
					UWORD keep = ~cover;
					b &= cover;
					q[0] = (q[0] & keep) | (f & fgm[0]) | (b & bgm[0]);
					q[1] = (q[1] & keep) | (f & fgm[1]) | (b & bgm[1]);
					q[2] = (q[2] & keep) | (f & fgm[2]) | (b & bgm[2]);
					q[3] = (q[3] & keep) | (f & fgm[3]) | (b & bgm[3]);
				}
			}
		}
	}
	rp->cx = x;
	if (x > x_start && r0 <= r1)
	{
		int x1 = x - 1 < SCR_W ? x - 1 : SCR_W - 1;
		if (x_start < SCR_W)
			mark(rp, x_start < 0 ? 0 : x_start, top + r0, x1, top + r1);
	}
}

/* ------------------------------------------------------------------ */
/* display management */

static UBYTE old_sync, old_mste = 0xff;

static long gfx_cookie(ULONG id)
{
	long *jar = *(long **)0x5a0;
	if (!jar)
		return -1;
	for (; jar[0]; jar += 2)
		if ((ULONG)jar[0] == id)
			return jar[1];
	return -1;
}

int gfx_init(const UWORD *pal, int n)
{
	WORD stpal[16];
	UBYTE *p;

	/* PAL 50 Hz like the Amiga games: the ports pace game updates by
	 * the VBL (a US TOS boots in 60 Hz, which would run them 20% fast) */
	old_sync = *(volatile UBYTE *)0xffff820a;
	*(volatile UBYTE *)0xffff820a = old_sync | 2;
	/* Mega STE: 16 MHz with cache (CPU bound ports run up to 2x) */
	if (gfx_cookie(0x5f4d4348) == 0x00010010)	/* '_MCH' Mega STE */
	{
		old_mste = *(volatile UBYTE *)0xffff8e21;
		*(volatile UBYTE *)0xffff8e21 = 3;
	}

	for (int y = 0; y < SCR_H; y++)
		row_off[y] = y * LINE_W;
	for (int y = YMAP_MIN; y <= YMAP_MAX; y++)
		ymap[y - YMAP_MIN] = GFX_Y(y);
	for (int c = 0; c < 16; c++)
		for (int b = 0; b < 4; b++)
			cmask[c][b] = (c >> b) & 1 ? 0xffff : 0;

	mem = (void *)Malloc(4L * SCR_BYTES + 256);
	if (!mem)
		return 0;
	p = (UBYTE *)(((ULONG)mem + 255) & ~255UL);	/* screens: 256 byte aligned */
	screen[0] = (UWORD *)p;
	screen[1] = (UWORD *)(p + SCR_BYTES);
	bgscreen = (UWORD *)(p + 2 * SCR_BYTES);
	scenery = (UWORD *)(p + 3 * SCR_BYTES);
	memset(p, 0, 4L * SCR_BYTES);

	for (int i = 0; i < 2; i++)
	{
		rp_screen[i].base = screen[i];
		rp_screen[i].dirty = &dirty[i];
		dirty[i].n = dirty[i].full = 0;
	}
	rp_scenery.base = scenery;
	rp_scenery.dirty = NULL;
	rp_hud.record = 1;
	rp_draw.base = bgscreen;
	rp_draw.dirty = NULL;

	linea_init();
	mouse_hide();

	old_phys = Physbase();
	old_log = Logbase();
	old_rez = Getrez();
	for (int i = 0; i < 16; i++)
		old_pal[i] = Setcolor(i, -1);

	/* Amiga $0RGB (4 bit) -> STE format (3 bit + LSB in bit 3), ST ignores bit 3 */
	for (int i = 0; i < 16; i++)
	{
		UWORD c = i < n ? pal[i] : 0, out = 0;
		for (int s = 0; s < 12; s += 4)
		{
			UWORD v = (c >> s) & 15;
			out |= (((v >> 1) | ((v & 1) << 3)) & 15) << s;
		}
		stpal[i] = out;
	}
	Vsync();
	Setscreen(screen[0], screen[0], 0);
	Setpalette(stpal);
	back = 1;
	Vsync();
	return 1;
}

void gfx_exit(void)
{
	Vsync();
	*(volatile UBYTE *)0xffff820a = old_sync;
	if (old_mste != 0xff)
		*(volatile UBYTE *)0xffff8e21 = old_mste;
	Setscreen(old_log, old_phys, old_rez);
	Setpalette(old_pal);
	Vsync();
	mouse_show();
	if (mem)
		Mfree(mem);
	mem = NULL;
}

struct RastPort *gfx_bg(void) { return &rp_scenery; }
struct RastPort *gfx_back(void) { return &rp_screen[back]; }
struct RastPort *gfx_hud(void) { return &rp_hud; }

static struct RastPort rp_nomark;

struct RastPort *gfx_back_nomark(void)
{
	rp_nomark = rp_screen[back];
	rp_nomark.dirty = NULL;
	return &rp_nomark;
}

/* fill 'blocks' x 32 bytes downwards from 'end' with the plane
 * pattern a, b (planes 0/1, 2/3) using movem: ~2 cycles per byte */
void fill_movem(void *end, long blocks, ULONG a, ULONG b);
__asm__(
	"	.text\n"
	"	.globl	fill_movem\n"
	"fill_movem:\n"
	"	movem.l	%d2-%d4/%a2-%a5,-(%sp)\n"	/* 7 regs = 28 bytes */
	"	move.l	32(%sp),%a0\n"
	"	move.l	36(%sp),%d4\n"
	"	move.l	40(%sp),%d0\n"
	"	move.l	44(%sp),%d1\n"
	"	move.l	%d0,%d2\n"
	"	move.l	%d1,%d3\n"
	"	move.l	%d0,%a2\n"
	"	move.l	%d1,%a3\n"
	"	move.l	%d0,%a4\n"
	"	move.l	%d1,%a5\n"
	"	subq.l	#1,%d4\n"
	"1:	movem.l	%d0-%d3/%a2-%a5,-(%a0)\n"	/* memory: a,b,a,b,... */
	"	dbra	%d4,1b\n"
	"	movem.l	(%sp)+,%d2-%d4/%a2-%a5\n"
	"	rts\n"
);

void gfx_fill_band(WORD y0, WORD y1, int col)
{
	int py0 = map_y(y0), py1 = map_y(y1);
	if (py0 < 0) py0 = 0;
	if (py1 >= SCR_H) py1 = SCR_H - 1;
	if (py0 > py1)
		return;
	const UWORD *c = cmask[col & 15];
	ULONG a = (ULONG)c[0] << 16 | c[1], b = (ULONG)c[2] << 16 | c[3];
	UBYTE *end = (UBYTE *)(screen[back] + row_off[py1] + LINE_W);
	long n = (py1 - py0 + 1) * (LINE_W * 2);	/* bytes, multiple of 160 */
	/* 160 bytes per line = 5 x 32 byte movem blocks */
	fill_movem(end, n / 32, a, b);
}

/*
 * Heightmap band: for columns x0 .. x0+n-1, logical row y[i] gets
 * top_col and rows y[i]+1 .. y[i]+body_h get body_col (terrain with a
 * highlighted surface).  Works 16 columns at a time: each column ORs
 * its bit into per-row masks, then every row of the group is written
 * once per plane, instead of one rectangle per height change.
 */
void gfx_column_band(struct RastPort *rp, const WORD *y, int n, int x0,
		     int top_col, int body_col, int body_h)
{
	UWORD mt[SCR_H], mb[SCR_H];
	WORD ct[16], cb[16];
	UWORD selt[4], selb[4];
	short x = x0, i = 0;

	for (int pl = 0; pl < 4; pl++)
	{
		selt[pl] = (top_col >> pl) & 1 ? 0xffff : 0;
		selb[pl] = (body_col >> pl) & 1 ? 0xffff : 0;
	}
	while (i < n)
	{
		short g = x >> 4, s0 = x & 15, cnt = 16 - s0;
		short rmin = SCR_H, rmax = -1, k;
		if (cnt > n - i)
			cnt = n - i;
		if (g < 0 || g >= SCR_W / 16)
		{
			x += cnt;
			i += cnt;
			continue;
		}
		/* physical top / body rows of the group's columns */
		const WORD *yy = y + i;
		for (k = 0; k < cnt; k++)
		{
			short ly = yy[k], t, b;
			t = ly < YMAP_MIN ? -1 : ly > YMAP_MAX ? SCR_H : ymap[ly - YMAP_MIN];
			ly += body_h;
			b = ly < YMAP_MIN ? -1 : ly > YMAP_MAX ? SCR_H - 1 : ymap[ly - YMAP_MIN];
			if (b >= SCR_H) b = SCR_H - 1;
			ct[k] = t;
			cb[k] = b;
			if (t >= SCR_H || b < 0)
				continue;
			if (t < rmin) rmin = t < 0 ? 0 : t;
			if (b > rmax) rmax = b;
		}
		if (rmax >= rmin)
		{
			UWORD bit = 0x8000 >> s0;
			for (short r = rmin; r <= rmax; r++)
				mt[r] = mb[r] = 0;
			for (k = 0; k < cnt; k++, bit >>= 1)
			{
				short t = ct[k], b = cb[k], r;
				if (t >= SCR_H || b < 0)
					continue;
				if (t >= 0)
					mt[t] |= bit;
				for (r = t < 0 ? 0 : t + 1; r <= b; r++)
					mb[r] |= bit;
			}
			UWORD *p = rp->base + row_off[rmin] + (g << 2);
			for (short r = rmin; r <= rmax; r++, p += LINE_W)
			{
				UWORD t = mt[r], bb = mb[r] & ~t, keep = ~(t | bb);
				if (keep == 0xffff)
					continue;
				p[0] = (p[0] & keep) | (t & selt[0]) | (bb & selb[0]);
				p[1] = (p[1] & keep) | (t & selt[1]) | (bb & selb[1]);
				p[2] = (p[2] & keep) | (t & selt[2]) | (bb & selb[2]);
				p[3] = (p[3] & keep) | (t & selt[3]) | (bb & selb[3]);
			}
			if (rp->dirty)
				mark(rp, g << 4, rmin, (g << 4) + 15, rmax);
		}
		x += cnt;
		i += cnt;
	}
}

/*
 * Prepared heightmap (scrolling terrain): physical rows per world column
 * are computed once, then each frame only does, per column, two table
 * reads, one OR for the top row and a dbra loop for the body rows
 * (assembly below), then writes each touched row of a 16 column group
 * once per plane.
 */
struct gfx_heightmap {
	int n;
	UWORD *top2;		/* physical top row * 2 (byte offset in mask arrays) */
	UWORD *cnt;		/* body rows - 1 (dbra count), 0xffff: none */
	UWORD *bot;		/* physical bottom row */
	UWORD *wmin, *wmax;	/* row range of columns i .. i+15 (wrapping) */
};

gfx_heightmap *gfx_heightmap_prepare(const WORD *y, int n, int body_h)
{
	gfx_heightmap *h = (gfx_heightmap *)Malloc(sizeof(*h) + (long)n * 10);
	if (!h || (long)h < 0)
		return NULL;
	h->n = n;
	h->top2 = (UWORD *)(h + 1);
	h->cnt = h->top2 + n;
	h->bot = h->cnt + n;
	h->wmin = h->bot + n;
	h->wmax = h->wmin + n;
	for (int i = 0; i < n; i++)
	{
		int t = map_y(y[i]), b = map_y(y[i] + body_h);
		if (t < 0) t = 0;
		if (t > SCR_H - 1) t = SCR_H - 1;
		if (b > SCR_H - 1) b = SCR_H - 1;
		if (b < t) b = t;
		h->top2[i] = t * 2;
		h->cnt[i] = b > t ? b - t - 1 : 0xffff;
		h->bot[i] = b;
	}
	for (int i = 0; i < n; i++)
	{
		int mn = SCR_H, mx = -1;
		for (int k = 0, j = i; k < 16; k++, j = j + 1 < n ? j + 1 : 0)
		{
			int t = h->top2[j] >> 1;
			if (t < mn) mn = t;
			if (h->bot[j] > mx) mx = h->bot[j];
		}
		h->wmin[i] = mn;
		h->wmax[i] = mx;
	}
	return h;
}

void gfx_heightmap_free(gfx_heightmap *h)
{
	if (h)
		Mfree(h);
}

void gfx_heightmap_rows(const gfx_heightmap *h, int start, int *y0, int *y1)
{
	int mn = SCR_H, mx = -1, wx = start % h->n;
	if (wx < 0)
		wx += h->n;
	for (int g = 0; g < SCR_W / 16; g++)
	{
		if (h->wmin[wx] < mn) mn = h->wmin[wx];
		if (h->wmax[wx] > mx) mx = h->wmax[wx];
		wx += 16;
		if (wx >= h->n)
			wx -= h->n;
	}
	*y0 = mn;
	*y1 = mx;
}

/* OR the bits of 'cnt' columns into the masks: mt[top] for the top row,
 * mb[top+1 .. top+cnt+1] for the body.  bit = first column's bit. */
void hm_columns(const UWORD *top2, const UWORD *cnt, int n, UWORD bit,
		UWORD *mt, UWORD *mb);
__asm__(
	"	.text\n"
	"	.globl	hm_columns\n"
	"hm_columns:\n"
	"	movem.l	%d2-%d4/%a2-%a5,-(%sp)\n"
	"	movem.l	32(%sp),%a0-%a1\n"		/* top2, cnt */
	"	move.l	40(%sp),%d2\n"			/* n */
	"	move.l	44(%sp),%d3\n"			/* bit */
	"	movem.l	48(%sp),%a2-%a3\n"		/* mt, mb */
	"	subq.w	#1,%d2\n"
	"	bmi.s	9f\n"
	"1:	move.w	(%a0)+,%d0\n"			/* top * 2 */
	"	or.w	%d3,(%a2,%d0.w)\n"
	"	move.w	(%a1)+,%d1\n"			/* body rows - 1 */
	"	bmi.s	3f\n"
	"	lea	2(%a3,%d0.w),%a4\n"
	"2:	or.w	%d3,(%a4)+\n"
	"	dbra	%d1,2b\n"
	"3:	lsr.w	#1,%d3\n"
	"	dbra	%d2,1b\n"
	"9:	movem.l	(%sp)+,%d2-%d4/%a2-%a5\n"
	"	rts\n"
);

static void heightmap_draw(struct RastPort *rp, const gfx_heightmap *h, int start,
			   int top_col, int body_col, int fresh);

void gfx_heightmap_draw(struct RastPort *rp, const gfx_heightmap *h, int start,
			int top_col, int body_col)
{
	heightmap_draw(rp, h, start, top_col, body_col, 0);
}

void gfx_heightmap_draw_fresh(struct RastPort *rp, const gfx_heightmap *h, int start,
			      int top_col, int body_col)
{
	heightmap_draw(rp, h, start, top_col, body_col, 1);
}

static void heightmap_draw(struct RastPort *rp, const gfx_heightmap *h, int start,
			   int top_col, int body_col, int fresh)
{
	UWORD mt[SCR_H], mb[SCR_H];
	UWORD selt[4], selb[4];
	short wx = start % h->n;

	if (wx < 0)
		wx += h->n;
	for (int pl = 0; pl < 4; pl++)
	{
		selt[pl] = (top_col >> pl) & 1 ? 0xffff : 0;
		selb[pl] = (body_col >> pl) & 1 ? 0xffff : 0;
	}
	for (short g = 0; g < SCR_W / 16; g++)
	{
		short rmin = h->wmin[wx], rmax = h->wmax[wx], k;
		for (short r = rmin; r <= rmax; r++)
			mt[r] = mb[r] = 0;
		k = h->n - wx;			/* columns before the wrap */
		if (k >= 16)
			hm_columns(h->top2 + wx, h->cnt + wx, 16, 0x8000, mt, mb);
		else
		{
			hm_columns(h->top2 + wx, h->cnt + wx, k, 0x8000, mt, mb);
			hm_columns(h->top2, h->cnt, 16 - k, 0x8000 >> k, mt, mb);
		}
		wx += 16;
		if (wx >= h->n)
			wx -= h->n;
		UWORD *p = rp->base + row_off[rmin] + (g << 2);
		if (fresh)
			/* rows were just cleared to colour 0: plain writes */
			for (short r = rmin; r <= rmax; r++, p += LINE_W)
			{
				UWORD t = mt[r], bb = mb[r] & ~t;
				p[0] = (t & selt[0]) | (bb & selb[0]);
				p[1] = (t & selt[1]) | (bb & selb[1]);
				p[2] = (t & selt[2]) | (bb & selb[2]);
				p[3] = (t & selt[3]) | (bb & selb[3]);
			}
		else
			for (short r = rmin; r <= rmax; r++, p += LINE_W)
			{
				UWORD t = mt[r], bb = mb[r] & ~t, keep = ~(t | bb);
				if (keep == 0xffff)
					continue;
				p[0] = (p[0] & keep) | (t & selt[0]) | (bb & selb[0]);
				p[1] = (p[1] & keep) | (t & selt[1]) | (bb & selb[1]);
				p[2] = (p[2] & keep) | (t & selt[2]) | (bb & selb[2]);
				p[3] = (p[3] & keep) | (t & selt[3]) | (bb & selb[3]);
			}
		if (rp->dirty)
			mark(rp, g << 4, rmin, (g << 4) + 15, rmax);
	}
}

void gfx_fill_rows(int py0, int py1, int col)
{
	if (py0 < 0) py0 = 0;
	if (py1 >= SCR_H) py1 = SCR_H - 1;
	if (py0 > py1)
		return;
	const UWORD *c = cmask[col & 15];
	fill_movem((UBYTE *)(screen[back] + row_off[py1] + LINE_W), (long)(py1 - py0 + 1) * (LINE_W * 2) / 32,
		   (ULONG)c[0] << 16 | c[1], (ULONG)c[2] << 16 | c[3]);
}

/*
 * Vertical spans: column x0+i is filled from logical row y0[i] to y1[i]
 * (nothing if y1 < y0) with colour col.  16 columns at a time: each
 * column toggles its bit at its first and one-past-last row in two
 * difference arrays, a running XOR down the rows gives each row's mask,
 * and each row is written once per plane.  A column costs the same
 * however tall its span is (mountain silhouettes, terrain bodies).
 */
/* Per-column part of gfx_vspans in assembly: for n columns, map
 * y0/y1 to screen rows through ymap, toggle the column's bit at the
 * first row and one past the last in dmask (indexed from row -128), and
 * track the row range in mm[0] (min) / mm[1] (max). */
void vs_columns(const WORD *y0, const WORD *y1, int n, UWORD bit, UWORD *dmask,
		const WORD *ymap0, WORD *mm);
__asm__(
	"	.text\n"
	"	.globl	vs_columns\n"
	"vs_columns:\n"
	"	movem.l	%d2-%d5/%a2-%a4,-(%sp)\n"
	"	movem.l	32(%sp),%a0-%a1\n"		/* y0, y1 */
	"	move.l	40(%sp),%d2\n"			/* n */
	"	move.l	44(%sp),%d3\n"			/* bit */
	"	movem.l	48(%sp),%a2-%a4\n"		/* dmask, ymap0, mm */
	"	move.w	(%a4),%d4\n"			/* min */
	"	move.w	2(%a4),%d5\n"			/* max */
	"	subq.w	#1,%d2\n"
	"	bmi.s	9f\n"
	"1:	move.w	(%a0)+,%d0\n"
	"	move.w	(%a1)+,%d1\n"
	"	cmp.w	%d0,%d1\n"
	"	blt.s	5f\n"			/* empty */
	"	add.w	%d0,%d0\n"
	"	move.w	(%a3,%d0.w),%d0\n"	/* screen row of y0 */
	"	add.w	%d1,%d1\n"
	"	move.w	(%a3,%d1.w),%d1\n"	/* screen row of y1 */
	"	cmp.w	%d4,%d0\n"
	"	bge.s	2f\n"
	"	move.w	%d0,%d4\n"
	"2:	cmp.w	%d5,%d1\n"
	"	ble.s	3f\n"
	"	move.w	%d1,%d5\n"
	"3:	add.w	%d0,%d0\n"
	"	eor.w	%d3,(%a2,%d0.w)\n"
	"	add.w	%d1,%d1\n"
	"	eor.w	%d3,2(%a2,%d1.w)\n"
	"5:	lsr.w	#1,%d3\n"
	"	dbra	%d2,1b\n"
	"	move.w	%d4,(%a4)\n"
	"	move.w	%d5,2(%a4)\n"
	"9:	movem.l	(%sp)+,%d2-%d5/%a2-%a4\n"
	"	rts\n"
);

/* rows -128 .. 383 map to screen rows -100 .. 299 */
#define DM_OFF 100
static UWORD vs_dmask[DM_OFF + 300 + 2];

void gfx_vspans(struct RastPort *rp, int x0, int n, const WORD *y0, const WORD *y1, int col)
{
	const UWORD *c = cmask[col & 15];
	UWORD c0 = c[0], c1 = c[1], c2 = c[2], c3 = c[3];
	UWORD *dm = vs_dmask + DM_OFF;		/* index = screen row */
	short i = 0, x = x0;

	while (i < n)
	{
		short g = x >> 4, s0 = x & 15, cnt = 16 - s0;
		WORD mm[2] = { 32767, -32768 };
		if (cnt > n - i)
			cnt = n - i;
		if (g < 0 || g >= SCR_W / 16)
		{
			x += cnt;
			i += cnt;
			continue;
		}
		/* y values must be inside the mapping table (-128 .. 383) */
		vs_columns(y0 + i, y1 + i, cnt, 0x8000 >> s0, dm, ymap - YMAP_MIN, mm);
		if (mm[1] >= mm[0])
		{
			short rmin = mm[0], rmax = mm[1], r = rmin;
			UWORD m = 0;
			/* rows above the screen only feed the running mask */
			for (; r < 0 && r <= rmax; r++)
				m ^= dm[r];
			short last = rmax < SCR_H ? rmax : SCR_H - 1;
			UWORD *p = rp->base + row_off[r < SCR_H ? r : SCR_H - 1] + (g << 2);
			for (; r <= last; r++, p += LINE_W)
			{
				m ^= dm[r];
				if (!m)
					continue;
				if (m == 0xffff)
				{
					/* fully covered row: plain stores */
					p[0] = c0; p[1] = c1; p[2] = c2; p[3] = c3;
					continue;
				}
				/* per plane a single OR or AND */
				UWORD km = ~m;
				if (c0) p[0] |= m; else p[0] &= km;
				if (c1) p[1] |= m; else p[1] &= km;
				if (c2) p[2] |= m; else p[2] &= km;
				if (c3) p[3] |= m; else p[3] &= km;
			}
			/* keep the difference array zero between groups */
			for (r = rmin; r <= rmax + 1; r++)
				dm[r] = 0;
			if (rp->dirty && last >= (rmin < 0 ? 0 : rmin))
				mark(rp, g << 4, rmin < 0 ? 0 : rmin, (g << 4) + 15, last);
		}
		x += cnt;
		i += cnt;
	}
}

void gfx_copy_band(WORD y0, WORD y1)
{
	int py0 = map_y(y0), py1 = map_y(y1);
	if (py0 < 0) py0 = 0;
	if (py1 >= SCR_H) py1 = SCR_H - 1;
	if (py0 <= py1)
		memcpy(screen[back] + row_off[py0], bgscreen + row_off[py0],
		       (py1 - py0 + 1) * LINE_W * 2);
}

void gfx_bg_clear(void)
{
	memset(scenery, 0, SCR_BYTES);
}

void gfx_bg_commit_rows(WORD y0, WORD y1)
{
	int py0 = map_y(y0), py1 = map_y(y1);
	if (py0 < 0) py0 = 0;
	if (py1 >= SCR_H) py1 = SCR_H - 1;
	if (py0 <= py1)
		memcpy(bgscreen + row_off[py0], scenery + row_off[py0],
		       (py1 - py0 + 1) * LINE_W * 2);
}

void gfx_bg_copy_rect(struct RastPort *rp, WORD x0, WORD y0, WORD x1, WORD y1)
{
	int py0 = map_y(y0), py1 = map_y(y1);
	if (x0 < 0) x0 = 0;
	if (x1 >= SCR_W) x1 = SCR_W - 1;
	if (py0 < 0) py0 = 0;
	if (py1 >= SCR_H) py1 = SCR_H - 1;
	if (x0 > x1 || py0 > py1)
		return;
	copy_rect(rp->base, scenery, x0 >> 4, x1 >> 4, py0, py1);
	if (rp->dirty)
		mark(rp, x0, py0, x1, py1);
}

void gfx_bg_dirty_rows(WORD y0, WORD y1)
{
	int py0 = map_y(y0), py1 = map_y(y1);
	if (py0 < 0) py0 = 0;
	if (py1 >= SCR_H) py1 = SCR_H - 1;
	if (py0 <= py1)
	{
		add_dirty(&dirty[0], 0, SCR_W / 16 - 1, py0, py1);
		add_dirty(&dirty[1], 0, SCR_W / 16 - 1, py0, py1);
	}
}

void gfx_bg_to_screens(void)
{
	memcpy(bgscreen, scenery, SCR_BYTES);
	memcpy(screen[0], scenery, SCR_BYTES);
	memcpy(screen[1], scenery, SCR_BYTES);
	dirty[0].n = dirty[0].full = 0;
	dirty[1].n = dirty[1].full = 0;
	star_n[0] = star_n[1] = 0;
	n_prev = n_cur = 0;		/* HUD must be drawn again */
}

/* ------------------------------------------------------------------ */
/* HUD layer */

static void record(struct RastPort *rp, int type, int x0, int y0, int x1, int y1,
                   const char *text, int len)
{
	struct Op *op;
	int px0, px1, py0, py1;

	if (n_cur >= MAX_OPS)
		return;
	op = &cur_ops[n_cur];
	op->type = type;
	op->apen = rp->apen;
	op->bpen = rp->bpen;
	op->x0 = x0; op->y0 = y0; op->x1 = x1; op->y1 = y1;
	op->len = 0;
	if (type == OP_TEXT || type == OP_CUSTOM)
	{
		if (len > TEXT_MAX)
			len = TEXT_MAX;
		memcpy(op->text, text, len);
		op->len = len;
	}
	/* physical bounding box */
	px0 = x0 < x1 ? x0 : x1;
	px1 = x0 < x1 ? x1 : x0;
	if (type == OP_TEXT)
	{
		py0 = map_y(y0) - 6;
		py1 = py0 + 7;
	}
	else if (type == OP_CUSTOM)
	{
		/* x1/y1 = bottom right corner (logical) */
		py0 = map_y(y0);
		py1 = map_y(y1);
	}
	else
	{
		int a = map_y(y0), b = map_y(y1);
		py0 = a < b ? a : b;
		py1 = a < b ? b : a;
	}
	if (px0 < 0) px0 = 0;
	if (py0 < 0) py0 = 0;
	if (px1 >= SCR_W) px1 = SCR_W - 1;
	if (py1 >= SCR_H) py1 = SCR_H - 1;
	if (px0 > px1 || py0 > py1)
		return;			/* off screen */
	op->g0 = px0 >> 4;
	op->g1 = px1 >> 4;
	op->by0 = py0;
	op->by1 = py1;
	op->matched = 0;
	n_cur++;
}

void gfx_cached(struct RastPort *rp, gfx_draw_fn fn, const void *key, int keylen,
                const void *ctx, WORD x, WORD y, WORD arg, WORD w, WORD h)
{
	if (!rp->record)
	{
		fn(rp, ctx ? ctx : key, x, y, arg);
		return;
	}
	int n0 = n_cur;
	record(rp, OP_CUSTOM, x, y, x + w - 1, y + h - 1, key, keylen);
	if (n_cur > n0)
	{
		cur_ops[n0].fn = fn;
		cur_ops[n0].ctx = ctx;
		cur_ops[n0].arg = arg;
	}
}

void gfx_big(struct RastPort *rp, const char *str, WORD x, WORD y, WORD scale,
             WORD width, gfx_big_fn draw)
{
	gfx_cached(rp, (gfx_draw_fn)draw, str, strlen(str) + 1, NULL, x, y, scale,
	           width, 7 * scale);
}

void gfx_or16(struct RastPort *rp, WORD x, WORD y, const UWORD *rows, int h, int col)
{
	int shift = 16 - (x & 15), g = x >> 4;
	int p0 = col & 1, p1 = col & 2, p2 = col & 4, p3 = col & 8;

	if (x <= -16 || x >= SCR_W)
		return;
	for (int r = 0; r < h; r++)
	{
		int py = map_y(y + r);
		if ((unsigned)py >= SCR_H || !rows[r])
			continue;
		ULONG m = (ULONG)rows[r] << shift;
		UWORD hi = m >> 16, lo = m;
		UWORD *q = rp->base + row_off[py] + (g << 2);
		if (g >= 0 && hi)
		{
			if (p0) q[0] |= hi;
			if (p1) q[1] |= hi;
			if (p2) q[2] |= hi;
			if (p3) q[3] |= hi;
		}
		if (lo && g + 1 < SCR_W / 16)
		{
			if (p0) q[4] |= lo;
			if (p1) q[5] |= lo;
			if (p2) q[6] |= lo;
			if (p3) q[7] |= lo;
		}
	}
}

void gfx_or16_row(struct RastPort *rp, const WORD *xs, int n, WORD y,
                  const UWORD *rows, int h, int col)
{
	UWORD off[16], mask[16];
	UWORD hi[2][16], lo[2][16];
	int shifts[2] = { -1, -1 };
	int nr = 0, last = -1, p0 = -1, p1 = -1, nplanes = 0;
	int ymin = SCR_H, ymax = -1, xmin = SCR_W, xmax = -1;

	/* distinct physical rows (rows mapping to the same line are merged) */
	for (int r = 0; r < h && r < 16; r++)
	{
		int py = map_y(y + r);
		if ((unsigned)py >= SCR_H)
			continue;
		if (py == last)
			mask[nr - 1] |= rows[r];
		else
		{
			off[nr] = row_off[py];
			mask[nr++] = rows[r];
			last = py;
			if (py < ymin) ymin = py;
			ymax = py;
		}
	}
	if (!nr)
		return;
	for (int p = 0; p < 4; p++)
		if (col & (1 << p))
		{
			if (p0 < 0) p0 = p; else if (p1 < 0) p1 = p;
			nplanes++;
		}
	if (nplanes > 2)
		nplanes = 2;	/* (invader colours use at most 2 planes) */

	for (int i = 0; i < n; i++)
	{
		int x = xs[i];
		if (x < 0 || x > SCR_W - 16)
			continue;
		int sh = x & 15, k;
		/* sprites at a fixed spacing only use a couple of shifts */
		if (shifts[0] == sh) k = 0;
		else if (shifts[1] == sh) k = 1;
		else
		{
			k = shifts[0] < 0 ? 0 : 1;
			shifts[k] = sh;
			for (int r = 0; r < nr; r++)
			{
				ULONG m = (ULONG)mask[r] << (16 - sh);
				hi[k][r] = m >> 16;
				lo[k][r] = m;
			}
		}
		if (x < xmin) xmin = x;
		if (x > xmax) xmax = x;
		UWORD *base = rp->base + ((x >> 4) << 2);
		const UWORD *h1 = hi[k], *l1 = lo[k];
		if (nplanes == 1)
			for (int r = 0; r < nr; r++)
			{
				UWORD *q = base + off[r] + p0;
				q[0] |= h1[r];
				q[4] |= l1[r];
			}
		else
			for (int r = 0; r < nr; r++)
			{
				UWORD *q = base + off[r];
				q[p0] |= h1[r];
				q[p0 + 4] |= l1[r];
				q[p1] |= h1[r];
				q[p1 + 4] |= l1[r];
			}
	}
	if (xmax >= 0)
		mark(rp, xmin, ymin, xmax + 15, ymax);
}

void gfx_mark(struct RastPort *rp, WORD x0, WORD y0, WORD x1, WORD y1)
{
	int py0 = map_y(y0), py1 = map_y(y1);
	if (x0 < 0) x0 = 0;
	if (x1 >= SCR_W) x1 = SCR_W - 1;
	if (py0 < 0) py0 = 0;
	if (py1 >= SCR_H) py1 = SCR_H - 1;
	if (x0 <= x1 && py0 <= py1)
		mark(rp, x0, py0, x1, py1);
}

void gfx_clear(struct RastPort *rp)
{
	memset(rp->base, 0, SCR_BYTES);
}

void gfx_mask16(struct RastPort *rp, WORD x, WORD y, const UWORD *rows, int h, int col)
{
	const UWORD *c = cmask[col & 15];
	int shift = x & 15, g = x >> 4, ymin = SCR_H, ymax = -1;

	if (x <= -16 || x >= SCR_W)
		return;
	for (int r = 0; r < h; r++)
	{
		int py = map_y(y + r);
		if ((unsigned)py >= SCR_H || !rows[r])
			continue;
		if (py < ymin) ymin = py;
		if (py > ymax) ymax = py;
		ULONG m = (ULONG)rows[r] << (16 - shift);
		UWORD *q = rp->base + row_off[py] + (g << 2);
		if (g >= 0)
			group_set(q, m >> 16, c);
		if ((m & 0xffff) && g + 1 < SCR_W / 16)
			group_set(q + 4, m, c);
	}
	if (ymax >= 0)
	{
		int x0 = x < 0 ? 0 : x, x1 = x + 15 >= SCR_W ? SCR_W - 1 : x + 15;
		mark(rp, x0, ymin, x1, ymax);
	}
}

static int op_equal(const struct Op *a, const struct Op *b)
{
	return a->type == b->type && a->apen == b->apen && a->x0 == b->x0 &&
	       a->y0 == b->y0 && a->x1 == b->x1 && a->y1 == b->y1 &&
	       (a->type != OP_TEXT || a->bpen == b->bpen) &&
	       (a->type != OP_CUSTOM || (a->fn == b->fn && a->arg == b->arg)) &&
	       ((a->type != OP_TEXT && a->type != OP_CUSTOM) ||
	        (a->len == b->len && memcmp(a->text, b->text, a->len) == 0));
}

static int op_overlaps(const struct Op *a, const struct Op *b)
{
	return a->g0 <= b->g1 && b->g0 <= a->g1 && a->by0 <= b->by1 && b->by0 <= a->by1;
}

static void add_dirty(struct DirtyList *d, int g0, int g1, int y0, int y1)
{
	if (d->full)
		return;
	if (d->n)
	{
		/* merge with previous rectangle when in the same columns and
		 * touching/overlapping vertically (text lines, bars) */
		struct { WORD g0, g1, y0, y1; } *l = (void *)&d->r[d->n - 1];
		if (l->g0 == g0 && l->g1 == g1 && y0 <= l->y1 + 1 && y1 >= l->y0 - 1)
		{
			if (y0 < l->y0) l->y0 = y0;
			if (y1 > l->y1) l->y1 = y1;
			return;
		}
	}
	if (d->n >= MAX_DIRTY)
	{
		d->full = 1;
		return;
	}
	d->r[d->n].g0 = g0;
	d->r[d->n].g1 = g1;
	d->r[d->n].y0 = y0;
	d->r[d->n].y1 = y1;
	d->n++;
}

static void copy_rect(UWORD *dst, const UWORD *src, int g0, int g1, int y0, int y1)
{
	int off = g0 << 2, longs = (g1 - g0 + 1) * 2;
	for (int y = y0; y <= y1; y++)
	{
		const ULONG *s = (const ULONG *)(src + row_off[y] + off);
		ULONG *t = (ULONG *)(dst + row_off[y] + off);
		for (int k = 0; k < longs; k++)
			t[k] = s[k];
	}
}

static void op_draw(const struct Op *op)
{
	rp_draw.apen = op->apen;
	rp_draw.bpen = op->bpen;
	switch (op->type)
	{
	case OP_PIXEL:
		WritePixel(&rp_draw, op->x0, op->y0);
		break;
	case OP_LINE:
		Move(&rp_draw, op->x0, op->y0);
		Draw(&rp_draw, op->x1, op->y1);
		break;
	case OP_RECT:
		RectFill(&rp_draw, op->x0, op->y0, op->x1, op->y1);
		break;
	case OP_TEXT:
		Move(&rp_draw, op->x0, op->y0);
		Text(&rp_draw, op->text, op->len);
		break;
	case OP_CUSTOM:
		op->fn(&rp_draw, op->ctx ? op->ctx : op->text, op->x0, op->y0, op->arg);
		break;
	}
}

/* Diff this frame's HUD operations against the previous frame's:
 * vanished ones are erased (scenery copied back), new/changed ones and
 * anything overlapping an erased area are drawn, all into the
 * background buffer; touched areas are invalidated on both screens.
 */
void gfx_hud_keep(void)
{
	n_cur = 0;		/* previous frame's HUD stays as it is */
}

void gfx_hud_commit(void)
{
	int i, j, n_erased = 0;
	struct Op *t;

	for (i = 0; i < n_cur; i++)
	{
		struct Op *c = &cur_ops[i];
		/* operations usually come in the same order every frame */
		if (i < n_prev && !prev_ops[i].matched && op_equal(c, &prev_ops[i]))
		{
			c->matched = prev_ops[i].matched = 1;
			continue;
		}
		/* small window around the same position: ops shift a little
		 * when items appear/disappear; a full search would be O(n^2)
		 */
		int lo = i - 8 < 0 ? 0 : i - 8, hi = i + 8 > n_prev ? n_prev : i + 8;
		for (j = lo; j < hi; j++)
		{
			if (!prev_ops[j].matched && op_equal(c, &prev_ops[j]))
			{
				c->matched = prev_ops[j].matched = 1;
				break;
			}
		}
	}

	/* erase vanished operations */
	for (j = 0; j < n_prev; j++)
	{
		struct Op *o = &prev_ops[j];
		if (o->matched)
			continue;
		copy_rect(bgscreen, scenery, o->g0, o->g1, o->by0, o->by1);
		add_dirty(&dirty[0], o->g0, o->g1, o->by0, o->by1);
		add_dirty(&dirty[1], o->g0, o->g1, o->by0, o->by1);
		o->type = 0xff;		/* mark as erased area */
		n_erased++;
	}

	/* draw new operations, and old ones hit by an erase or drawn over
	 * by an earlier redrawn one (painter's order: a HUD background
	 * rectangle redrawn because one number changed must not cover the
	 * unchanged labels drawn after it) */
	enum { MAX_AREAS = 16 };
	struct Op area[MAX_AREAS];
	int drawn = 0;
	for (i = 0; i < n_cur; i++)
	{
		struct Op *c = &cur_ops[i];
		int draw = !c->matched;
		for (j = 0; !draw && n_erased && j < n_prev; j++)
			draw = prev_ops[j].type == 0xff && op_overlaps(c, &prev_ops[j]);
		for (j = 0; !draw && j < drawn; j++)
			draw = op_overlaps(c, &area[j]);
		if (draw)
		{
			if (drawn < MAX_AREAS)
				area[drawn++] = *c;
			else
			{
				/* too many: widen the last one */
				struct Op *a = &area[MAX_AREAS - 1];
				if (c->g0 < a->g0) a->g0 = c->g0;
				if (c->g1 > a->g1) a->g1 = c->g1;
				if (c->by0 < a->by0) a->by0 = c->by0;
				if (c->by1 > a->by1) a->by1 = c->by1;
			}
			op_draw(c);
			add_dirty(&dirty[0], c->g0, c->g1, c->by0, c->by1);
			add_dirty(&dirty[1], c->g0, c->g1, c->by0, c->by1);
		}
		c->matched = 0;
	}

	t = prev_ops;
	prev_ops = cur_ops;
	cur_ops = t;
	n_prev = n_cur;
	n_cur = 0;
}

void gfx_restore_back(void)
{
	struct DirtyList *d = &dirty[back];

	if (d->full)
		memcpy(screen[back], bgscreen, SCR_BYTES);
	else
		for (int i = 0; i < d->n; i++)
			copy_rect(screen[back], bgscreen, d->r[i].g0, d->r[i].g1, d->r[i].y0, d->r[i].y1);
	d->n = d->full = 0;
}

static int min_vbls = 1;
static long last_swap;

void gfx_or_mode(int on)
{
	rp_screen[0].ormode = rp_screen[1].ormode = on;
}

void gfx_set_frame_vbls(int n)
{
	min_vbls = n < 1 ? 1 : n;
}

void gfx_swap(void)
{
	ULONG addr = (ULONG)screen[back];
	long vbl = *(volatile long *)0x466;	/* _frclock */

	/* steady pacing: show a new frame at most every min_vbls VBLs */
	while (*(volatile long *)0x466 - last_swap < min_vbls - 1)
		;
	vbl = *(volatile long *)0x466;

	/* write the video base directly (Setscreen() waits for a VBL by
	 * itself on some TOS versions); the shifter latches it at the
	 * next VBL, which we then wait for
	 */
	*(volatile UBYTE *)0xffff8201 = addr >> 16;
	*(volatile UBYTE *)0xffff8203 = addr >> 8;
	while (*(volatile long *)0x466 == vbl)
		;
	last_swap = *(volatile long *)0x466;
	back ^= 1;
}

/* ------------------------------------------------------------------ */
/* tiny sprintf, see st_gfx.h */

int gfx_sprintf(char *buf, const char *fmt, ...)
{
	va_list ap;
	char *out = buf;

	va_start(ap, fmt);
	while (*fmt)
	{
		if (*fmt != '%')
		{
			*out++ = *fmt++;
			continue;
		}
		fmt++;
		int zero = 0, width = 0;
		if (*fmt == '0')
		{
			zero = 1;
			fmt++;
		}
		while (*fmt >= '0' && *fmt <= '9')
			width = width * 10 + (*fmt++ - '0');
		if (*fmt == 'l')
			fmt++;
		switch (*fmt++)
		{
		case 's':
		{
			const char *s = va_arg(ap, const char *);
			while (*s)
				*out++ = *s++;
			break;
		}
		case 'c':
			*out++ = (char)va_arg(ap, int);
			break;
		case 'd':
		{
			long v = va_arg(ap, long);
			char tmp[12];
			int n = 0, neg = v < 0;
			unsigned long u = neg ? -(unsigned long)v : (unsigned long)v;
			do {
				tmp[n++] = '0' + u % 10;
				u /= 10;
			} while (u);
			if (neg)
				width--;
			if (neg && zero)
				*out++ = '-';
			while (n < width--)
				*out++ = zero ? '0' : ' ';
			if (neg && !zero)
				*out++ = '-';
			while (n)
				*out++ = tmp[--n];
			break;
		}
		case '%':
			*out++ = '%';
			break;
		}
	}
	va_end(ap);
	*out = '\0';
	return out - buf;
}

/* ------------------------------------------------------------------ */
/* stars */

void gfx_stars(struct RastPort *rp, const WORD *pts, int n, int stride, const WORD *ground)
{
	int b = (rp == &rp_screen[0]) ? 0 : 1;
	UWORD *base = rp->base;
	UWORD *offs = star_off[b];
	int i, cnt = 0;

	if (rp != &rp_screen[0] && rp != &rp_screen[1])
		return;

	/* undo this buffer's previous stars: copy their groups from background */
	for (i = 0; i < star_n[b]; i++)
	{
		ULONG *t = (ULONG *)(base + offs[i]);
		const ULONG *f = (const ULONG *)(bgscreen + offs[i]);
		t[0] = f[0];
		t[1] = f[1];
	}

	for (i = 0; i < n && cnt < MAX_STARS_ST; i++, pts += stride)
	{
		int x = pts[0], y = pts[1];
		if ((unsigned)x >= SCR_W)
			continue;
		if (ground && y >= ground[x])
			continue;
		int py = map_y(y);
		if ((unsigned)py >= SCR_H)
			continue;
		UWORD off = row_off[py] + ((x >> 4) << 2);
		group_set(base + off, 0x8000 >> (x & 15), cmask[pts[2] & 15]);
		offs[cnt++] = off;
	}
	star_n[b] = cnt;
}

/* ------------------------------------------------------------------ */
/* pre-shifted sprites */

struct gfx_sprite {
	WORD py0, rows, groups;		/* physical first row, rows, groups incl. shift */
	UWORD *data[16];		/* per shift: rows x groups x (mask, p0..p3) */
};

static UWORD *scratch;

gfx_sprite *gfx_sprite_build(gfx_draw_fn fn, const void *ctx, WORD arg, WORD y, WORD w, WORD h)
{
	return gfx_sprite_build_on(fn, ctx, arg, y, w, h, -1);
}

gfx_sprite *gfx_sprite_build_on(gfx_draw_fn fn, const void *ctx, WORD arg, WORD y, WORD w, WORD h, int bg)
{
	struct RastPort rp;
	gfx_sprite *spr;
	int py0 = map_y(y), py1 = map_y(y + h - 1), ng = (w + 15) / 16;
	UWORD *img;

	if (py0 < 0) py0 = 0;
	if (py1 >= SCR_H) py1 = SCR_H - 1;
	if (py1 < py0 || ng > 7)
		return NULL;
	if (!scratch && !(scratch = (UWORD *)Malloc(SCR_BYTES)))
		return NULL;
	spr = (gfx_sprite *)Malloc(sizeof(*spr));
	if (!spr)
		return NULL;
	spr->py0 = py0;
	spr->rows = py1 - py0 + 1;
	spr->groups = ng + 1;

	/* render over colour 0 and over colour 15: a pixel belongs to the
	 * sprite where either rendering differs from its background */
	img = (UWORD *)Malloc((long)spr->rows * ng * 5 * 2);
	if (!img)
		return NULL;
	memset(&rp, 0, sizeof(rp));
	rp.base = scratch;
	for (int pass = 0; pass < (bg >= 0 ? 1 : 2); pass++)
	{
		for (int r = 0; r < spr->rows; r++)
			for (int g = 0; g < ng * 4; g++)
				scratch[row_off[py0 + r] + g] =
					bg >= 0 ? cmask[bg & 15][g & 3] : pass ? 0xffff : 0;
		fn(&rp, ctx, 0, y, arg);
		for (int r = 0; r < spr->rows; r++)
			for (int g = 0; g < ng; g++)
			{
				const UWORD *q = scratch + row_off[py0 + r] + g * 4;
				UWORD *o = img + (r * ng + g) * 5;
				if (bg >= 0)
				{
					o[0] = 0xffff;		/* composited: opaque */
					o[1] = q[0]; o[2] = q[1]; o[3] = q[2]; o[4] = q[3];
				}
				else if (!pass)
				{
					o[0] = q[0] | q[1] | q[2] | q[3];	/* drawn non-zero */
					o[1] = q[0]; o[2] = q[1]; o[3] = q[2]; o[4] = q[3];
				}
				else
					o[0] |= ~(q[0] & q[1] & q[2] & q[3]);	/* drawn non-15 */
			}
	}
	/* 16 pre-shifted copies with one extra group */
	for (int sh = 0; sh < 16; sh++)
	{
		UWORD *d = (UWORD *)Malloc((long)spr->rows * spr->groups * 5 * 2);
		if (!d)
			return NULL;
		spr->data[sh] = d;
		for (int r = 0; r < spr->rows; r++)
			for (int g = 0; g < spr->groups; g++)
				for (int k = 0; k < 5; k++)
				{
					/* outside the image: transparent, or the
					 * composite colour (opaque) */
					UWORD out = bg < 0 ? 0 : k == 0 ? 0xffff : cmask[bg & 15][k - 1];
					UWORD cur = g < ng ? img[(r * ng + g) * 5 + k] : out;
					UWORD prev = g > 0 ? img[(r * ng + g - 1) * 5 + k] : out;
					/* data words are only valid under the mask */
					d[(r * spr->groups + g) * 5 + k] =
						sh ? (UWORD)((cur >> sh) | (prev << (16 - sh))) : cur;
				}
	}
	Mfree(img);
	return spr;
}

/* draw at logical x, y instead of the build row (objects moving
 * vertically: the 256 -> 200 line mapping of the build row is reused,
 * at most one row off).  Clipped at all edges.  In OR mode (sprites
 * over colour 0, see gfx_or_mode) the plane words are only ORed in. */
void gfx_sprite_draw_xy(struct RastPort *rp, const gfx_sprite *spr, WORD x, WORD y)
{
	int g0 = x >> 4, ng, gskip = 0, gcount, r0 = 0, rows, py;
	const UWORD *d;

	if (!spr)
		return;
	ng = gcount = spr->groups;
	rows = spr->rows;
	py = y < 0 ? -(int)GFX_Y(-y) : GFX_Y(y);
	if (x <= -16 * ng || x >= SCR_W || py >= SCR_H || py + rows <= 0)
		return;
	d = spr->data[x & 15];
	if (g0 < 0)
	{
		gskip = -g0;
		gcount -= gskip;
		g0 = 0;
	}
	if (g0 + gcount > SCR_W / 16)
		gcount = SCR_W / 16 - g0;
	if (py < 0)
	{
		r0 = -py;
		py = 0;
	}
	if (py + rows - r0 > SCR_H)
		rows = SCR_H - py + r0;
	for (int r = r0; r < rows; r++)
	{
		UWORD *q = rp->base + row_off[py + r - r0] + (g0 << 2);
		const UWORD *s = d + (r * ng + gskip) * 5;
		for (int g = 0; g < gcount; g++, q += 4, s += 5)
		{
			UWORD m = s[0];
			if (!m)
				continue;
			if (rp->ormode)
			{
				q[0] |= s[1]; q[1] |= s[2]; q[2] |= s[3]; q[3] |= s[4];
				continue;
			}
			UWORD k = ~m;
			q[0] = (q[0] & k) | (s[1] & m);
			q[1] = (q[1] & k) | (s[2] & m);
			q[2] = (q[2] & k) | (s[3] & m);
			q[3] = (q[3] & k) | (s[4] & m);
		}
	}
	if (rp->dirty && gcount > 0)
		mark(rp, g0 << 4, py, ((g0 + gcount) << 4) - 1, py + rows - r0 - 1);
}

void gfx_sprite_draw(struct RastPort *rp, const gfx_sprite *spr, WORD x)
{
	int g0 = x >> 4, ng = spr->groups;
	const UWORD *d;
	int gskip = 0, gcount = ng;

	if (!spr || x <= -16 * ng || x >= SCR_W)
		return;
	d = spr->data[x & 15];
	if (g0 < 0)
	{
		gskip = -g0;
		gcount -= gskip;
		g0 = 0;
	}
	if (g0 + gcount > SCR_W / 16)
		gcount = SCR_W / 16 - g0;
	for (int r = 0; r < spr->rows; r++)
	{
		UWORD *q = rp->base + row_off[spr->py0 + r] + (g0 << 2);
		const UWORD *s = d + (r * ng + gskip) * 5;
		for (int g = 0; g < gcount; g++, q += 4, s += 5)
		{
			UWORD m = s[0];
			if (!m)
				continue;
			if (m == 0xffff)
			{
				/* fully opaque group: plain copy of the 4 plane words */
				q[0] = s[1]; q[1] = s[2]; q[2] = s[3]; q[3] = s[4];
				continue;
			}
			UWORD k = ~m;
			q[0] = (q[0] & k) | (s[1] & m);
			q[1] = (q[1] & k) | (s[2] & m);
			q[2] = (q[2] & k) | (s[3] & m);
			q[3] = (q[3] & k) | (s[4] & m);
		}
	}
	if (rp->dirty)
		mark(rp, g0 << 4, spr->py0, ((g0 + gcount) << 4) - 1, spr->py0 + spr->rows - 1);
}
