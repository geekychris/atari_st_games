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
enum { OP_PIXEL, OP_LINE, OP_RECT, OP_TEXT, OP_BIG };
#define MAX_OPS  200
#define TEXT_MAX 40
struct Op {
	UBYTE type, apen, bpen, len;
	WORD x0, y0, x1, y1;		/* logical coordinates */
	WORD g0, g1, by0, by1;		/* physical bbox: groups, rows */
	UBYTE matched;
	gfx_big_fn big;			/* OP_BIG: drawing function */
	char text[TEXT_MAX];
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

static void mark(struct RastPort *rp, int x0, int y0, int x1, int y1)
{
	struct DirtyList *d = rp->dirty;
	if (!d || d->full)
		return;
	if (d->n)
	{
		/* merge consecutive marks in the same columns */
		int g0 = x0 >> 4, g1 = x1 >> 4;
		if (d->r[d->n - 1].g0 == g0 && d->r[d->n - 1].g1 == g1 &&
		    y0 <= d->r[d->n - 1].y1 + 1 && y1 >= d->r[d->n - 1].y0 - 1)
		{
			if (y0 < d->r[d->n - 1].y0) d->r[d->n - 1].y0 = y0;
			if (y1 > d->r[d->n - 1].y1) d->r[d->n - 1].y1 = y1;
			return;
		}
	}
	if (d->n >= MAX_DIRTY)
	{
		d->full = 1;
		return;
	}
	d->r[d->n].g0 = x0 >> 4;
	d->r[d->n].g1 = x1 >> 4;
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
void SetBPen(struct RastPort *rp, ULONG pen) { rp->bpen = pen & 15; }

void Move(struct RastPort *rp, WORD x, WORD y)
{
	rp->cx = x;
	rp->cy = y;
}

static void record(struct RastPort *rp, int type, int x0, int y0, int x1, int y1,
                   const char *text, int len);

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
	int x0 = rp->cx, y0 = map_y(rp->cy), x1 = x, y1 = map_y(y);
	int bx0 = x0 < x1 ? x0 : x1, bx1 = x0 < x1 ? x1 : x0;
	int by0 = y0 < y1 ? y0 : y1, by1 = y0 < y1 ? y1 : y0;

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
	{
		/* Bresenham */
		UWORD m[4];
		int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
		int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
		int err = dx + dy;
		color_masks(rp->apen, m);
		for (;;)
		{
			if ((unsigned)x0 < SCR_W && (unsigned)y0 < SCR_H)
			{
				UWORD *q = rp->base + row_off[y0] + ((x0 >> 4) << 2);
				UWORD bit = 0x8000 >> (x0 & 15), nbit = ~bit;
				q[0] = (q[0] & nbit) | (bit & m[0]);
				q[1] = (q[1] & nbit) | (bit & m[1]);
				q[2] = (q[2] & nbit) | (bit & m[2]);
				q[3] = (q[3] & nbit) | (bit & m[3]);
			}
			if (x0 == x1 && y0 == y1)
				break;
			int e2 = 2 * err;
			if (e2 >= dy) { err += dy; x0 += sx; }
			if (e2 <= dx) { err += dx; y0 += sy; }
		}
	}
	if (bx0 < 0) bx0 = 0;
	if (by0 < 0) by0 = 0;
	if (bx1 >= SCR_W) bx1 = SCR_W - 1;
	if (by1 >= SCR_H) by1 = SCR_H - 1;
	mark(rp, bx0, by0, bx1, by1);
}

void RectFill(struct RastPort *rp, WORD x0, WORD y0, WORD x1, WORD y1)
{
	if (rp->record)
	{
		record(rp, OP_RECT, x0, y0, x1, y1, NULL, 0);
		return;
	}
	int py0 = map_y(y0), py1 = map_y(y1);
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
		int g0 = x0 >> 4, g1 = x1 >> 4;
		UWORD lmask = 0xffff >> (x0 & 15), rmask = 0xffff << (15 - (x1 & 15));
		UWORD *row = rp->base + row_off[py0] + (g0 << 2);
		if (g0 == g1)
		{
			UWORD m = lmask & rmask;
			for (int y = py0; y <= py1; y++, row += LINE_W)
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

	color_masks(rp->apen, fgm);
	color_masks(rp->bpen, bgm);
	for (ULONG i = 0; i < len; i++, x += 8)
	{
		if (x < 0 || x > SCR_W - 8)
			continue;
		const UBYTE *glyph = font_data + ((UBYTE)str[i] - font_first) + r0 * font_width;
		int shift = x & 15;
		UWORD *p = rp->base + row_off[top + r0] + ((x >> 4) << 2);

		if (shift <= 8)
		{
			/* glyph fits in one 16 pixel group */
			UWORD cell = 0xff00 >> shift, keep = ~cell;
			for (int r = r0; r <= r1; r++, glyph += font_width, p += LINE_W)
			{
				UWORD f = ((UWORD)*glyph << 8) >> shift, b = cell & ~f;
				p[0] = (p[0] & keep) | (f & fgm[0]) | (b & bgm[0]);
				p[1] = (p[1] & keep) | (f & fgm[1]) | (b & bgm[1]);
				p[2] = (p[2] & keep) | (f & fgm[2]) | (b & bgm[2]);
				p[3] = (p[3] & keep) | (f & fgm[3]) | (b & bgm[3]);
			}
		}
		else
		{
			/* straddles two groups */
			UWORD c1 = 0xff00 >> shift, c2 = 0xff00 << (16 - shift);
			UWORD k1 = ~c1, k2 = ~c2;
			for (int r = r0; r <= r1; r++, glyph += font_width, p += LINE_W)
			{
				UWORD g = (UWORD)*glyph << 8;
				UWORD f1 = g >> shift, b1 = c1 & ~f1;
				UWORD f2 = g << (16 - shift), b2 = c2 & ~f2;
				p[0] = (p[0] & k1) | (f1 & fgm[0]) | (b1 & bgm[0]);
				p[1] = (p[1] & k1) | (f1 & fgm[1]) | (b1 & bgm[1]);
				p[2] = (p[2] & k1) | (f1 & fgm[2]) | (b1 & bgm[2]);
				p[3] = (p[3] & k1) | (f1 & fgm[3]) | (b1 & bgm[3]);
				p[4] = (p[4] & k2) | (f2 & fgm[0]) | (b2 & bgm[0]);
				p[5] = (p[5] & k2) | (f2 & fgm[1]) | (b2 & bgm[1]);
				p[6] = (p[6] & k2) | (f2 & fgm[2]) | (b2 & bgm[2]);
				p[7] = (p[7] & k2) | (f2 & fgm[3]) | (b2 & bgm[3]);
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

int gfx_init(const UWORD *pal, int n)
{
	WORD stpal[16];
	UBYTE *p;

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

void gfx_bg_clear(void)
{
	memset(scenery, 0, SCR_BYTES);
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
	if (type == OP_TEXT || type == OP_BIG)
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
	else if (type == OP_BIG)
	{
		/* x1 = right edge, y1 = scale; glyphs are 7 rows high */
		px1 = x1;
		py0 = map_y(y0);
		py1 = map_y(y0 + 7 * y1 - 1);
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

void gfx_big(struct RastPort *rp, const char *str, WORD x, WORD y, WORD scale,
             WORD width, gfx_big_fn draw)
{
	if (!rp->record)
	{
		draw(rp, str, x, y, scale);
		return;
	}
	int n0 = n_cur;
	record(rp, OP_BIG, x, y, x + width - 1, scale, str, strlen(str));
	if (n_cur > n0)
		cur_ops[n0].big = draw;
}

static int op_equal(const struct Op *a, const struct Op *b)
{
	return a->type == b->type && a->apen == b->apen && a->x0 == b->x0 &&
	       a->y0 == b->y0 && a->x1 == b->x1 && a->y1 == b->y1 &&
	       (a->type != OP_TEXT || a->bpen == b->bpen) &&
	       ((a->type != OP_TEXT && a->type != OP_BIG) ||
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
	case OP_BIG:
	{
		char buf[TEXT_MAX + 1];
		memcpy(buf, op->text, op->len);
		buf[op->len] = '\0';
		op->big(&rp_draw, buf, op->x0, op->y0, op->y1);
		break;
	}
	}
}

/* Diff this frame's HUD operations against the previous frame's:
 * vanished ones are erased (scenery copied back), new/changed ones and
 * anything overlapping an erased area are drawn, all into the
 * background buffer; touched areas are invalidated on both screens.
 */
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

	/* draw new operations, and old ones hit by an erase */
	for (i = 0; i < n_cur; i++)
	{
		struct Op *c = &cur_ops[i];
		int draw = !c->matched;
		for (j = 0; !draw && n_erased && j < n_prev; j++)
			draw = prev_ops[j].type == 0xff && op_overlaps(c, &prev_ops[j]);
		if (draw)
		{
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
