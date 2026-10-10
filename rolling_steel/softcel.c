/*
 * The 3DO cel engine in software: see softcel.h. Integer only.
 * The same file is in planet_chomp and rolling_steel.
 */
#include "softcel.h"

#define MAXH 512
#define MAXTEX 40

static unsigned short *F;
static int FW, FH, YSC = 15, CX0, CX1 = 320, XOFF;
static long left[MAXH], right[MAXH];
static const unsigned short *tex_px[MAXTEX];
static int tex_w[MAXTEX], tex_h[MAXTEX], ntex;
long sc_count;
static long *ZB;
#define ZFAR 0x7FFFFFFFL

void sc_zbuffer(long *zb) { ZB = zb; }

void sc_zclear(void)
{
    long *p = ZB, *e = ZB + (long)FW * FH;
    if (!ZB) return;
#if defined(__MINT__) && defined(__mc68020__)
    /* Atari Falcon port: 32 bytes per movem (the buffer is a multiple) */
    __asm__ volatile (
        "	move.l	%2,%%d0\n"
        "	move.l	%%d0,%%d1\n	move.l	%%d0,%%d2\n	move.l	%%d0,%%d3\n"
        "	move.l	%%d0,%%d4\n	move.l	%%d0,%%d5\n	move.l	%%d0,%%d6\n"
        "	move.l	%%d0,%%a1\n"
        "1:	movem.l	%%d0-%%d6/%%a1,(%0)\n"
        "	lea	32(%0),%0\n"
        "	cmp.l	%1,%0\n"
        "	blt.s	1b\n"
        : "+a" (p) : "a" (e), "i" (ZFAR)
        : "d0", "d1", "d2", "d3", "d4", "d5", "d6", "a1", "cc", "memory");
#else
    while (p < e) *p++ = ZFAR;
#endif
}

void sc_setup(unsigned short *frame, int w, int h, int ysc)
{
    F = frame;
    FW = w;
    FH = h > MAXH ? MAXH : h;
    YSC = ysc;
    CX0 = 0;
    CX1 = w;
    XOFF = 0;
    sc_count = 0;
}

void sc_clip(int x0, int x1, int xoff)
{
    CX0 = x0 < 0 ? 0 : x0;
    CX1 = x1 > FW ? FW : x1;
    XOFF = xoff;
}

/* ---- blending ---- */

#ifdef SOFTCEL_RGB565
/* Atari Falcon port: the frame and the colours are the Falcon's RGB565
 * (rolling_steel's fastcel.c, which this calls, says how) */
#include "fastcel.h"
static unsigned short blend(unsigned short dst, unsigned short src, unsigned long pixc)
{
    return fc_blend(dst, src, pixc);
}
#else
static unsigned short blend(unsigned short dst, unsigned short src, unsigned long pixc)
{
    unsigned long h = pixc >> 16;
    int r, g, b;
    if (pixc == PIXC_GHOST)
        return (unsigned short)(((dst >> 1) & 0x3DEF) + ((src >> 1) & 0x3DEF));
    if (pixc == PIXC_ADD) {
        r = ((dst >> 10) & 31) + ((src >> 10) & 31); if (r > 31) r = 31;
        g = ((dst >> 5) & 31) + ((src >> 5) & 31);   if (g > 31) g = 31;
        b = (dst & 31) + (src & 31);                 if (b > 31) b = 31;
        return (unsigned short)((r << 10) | (g << 5) | b);
    }
    if (h & 0x8000) {                                /* the frame darkened to k/8 */
        int k = (int)((h >> 10) & 7) + 1;
        if (k == 4) return (unsigned short)((dst >> 1) & 0x3DEF);
        r = ((dst >> 10) & 31) * k >> 3;
        g = ((dst >> 5) & 31) * k >> 3;
        b = (dst & 31) * k >> 3;
        return (unsigned short)((r << 10) | (g << 5) | b);
    }
    return src;
}
#endif

/* ---- quads: edges into left / right extents, then spans ---- */

static void edge(long x0, long y0, long x1, long y1, int *ymin, int *ymax)
{
    long t, dx;
    int y, ya, yb;
    if (y0 == y1) return;
    if (y0 > y1) { t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
    /* the scanlines whose centres (y + 0.5) lie in [y0, y1) */
    ya = (int)((y0 - 32768 + 65535) >> 16);
    yb = (int)((y1 - 32768 + 65535) >> 16);
    if (ya < 0) ya = 0;
    if (yb > FH) yb = FH;
    if (ya >= yb) return;
    dx = (long)(((long long)(x1 - x0) << 16) / (y1 - y0));          /* 16.16 per row */
    {
        long yc = ((long)ya << 16) + 32768, x = x0 + (long)(((long long)(yc - y0) * dx) >> 16);
        for (y = ya; y < yb; y++, x += dx) {
            if (x < left[y]) left[y] = x;
            if (x > right[y]) right[y] = x;
        }
    }
    if (ya < *ymin) *ymin = ya;
    if (yb > *ymax) *ymax = yb;
}

/* the plane z = zx * X + zy * Y + z0 over the quad, in Z = z >> 4 units with
 * 8 fraction bits (so a long holds it), X and Y in pixels */
typedef struct { int on, write; long zx, zy; long long zc; } Plane;

static void quad_core(long ax, long ay, long bx, long by, long cx, long cy, long dx, long dy,
                      unsigned short rgb15, unsigned long pixc, const Plane *pl)
{
    int y, ymin = FH, ymax = 0, lim0 = CX0 - XOFF, lim1 = CX1 - XOFF;
    if (!F) return;
    if (YSC != 15) { ay = ay / 15 * YSC; by = by / 15 * YSC; cy = cy / 15 * YSC; dy = dy / 15 * YSC; }
    {
        long lo = ay, hi = ay;
        if (by < lo) lo = by; if (by > hi) hi = by;
        if (cy < lo) lo = cy; if (cy > hi) hi = cy;
        if (dy < lo) lo = dy; if (dy > hi) hi = dy;
        if (hi < 0 || lo >= (long)FH << 16) return;
        y = (int)(lo >> 16); if (y < 0) y = 0;
        ymax = (int)(hi >> 16) + 1; if (ymax > FH) ymax = FH;
        for (; y < ymax; y++) { left[y] = 0x7FFFFFFFL; right[y] = -0x7FFFFFFFL; }
        ymax = 0;
    }
    edge(ax, ay, bx, by, &ymin, &ymax);
    edge(bx, by, cx, cy, &ymin, &ymax);
    edge(cx, cy, dx, dy, &ymin, &ymax);
    edge(dx, dy, ax, ay, &ymin, &ymax);
    sc_count++;
    for (y = ymin; y < ymax; y++) {
        int xa, xb, x;
        unsigned short *row;
        if (left[y] > right[y]) continue;
        xa = (int)((left[y] - 32768 + 65535) >> 16);
        xb = (int)((right[y] - 32768 + 65535) >> 16);       /* exclusive */
        if (xa < lim0) xa = lim0;
        if (xb > lim1) xb = lim1;
        if (xa >= xb) continue;
        row = F + (long)y * FW + XOFF;
        if (pl && pl->on && ZB) {
            long *zr = ZB + (long)y * FW + XOFF;
            long zf = (long)(pl->zc + (long long)pl->zx * xa + (long long)pl->zy * y);
            for (x = xa; x < xb; x++, zf += pl->zx) {
                long z = zf >> 8;
                if (z >= zr[x]) continue;
                if (pl->write) zr[x] = z;
                row[x] = pixc ? blend(row[x], rgb15, pixc) : rgb15;
            }
        } else if (!pixc)
            for (x = xa; x < xb; x++) row[x] = rgb15;
        else
            for (x = xa; x < xb; x++) row[x] = blend(row[x], rgb15, pixc);
    }
}

void sc_quad(long ax, long ay, long bx, long by, long cx, long cy, long dx, long dy,
             unsigned short rgb15, unsigned long pixc)
{
    quad_core(ax, ay, bx, by, cx, cy, dx, dy, rgb15, pixc, 0);
}

/* the plane through three corners; 0 if they're in a line */
static int plane3(const long *x, const long *y, const long *z, int i, int j, int k, Plane *pl)
{
    long long x1 = (x[j] - x[i]) >> 8, y1 = (y[j] - y[i]) >> 8;       /* pixels * 256 */
    long long x2 = (x[k] - x[i]) >> 8, y2 = (y[k] - y[i]) >> 8;
    long long z1 = (z[j] - z[i]) >> 4, z2 = (z[k] - z[i]) >> 4;       /* Z */
    long long det = x1 * y2 - x2 * y1;                                 /* px^2 * 65536 */
    long long px0, py0;
    if (det == 0) return 0;
    pl->zx = (long)(((z1 * y2 - z2 * y1) << 16) / det);               /* Z * 256 per pixel */
    pl->zy = (long)(((x1 * z2 - x2 * z1) << 16) / det);
    /* at pixel centres: Z*256 at (X, Y) = zc + zx * X + zy * Y */
    px0 = x[i] - 32768; py0 = y[i] - 32768;                           /* pixel-centre offset */
    pl->zc = ((long long)(z[i] >> 4) << 8) - ((long long)pl->zx * px0 >> 16) - ((long long)pl->zy * py0 >> 16);
    pl->on = 1;
    return 1;
}

void sc_quad_z(const long *x, const long *y, const long *z, unsigned short rgb15,
               unsigned long pixc, int zwrite)
{
    Plane pl;
    pl.on = 0;
    pl.write = zwrite;
    if (YSC == 15 && !plane3(x, y, z, 0, 1, 3, &pl)) plane3(x, y, z, 0, 1, 2, &pl);
    quad_core(x[0], y[0], x[1], y[1], x[2], y[2], x[3], y[3], rgb15, pixc, &pl);
}

/* ---- textures ---- */

int sc_tex(int w, int h, const unsigned short *pixels)
{
    if (ntex >= MAXTEX || !pixels) return -1;
    tex_px[ntex] = pixels;
    tex_w[ntex] = w;
    tex_h[ntex] = h;
    return ntex++;
}

int sc_tex_w(int t) { return t >= 0 && t < ntex ? tex_w[t] : 1; }
int sc_tex_h(int t) { return t >= 0 && t < ntex ? tex_h[t] : 1; }

static void sprite_core(int t, long x0, long y0, long w, long h, unsigned long pixc, long zs, int ztest, int zwrite)
{
    const unsigned short *px;
    int tw, th, xa, xb, ya, yb, x, y, lim0 = CX0 - XOFF, lim1 = CX1 - XOFF;
    long du, dv, u0, v;
    if (!F || t < 0 || t >= ntex || w <= 0 || h <= 0) return;
    if (YSC != 15) { y0 = y0 / 15 * YSC; h = h / 15 * YSC; }
    px = tex_px[t]; tw = tex_w[t]; th = tex_h[t];
    xa = (int)((x0 + 32767) >> 16); xb = (int)((x0 + w + 32767) >> 16);
    ya = (int)((y0 + 32767) >> 16); yb = (int)((y0 + h + 32767) >> 16);
    if (xb <= lim0 || xa >= lim1 || yb <= 0 || ya >= FH) return;
    du = (long)(((long long)tw << 32) / w);                  /* texels per pixel, 16.16 */
    dv = (long)(((long long)th << 32) / h);
    u0 = (long)((((long long)xa << 16) + 32768 - x0) * du >> 16);
    v = (long)((((long long)ya << 16) + 32768 - y0) * dv >> 16);
    if (xa < lim0) { u0 += du * (lim0 - xa); xa = lim0; }
    if (ya < 0) { v += dv * -ya; ya = 0; }
    if (xb > lim1) xb = lim1;
    if (yb > FH) yb = FH;
    sc_count++;
    for (y = ya; y < yb; y++, v += dv) {
        const unsigned short *trow;
        unsigned short *row = F + (long)y * FW + XOFF;
        long *zr = ZB ? ZB + (long)y * FW + XOFF : 0;
        long u = u0;
        int tv = (int)(v >> 16);
        if (tv >= th) break;
        if (tv < 0) continue;
        trow = px + (long)tv * tw;
        for (x = xa; x < xb; x++, u += du) {
            int tu = (int)(u >> 16);
            unsigned short c;
            if (tu < 0 || tu >= tw) continue;
            c = trow[tu];
            if (!c) continue;
            if (ztest && zr) {
                if (zs >= zr[x]) continue;
                if (zwrite) zr[x] = zs;
            }
            row[x] = pixc ? blend(row[x], c, pixc) : c;
        }
    }
}

void sc_sprite(int t, long x0, long y0, long w, long h, unsigned long pixc)
{
    sprite_core(t, x0, y0, w, h, pixc, 0, 0, 0);
}

void sc_sprite_z(int t, long x0, long y0, long w, long h, long z, unsigned long pixc, int zwrite)
{
    sprite_core(t, x0, y0, w, h, pixc, z >> 4, 1, zwrite);
}
