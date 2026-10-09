/*
 * glcels.h on softcel.c, for the classic 68k build: the same render.c as
 * the AmigaOS 4 version, whose faces carry each corner's depth, so a depth
 * buffer settles what hides what per pixel. (The 3DO's painter's sort put
 * a side face over the deck now and then where track pieces meet.)
 *
 * Everything is drawn as it arrives; render.c already sends the solid
 * faces first and the blended ones (shadows, the ghost, sparks) after.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include "glcels.h"
#include "softcel.h"
#include "amiga68k.h"

static long *zbuf;
static int count;

#ifdef __MINT__
/* Atari Falcon port: half resolution. The 3D goes into lo (160 x 120)
 * with positions halved on the way to softcel.c, and glc_finish doubles
 * it into fb; the HUD's text is drawn into fb after that, at full
 * resolution. A quarter of the pixels to fill, for a stock Falcon030.
 * F / F10 switch (sys_halfres, ../falcon_3do/sys3do.c). */
#define HALF 1
static UWORD *lo;
static int half;                        /* what this frame is drawn at */
#define H(v) ((v) >> half)
#else
#define H(v) (v)
#endif

int glc_open(int scale)
{
    (void)scale;
    zbuf = (long *)AllocVec((long)SCREEN_W * SCREEN_H * 4, MEMF_ANY);
    if (!zbuf) return 0;
#ifdef HALF
    lo = (UWORD *)AllocVec((long)(SCREEN_W / 2) * (SCREEN_H / 2) * 2, MEMF_ANY);
    if (lo) sys_halfres = sys_cpu() < 40;   /* a plain 030: start at half */
#endif
    sc_setup(fb, SCREEN_W, SCREEN_H, 15);
    sc_zbuffer(zbuf);
    return 1;
}

void glc_close(void)
{
    sc_zbuffer(0);
    if (zbuf) FreeVec(zbuf);
    zbuf = 0;
#ifdef HALF
    if (lo) FreeVec(lo);
    lo = 0;
#endif
}

unsigned char *glc_pixels(void) { return (unsigned char *)fb; }
int glc_scale(void) { return 1; }
int glc_count(void) { return count; }

void glc_begin(unsigned short bg)
{
    UWORD *f = fb, *e = fb + (long)SCREEN_W * SCREEN_H;
#ifdef HALF
    half = sys_halfres == 1;
    if (half) {
        f = lo;
        e = lo + (long)(SCREEN_W / 2) * (SCREEN_H / 2);
        sc_setup(lo, SCREEN_W / 2, SCREEN_H / 2, 15);
    } else
        sc_setup(fb, SCREEN_W, SCREEN_H, 15);
#endif
    while (f < e) *f++ = bg;
    sc_zclear();
    sc_clip(0, H(SCREEN_W), 0);
    count = 0;
}

void glc_view(int x0, int w) { sc_clip(H(x0), H(x0 + w), H(x0)); }

void glc_quad(const long *x, const long *y, const long *z, unsigned short rgb15, unsigned long pixc)
{
    count++;
#ifdef HALF
    if (half) {
        long hx[4], hy[4];
        int i;
        for (i = 0; i < 4; i++) { hx[i] = x[i] >> 1; hy[i] = y[i] >> 1; }
        x = hx; y = hy;
        if (z[0] == GLC_NOZ) sc_quad(x[0], y[0], x[1], y[1], x[2], y[2], x[3], y[3], rgb15, pixc);
        else sc_quad_z(x, y, z, rgb15, pixc, pixc == 0);
        return;
    }
#endif
    if (z[0] == GLC_NOZ) sc_quad(x[0], y[0], x[1], y[1], x[2], y[2], x[3], y[3], rgb15, pixc);
    else sc_quad_z(x, y, z, rgb15, pixc, pixc == 0);
}

void glc_flush(void) { }

/* render.c frees the image after this (GL copies textures): keep a copy */
int glc_tex(int w, int h, const unsigned short *rgb15)
{
    UWORD *copy = (UWORD *)AllocVec((long)w * h * 2, MEMF_ANY);
    long i;
    if (!copy) return -1;
    for (i = 0; i < (long)w * h; i++) copy[i] = rgb15[i];
    return sc_tex(w, h, copy);
}

void glc_sprite(int t, long x, long y, long hx, long hy, long z, unsigned long pixc)
{
    count++;
    x = H(x); y = H(y); hx = H(hx); hy = H(hy);
    if (z == GLC_NOZ) sc_sprite(t, x - hx, y - hy, hx * 2, hy * 2, pixc);
    else sc_sprite_z(t, x - hx, y - hy, hx * 2, hy * 2, z, pixc, pixc == 0);
}

void glc_finish(void)
{
#ifdef HALF
    /* each lo pixel to 2 x 2 in fb */
    if (half) {
        const UWORD *s = lo;
        ULONG *d = (ULONG *)fb;
        int y, x;
        for (y = 0; y < SCREEN_H / 2; y++, d += SCREEN_W) {
            for (x = 0; x < SCREEN_W / 2; x++) {
                ULONG v = *s++;
                v |= v << 16;
                d[x] = v;
                d[x + SCREEN_W / 2] = v;
            }
        }
    }
#endif
}
