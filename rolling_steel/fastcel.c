/*
 * The cel engine for a stock Falcon030: see fastcel.h. Atari Falcon port.
 */
#include "fastcel.h"
#include "softcel.h"                    /* PIXC_* */

#define MAXH 256

static unsigned short *F;
static int FW, FH, CX0, CX1 = 320, XOFF;

void fc_setup(unsigned short *frame, int w, int h)
{
    F = frame;
    FW = w;
    FH = h > MAXH ? MAXH : h;
    CX0 = 0;
    CX1 = w;
    XOFF = 0;
}

void fc_clip(int x0, int x1, int xoff)
{
    CX0 = x0 < 0 ? 0 : x0;
    CX1 = x1 > FW ? FW : x1;
    XOFF = xoff;
}

/* n pixels of c from p, as longwords */
static void span(unsigned short *p, int n, unsigned long c2)
{
    unsigned long *q;
    int k;
    if ((long)p & 2) { *p++ = (unsigned short)c2; n--; }
    q = (unsigned long *)p;
    k = n >> 1;
    while (k >= 8) {
        q[0] = c2; q[1] = c2; q[2] = c2; q[3] = c2;
        q[4] = c2; q[5] = c2; q[6] = c2; q[7] = c2;
        q += 8;
        k -= 8;
    }
    while (k-- > 0) *q++ = c2;
    if (n & 1) *(unsigned short *)q = (unsigned short)c2;
}

void fc_clear(unsigned short c)
{
    if (F) span(F, FW * FH, ((unsigned long)c << 16) | c);
}

/* ---- blending, in RGB565 ---- */

/* x * k / 8 for each of r, g, b: the shifts' masks keep each field's bits */
static unsigned short darken(unsigned short d, int k)
{
    unsigned short v = 0;
    if (k >= 8) return d;
    if (k & 4) v += (d >> 1) & 0x7BEF;
    if (k & 2) v += (d >> 2) & 0x39E7;
    if (k & 1) v += (d >> 3) & 0x18E3;
    return v;
}

/* a span darkened to k/8, two pixels per longword: each mask also clears
 * the bits the shift brings over from the neighbouring pixel */
static void darken_span(unsigned short *p, int n, int k)
{
    unsigned long *q;
    int m;
    if (k >= 8) return;
    if ((long)p & 2) { *p = darken(*p, k); p++; n--; }
    q = (unsigned long *)p;
    for (m = n >> 1; m > 0; m--, q++) {
        unsigned long d = *q, v = 0;
        if (k & 4) v += (d >> 1) & 0x7BEF7BEFUL;
        if (k & 2) v += (d >> 2) & 0x39E739E7UL;
        if (k & 1) v += (d >> 3) & 0x18E318E3UL;
        *q = v;
    }
    if (n & 1) *(unsigned short *)q = darken(*(unsigned short *)q, k);
}

unsigned short fc_blend(unsigned short dst, unsigned short src, unsigned long pixc)
{
    unsigned long h = pixc >> 16;
    int r, g, b;
    if (pixc == PIXC_GHOST)
        return (unsigned short)(((dst >> 1) & 0x7BEF) + ((src >> 1) & 0x7BEF));
    if (pixc == PIXC_ADD) {
        r = (dst >> 11) + (src >> 11);               if (r > 31) r = 31;
        g = ((dst >> 5) & 63) + ((src >> 5) & 63);   if (g > 63) g = 63;
        b = (dst & 31) + (src & 31);                 if (b > 31) b = 31;
        return (unsigned short)((r << 11) | (g << 5) | b);
    }
    if (h & 0x8000)                                  /* the frame darkened to k/8 */
        return darken(dst, (int)((h >> 10) & 7) + 1);
    return src;
}

/* ---- quads: two edge steppers down the sides, a span per row ---- */

typedef struct { int ya, yb; long x, slope; } Edge;

/* the edge's rows (centres in [y0, y1), within the frame), its x at the
 * first and its step per row; 0 if it covers no row */
static int edge(long x0, long y0, long x1, long y1, Edge *e)
{
    long t, dx, dy;
    if (y0 == y1) return 0;
    if (y0 > y1) { t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
    e->ya = (int)((y0 + 32767) >> 16);
    e->yb = (int)((y1 + 32767) >> 16);
    if (e->ya < 0) e->ya = 0;
    if (e->yb > FH) e->yb = FH;
    if (e->ya >= e->yb) return 0;
    dx = x1 - x0;
    dy = y1 - y0;
    /* 16.16 per row with one 32-bit divide */
    if (dx < (1L << 25) && dx > -(1L << 25) && (dy >> 10))
        e->slope = (dx << 6) / (dy >> 10);
    else if (dy >> 8)
        e->slope = (dx / (dy >> 8)) << 8;
    else
        e->slope = 0;
    t = ((long)e->ya << 16) + 32768 - y0;             /* from y0 to the first centre */
    e->x = x0 + (t >> 16) * e->slope + ((t & 0xFFFF) >> 8) * (e->slope >> 8);
    return 1;
}

#if defined(__mc68020__)
/* The rows of a solid span between two edges, from row to rend: the inner
 * loop, in registers and small enough for the 68030's 256-byte
 * instruction cache. x >> 16 rounded is add 0x7fff, swap, ext.l. */
static void rows_solid(long *pxl, long *pxr, long sl, long sr, unsigned short *row,
                       unsigned short *rend, long lim0, long lim1, long stride, unsigned long c2)
{
    long xl = *pxl, xr = *pxr, t1, t2;
    __asm__ volatile (
        "1:	move.l	%0,%4\n"
        "	move.l	%1,%5\n"
        "	cmp.l	%5,%4\n"
        "	ble.s	2f\n"
        "	exg	%4,%5\n"
        "2:	add.l	#0x7fff,%4\n"
        "	swap	%4\n"
        "	ext.l	%4\n"
        "	add.l	#0x7fff,%5\n"
        "	swap	%5\n"
        "	ext.l	%5\n"
        "	cmp.l	%8,%4\n"
        "	bge.s	3f\n"
        "	move.l	%8,%4\n"
        "3:	cmp.l	%9,%5\n"
        "	ble.s	4f\n"
        "	move.l	%9,%5\n"
        "4:	sub.l	%4,%5\n"
        "	ble.s	8f\n"
        "	lea	(%6,%4.l*2),%%a1\n"
        "	move.l	%%a1,%4\n"
        "	btst	#1,%4\n"
        "	beq.s	5f\n"
        "	move.w	%10,(%%a1)+\n"
        "	subq.l	#1,%5\n"
        "5:	lsr.l	#1,%5\n"
        "	scs	%4\n"
        "	bra.s	7f\n"
        "6:	move.l	%10,(%%a1)+\n"
        "7:	dbra	%5,6b\n"
        "	tst.b	%4\n"
        "	beq.s	8f\n"
        "	move.w	%10,(%%a1)\n"
        "8:	add.l	%2,%0\n"
        "	add.l	%3,%1\n"
        "	adda.l	%11,%6\n"
        "	cmpa.l	%7,%6\n"
        "	bne	1b\n"
        : "+d" (xl), "+d" (xr), "+d" (sl), "+d" (sr), "=&d" (t1), "=&d" (t2), "+a" (row)
        : "a" (rend), "g" (lim0), "g" (lim1), "a" (c2), "g" (stride)
        : "a1", "cc", "memory");
    *pxl = xl;
    *pxr = xr;
}
#endif

/* A projected face is convex, so each row crosses two of its edges.
 * The edges, in order of their first row, are taken two at a time. */
void fc_quad(const long *x, const long *y, unsigned short c, unsigned long pixc)
{
    Edge e[4], *a, *b, t;
    int n = 0, i, j, next, yy, lim0 = CX0 - XOFF, lim1 = CX1 - XOFF, k = 0;
    unsigned long c2 = ((unsigned long)c << 16) | c;
    unsigned short *row;
    if (!F) return;
    for (i = 0; i < 4; i++) {
        j = (i + 1) & 3;
        if (edge(x[i], y[i], x[j], y[j], &e[n])) n++;
    }
    if (n < 2) return;
    for (i = 1; i < n; i++)                          /* by first row */
        for (j = i; j > 0 && e[j].ya < e[j - 1].ya; j--) { t = e[j]; e[j] = e[j - 1]; e[j - 1] = t; }
    if (pixc && ((pixc >> 16) & 0x8000)) k = (int)((pixc >> 26) & 7) + 1;
    a = &e[0]; b = &e[1]; next = 2;
    yy = a->ya;
    row = F + (long)yy * FW + XOFF;
    for (;;) {
        int end = a->yb < b->yb ? a->yb : b->yb;
        long xl = a->x, xr = b->x, sl = a->slope, sr = b->slope;
#if defined(__mc68020__)
        if (!pixc) {
            if (yy < end) {
                rows_solid(&xl, &xr, sl, sr, row, row + (long)(end - yy) * FW,
                           lim0, lim1, (long)FW * 2, c2);
                row += (long)(end - yy) * FW;
                yy = end;
            }
        } else
#endif
        for (; yy < end; yy++, row += FW, xl += sl, xr += sr) {
            int xa, xb, xx;
            if (xl <= xr) { xa = (int)((xl + 32767) >> 16); xb = (int)((xr + 32767) >> 16); }
            else          { xa = (int)((xr + 32767) >> 16); xb = (int)((xl + 32767) >> 16); }
            if (xa < lim0) xa = lim0;
            if (xb > lim1) xb = lim1;
            if (xa >= xb) continue;
            if (!pixc) span(row + xa, xb - xa, c2);
            else if (k) darken_span(row + xa, xb - xa, k);
            else for (xx = xa; xx < xb; xx++) row[xx] = fc_blend(row[xx], c, pixc);
        }
        a->x = xl; a->ya = yy;
        b->x = xr; b->ya = yy;
        /* replace whichever edge ended */
        if (a->yb <= yy) { if (next >= n) break; a = &e[next++]; }
        if (b->yb <= yy) { if (next >= n) break; b = &e[next++]; }
        if (a->ya > yy || b->ya > yy) {              /* a gap (not convex): skip to it */
            int ny = a->ya > b->ya ? a->ya : b->ya;
            row += (long)(ny - yy) * FW;
            yy = ny;
        }
        /* an edge that began above this row: bring it down to it */
        if (a->ya < yy) { if (a->yb <= yy) break; a->x += a->slope * (yy - a->ya); a->ya = yy; }
        if (b->ya < yy) { if (b->yb <= yy) break; b->x += b->slope * (yy - b->ya); b->ya = yy; }
    }
}

/* ---- sprites ---- */

void fc_sprite(const unsigned short *tex, int tw, int th, long x0, long y0, long w, long h,
               unsigned long pixc)
{
    int xa, xb, ya, yb, x, y, lim0 = CX0 - XOFF, lim1 = CX1 - XOFF;
    long du, dv, u0, v, off;
    if (!F || !tex || (w >> 10) <= 0 || (h >> 10) <= 0) return;
    xa = (int)((x0 + 32767) >> 16); xb = (int)((x0 + w + 32767) >> 16);
    ya = (int)((y0 + 32767) >> 16); yb = (int)((y0 + h + 32767) >> 16);
    if (xb <= lim0 || xa >= lim1 || yb <= 0 || ya >= FH) return;
    du = ((long)tw << 22) / (w >> 10);               /* texels per pixel, 16.16 */
    dv = ((long)th << 22) / (h >> 10);
    off = ((long)xa << 16) + 32768 - x0;
    u0 = (off >> 8) * (du >> 8);
    off = ((long)ya << 16) + 32768 - y0;
    v = (off >> 8) * (dv >> 8);
    if (xa < lim0) { u0 += du * (lim0 - xa); xa = lim0; }
    if (ya < 0) { v += dv * -ya; ya = 0; }
    if (xb > lim1) xb = lim1;
    if (yb > FH) yb = FH;
    for (y = ya; y < yb; y++, v += dv) {
        const unsigned short *trow;
        unsigned short *row = F + (long)y * FW + XOFF;
        long u = u0;
        int tv = (int)(v >> 16);
        if (tv >= th) break;
        if (tv < 0) continue;
        trow = tex + (long)tv * tw;
        if (!pixc) {
            for (x = xa; x < xb; x++, u += du) {
                unsigned int tu = (unsigned int)(u >> 16);
                unsigned short c;
                if (tu >= (unsigned int)tw) continue;
                c = trow[tu];
                if (c) row[x] = c;
            }
        } else {
            for (x = xa; x < xb; x++, u += du) {
                unsigned int tu = (unsigned int)(u >> 16);
                unsigned short c;
                if (tu >= (unsigned int)tw) continue;
                c = trow[tu];
                if (c) row[x] = fc_blend(row[x], c, pixc);
            }
        }
    }
}
