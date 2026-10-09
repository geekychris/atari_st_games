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

int glc_open(int scale)
{
    (void)scale;
    zbuf = (long *)AllocVec((long)SCREEN_W * SCREEN_H * 4, MEMF_ANY);
    if (!zbuf) return 0;
    sc_setup(fb, SCREEN_W, SCREEN_H, 15);
    sc_zbuffer(zbuf);
    return 1;
}

void glc_close(void)
{
    sc_zbuffer(0);
    if (zbuf) FreeVec(zbuf);
    zbuf = 0;
}

unsigned char *glc_pixels(void) { return (unsigned char *)fb; }
int glc_scale(void) { return 1; }
int glc_count(void) { return count; }

void glc_begin(unsigned short bg)
{
    UWORD *f = fb, *e = fb + (long)SCREEN_W * SCREEN_H;
    while (f < e) *f++ = bg;
    sc_zclear();
    sc_clip(0, SCREEN_W, 0);
    count = 0;
}

void glc_view(int x0, int w) { sc_clip(x0, x0 + w, x0); }

void glc_quad(const long *x, const long *y, const long *z, unsigned short rgb15, unsigned long pixc)
{
    count++;
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
    if (z == GLC_NOZ) sc_sprite(t, x - hx, y - hy, hx * 2, hy * 2, pixc);
    else sc_sprite_z(t, x - hx, y - hy, hx * 2, hy * 2, z, pixc, pixc == 0);
}

void glc_finish(void) { }
