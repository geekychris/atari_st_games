/*
 * Atari Falcon030 graphics layer, see fgfx.h.
 */
#include <osbind.h>
#include <mint/falcon.h>
#include <string.h>
#include "fgfx.h"

#define SCR_W 320
#define MAX_H 240

static void *mem;
static UWORD *screen[3];
static int back, height = MAX_H;
static struct RastPort rp_screen[3];
static UWORD lut[256];
static WORD ymap[256 + 64];		/* logical -32..287 */
static ULONG row_off[MAX_H];
static UBYTE ylog[MAX_H];		/* physical -> logical row */

static void *old_phys, *old_log;
static WORD old_mode;

static const UBYTE *font_data;
static UWORD font_width, font_first;

static void linea_init(void)
{
	register void *fonts __asm__("a1");
	__asm__ volatile (".dc.w 0xa000" : "=r"(fonts) : : "d0", "d1", "d2", "a0", "a2", "memory");
	const UBYTE *f8 = ((const UBYTE **)fonts)[1];
	font_first = *(const UWORD *)(f8 + 36);
	font_data = *(const UBYTE **)(f8 + 76);
	font_width = *(const UWORD *)(f8 + 80);
}

int fgfx_height(void) { return height; }

int fgfx_map_y(int y)
{
	if (y < -32 || y >= 256 + 32)
		return y < 0 ? -1 : height;
	return ymap[y + 32];
}

UWORD fgfx_pen(int pen) { return lut[pen & 255]; }

const WORD *fgfx_ymap(void) { return ymap + 32; }

void fgfx_set_rgb(int pen, int r, int g, int b)
{
	r = r < 0 ? 0 : r > 255 ? 255 : r;
	g = g < 0 ? 0 : g > 255 ? 255 : g;
	b = b < 0 ? 0 : b > 255 ? 255 : b;
	lut[pen & 255] = (UWORD)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

/* ------------------------------------------------------------------ */

void SetAPen(struct RastPort *rp, ULONG pen) { rp->apen = pen; }
void SetBPen(struct RastPort *rp, ULONG pen) { rp->bpen = pen; }
void SetDrMd(struct RastPort *rp, ULONG mode) { rp->drmd = mode; }
void Move(struct RastPort *rp, LONG x, LONG y) { rp->cx = x; rp->cy = y; }

static inline void hspan(UWORD *row, int x0, int x1, UWORD c)
{
	UWORD *p = row + x0;
	int n = x1 - x0 + 1;
	if (((ULONG)p & 2) && n)
	{
		*p++ = c;
		n--;
	}
	ULONG cc = ((ULONG)c << 16) | c, *q = (ULONG *)p;
	for (; n >= 8; n -= 8)
	{
		q[0] = cc; q[1] = cc; q[2] = cc; q[3] = cc;
		q += 4;
	}
	for (; n >= 2; n -= 2)
		*q++ = cc;
	if (n)
		*(UWORD *)q = c;
}

/* physical coordinates, clipped */
static void fill_phys(UWORD *base, int x0, int py0, int x1, int py1, UWORD c)
{
	if (x0 < 0) x0 = 0;
	if (x1 >= SCR_W) x1 = SCR_W - 1;
	if (py0 < 0) py0 = 0;
	if (py1 >= height) py1 = height - 1;
	for (int y = py0; y <= py1; y++)
		hspan(base + row_off[y], x0, x1, c);
}

void fgfx_fill(struct RastPort *rp, int x0, int y0, int x1, int y1, int pen)
{
	if (x0 > x1)
		return;
	fill_phys(rp->base, x0, fgfx_map_y(y0), x1, fgfx_map_y(y1), lut[pen & 255]);
}

void fgfx_strip8(struct RastPort *rp, int x, int y0, int y1, int pen)
{
	int py0 = fgfx_map_y(y0), py1 = fgfx_map_y(y1);
	if (py0 < 0) py0 = 0;
	if (py1 >= height) py1 = height - 1;
	int n = py1 - py0 + 1;
	if (n <= 0)
		return;
	UWORD c = lut[pen & 255];
	ULONG cc = ((ULONG)c << 16) | c;
	ULONG *p = (ULONG *)(rp->base + row_off[py0] + x);
	do
	{
		p[0] = cc; p[1] = cc; p[2] = cc; p[3] = cc;
		p += SCR_W / 2;
	} while (--n);
}

void fgfx_strip8_rows(struct RastPort *rp, int x, int y0, int y1, const UBYTE *pens)
{
	int py0 = fgfx_map_y(y0), py1 = fgfx_map_y(y1);
	if (py0 < 0) py0 = 0;
	if (py1 >= height) py1 = height - 1;
	ULONG *p = (ULONG *)(rp->base + row_off[py0] + x);
	for (int py = py0; py <= py1; py++)
	{
		UWORD c = lut[pens[ylog[py]]];
		ULONG cc = ((ULONG)c << 16) | c;
		p[0] = cc; p[1] = cc; p[2] = cc; p[3] = cc;
		p += SCR_W / 2;
	}
}

void RectFill(struct RastPort *rp, LONG x0, LONG y0, LONG x1, LONG y1)
{
	fgfx_fill(rp, x0, y0, x1, y1, rp->apen);
}

void SetRast(struct RastPort *rp, ULONG pen)
{
	fill_phys(rp->base, 0, 0, SCR_W - 1, height - 1, lut[pen & 255]);
}

static inline void plot(struct RastPort *rp, int x, int py, UWORD c)
{
	if ((unsigned)x < SCR_W && (unsigned)py < (unsigned)height)
	{
		UWORD *p = rp->base + row_off[py] + x;
		*p = rp->drmd == COMPLEMENT ? (UWORD)~*p : c;
	}
}

void WritePixel(struct RastPort *rp, LONG x, LONG y)
{
	plot(rp, x, fgfx_map_y(y), lut[rp->apen]);
}

void Draw(struct RastPort *rp, LONG x, LONG y)
{
	int x0 = rp->cx, y0 = fgfx_map_y(rp->cy), x1 = x, y1 = fgfx_map_y(y);
	int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
	int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
	int err = dx + dy;
	UWORD c = lut[rp->apen];

	rp->cx = x;
	rp->cy = y;
	if (y0 == y1 && rp->drmd != COMPLEMENT)
	{
		if ((unsigned)y0 < (unsigned)height)
		{
			int a = x0 < x1 ? x0 : x1, b = x0 < x1 ? x1 : x0;
			if (a < 0) a = 0;
			if (b >= SCR_W) b = SCR_W - 1;
			if (a <= b)
				hspan(rp->base + row_off[y0], a, b, c);
		}
		return;
	}
	for (int guard = 0; guard < 2000; guard++)
	{
		plot(rp, x0, y0, c);
		if (x0 == x1 && y0 == y1)
			break;
		int e2 = 2 * err;
		if (e2 >= dy) { err += dy; x0 += sx; }
		if (e2 <= dx) { err += dx; y0 += sy; }
	}
}

/* 8x8 system font; baseline semantics like topaz/8 (top = y - 6).
 * Only set pixels are visited (bfffo), background in JAM2 is 4 longs. */
void Text(struct RastPort *rp, const char *str, ULONG len)
{
	int x = rp->cx, top = fgfx_map_y(rp->cy) - 6;
	UWORD fg = lut[rp->apen], bg = lut[rp->bpen];
	ULONG bgl = ((ULONG)bg << 16) | bg;
	int jam2 = rp->drmd == JAM2;

	for (ULONG i = 0; i < len; i++, x += 8)
	{
		if (x < 0 || x > SCR_W - 8)
			continue;
		const UBYTE *g = font_data + ((UBYTE)str[i] - font_first);
		for (int r = 0; r < 8; r++)
		{
			int py = top + r;
			if ((unsigned)py >= (unsigned)height)
				continue;
			UWORD *p = rp->base + row_off[py] + x;
			if (jam2)
			{
				ULONG *q = (ULONG *)p;
				q[0] = bgl; q[1] = bgl; q[2] = bgl; q[3] = bgl;
			}
			unsigned bits = g[r * font_width];
			while (bits)
			{
				int b = __builtin_clz(bits) - 24;
				p[b] = fg;
				bits &= ~(0x80u >> b);
			}
		}
	}
	rp->cx = x;
}

/* ------------------------------------------------------------------ */

int fgfx_init(void)
{
	WORD mode, mon = VgetMonitor();
	long size;

	if (mon == MON_VGA)
		mode = BPS16 | COL40 | VGA | VERTFLAG;	/* 320x240 */
	else
		mode = BPS16 | COL40 | PAL;		/* 320x200 */
	size = VgetSize(mode);
	height = size / (SCR_W * 2);
	if (height > MAX_H)
		height = MAX_H;

	/* three buffers: the one shown, the one queued for the next VBL,
	 * the one being drawn, so drawing never waits for the display */
	mem = (void *)Mxalloc(3 * size + 256, 0);	/* ST RAM */
	if (!mem || (long)mem < 0)
		return 1;
	UBYTE *p = (UBYTE *)(((ULONG)mem + 255) & ~255UL);
	screen[0] = (UWORD *)p;
	screen[1] = (UWORD *)(p + size);
	screen[2] = (UWORD *)(p + 2 * size);
	memset(p, 0, 3 * size);

	for (int y = 0; y < height; y++)
		row_off[y] = (ULONG)y * SCR_W;
	for (int y = -32; y < 256 + 32; y++)
		ymap[y + 32] = (y * height) >> 8;
	for (int y = 255; y >= 0; y--)
		ylog[ymap[y + 32]] = y;
	for (int i = 0; i < 3; i++)
	{
		rp_screen[i].base = screen[i];
		rp_screen[i].drmd = JAM2;
	}
	linea_init();

	old_phys = Physbase();
	old_log = Logbase();
	old_mode = VsetMode(-1);
	VsetScreen(screen[0], screen[0], 3, mode);
	back = 1;
	return 0;
}

void fgfx_exit(void)
{
	VsetScreen(old_log, old_phys, 3, old_mode);
	if (mem)
		Mfree(mem);
	mem = NULL;
}

struct RastPort *fgfx_back(void) { return &rp_screen[back]; }

void fgfx_swap(void)
{
	/* new screen address via XBIOS (Falcon video base registers differ
	 * from the ST); it takes effect at the next VBL.  With three buffers
	 * the next one to draw is neither shown nor queued, so no wait. */
	VsetScreen((void *)-1L, screen[back], -1, -1);
	back = back == 2 ? 0 : back + 1;
}
