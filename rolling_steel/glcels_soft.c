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
/* Atari Falcon port. fb is the screen, in the Falcon's RGB565
 * (sys_rgb565; softcel.c is built with SOFTCEL_RGB565), so colours and
 * textures are converted on the way in. F / F10 cycle four modes:
 *
 *   depth buffer   softcel.c, as on the Amiga
 *   fast           fastcel.c: no depth buffer, render.c's far-to-near
 *                  order decides (one pass, see render.c), 32-bit maths,
 *                  longword spans, the solid rows in assembly; the
 *                  default (the order glitches where rail pieces meet,
 *                  as on the 3DO, but it is 2-3 times faster)
 *   ... half res   either, drawing the 3D into lo (160 x 120) with
 *                  positions halved on the way, doubled into fb by
 *                  glc_finish; the HUD's text goes on after, at full
 *                  resolution
 */
#include "fastcel.h"
#define FALCON 1
enum { M_DEPTH, M_DEPTH_HALF, M_FAST, M_FAST_HALF, M_COUNT };
static const char *const mode_names[M_COUNT] = {
    "depth buffer", "depth buffer, half resolution", "fast", "fast, half resolution"
};
static UWORD *lo;
static int half, fast;                  /* what this frame is drawn with */
#define H(v) ((v) >> half)
#define C565(c) ((UWORD)((((c) << 1) & 0xFFC0) | ((c) & 0x1F)))
#define MAXTEXF 40
static const UWORD *ftex[MAXTEXF];
static int ftw[MAXTEXF], fth[MAXTEXF];

int glc_painter(void) { return fast; }
#else
#define H(v) (v)
#define C565(c) (c)
#endif

int glc_open(int scale)
{
    (void)scale;
    zbuf = (long *)AllocVec((long)SCREEN_W * SCREEN_H * 4, MEMF_ANY);
    if (!zbuf) return 0;
#ifdef FALCON
    lo = (UWORD *)AllocVec((long)(SCREEN_W / 2) * (SCREEN_H / 2) * 2, MEMF_ANY);
    sys_mode_names = mode_names;
    sys_modes = lo ? M_COUNT : M_FAST + 1;
    sys_mode = M_FAST;
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
#ifdef FALCON
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
#ifdef FALCON
    /* fb moves at every sys_present (it is the back screen) */
    half = sys_mode == M_DEPTH_HALF || sys_mode == M_FAST_HALF;
    fast = sys_mode >= M_FAST;
    if (half) {
        f = lo;
        e = lo + (long)(SCREEN_W / 2) * (SCREEN_H / 2);
    }
    if (fast) {
        fc_setup(f, H(SCREEN_W), H(SCREEN_H));
        fc_clear(C565(bg));
        fc_clip(0, H(SCREEN_W), 0);
        count = 0;
        return;
    }
    sc_setup(f, H(SCREEN_W), H(SCREEN_H), 15);
    bg = C565(bg);
#endif
    while (f < e) *f++ = bg;
    sc_zclear();
    sc_clip(0, H(SCREEN_W), 0);
    count = 0;
}

void glc_view(int x0, int w)
{
#ifdef FALCON
    if (fast) { fc_clip(H(x0), H(x0 + w), H(x0)); return; }
#endif
    sc_clip(H(x0), H(x0 + w), H(x0));
}

void glc_quad(const long *x, const long *y, const long *z, unsigned short rgb15, unsigned long pixc)
{
    count++;
#ifdef FALCON
    if (half) {
        long hx[4], hy[4];
        int i;
        for (i = 0; i < 4; i++) { hx[i] = x[i] >> 1; hy[i] = y[i] >> 1; }
        x = hx; y = hy;
        if (fast) fc_quad(x, y, C565(rgb15), pixc);
        else if (z[0] == GLC_NOZ) sc_quad(x[0], y[0], x[1], y[1], x[2], y[2], x[3], y[3], C565(rgb15), pixc);
        else sc_quad_z(x, y, z, C565(rgb15), pixc, pixc == 0);
        return;
    }
    if (fast) { fc_quad(x, y, C565(rgb15), pixc); return; }
#endif
    if (z[0] == GLC_NOZ) sc_quad(x[0], y[0], x[1], y[1], x[2], y[2], x[3], y[3], C565(rgb15), pixc);
    else sc_quad_z(x, y, z, C565(rgb15), pixc, pixc == 0);
}

void glc_flush(void) { }

/* render.c frees the image after this (GL copies textures): keep a copy */
int glc_tex(int w, int h, const unsigned short *rgb15)
{
    UWORD *copy = (UWORD *)AllocVec((long)w * h * 2, MEMF_ANY);
    long i;
    int t;
    if (!copy) return -1;
    for (i = 0; i < (long)w * h; i++) copy[i] = C565(rgb15[i]);
    t = sc_tex(w, h, copy);
#ifdef FALCON
    if (t >= 0 && t < MAXTEXF) { ftex[t] = copy; ftw[t] = w; fth[t] = h; }
#endif
    return t;
}

void glc_sprite(int t, long x, long y, long hx, long hy, long z, unsigned long pixc)
{
    count++;
    x = H(x); y = H(y); hx = H(hx); hy = H(hy);
#ifdef FALCON
    if (fast) {
        if (t >= 0 && t < MAXTEXF)
            fc_sprite(ftex[t], ftw[t], fth[t], x - hx, y - hy, hx * 2, hy * 2, pixc);
        return;
    }
#endif
    if (z == GLC_NOZ) sc_sprite(t, x - hx, y - hy, hx * 2, hy * 2, pixc);
    else sc_sprite_z(t, x - hx, y - hy, hx * 2, hy * 2, z, pixc, pixc == 0);
}

void glc_finish(void)
{
#ifdef FALCON
    /* each lo pixel to 2 x 2 in fb */
    if (half) {
        const ULONG *s = (const ULONG *)lo;
        ULONG *d = (ULONG *)fb, *d2;
        int y, x;
        for (y = 0; y < SCREEN_H / 2; y++, d += SCREEN_W / 2) {
            d2 = d + SCREEN_W / 2;
            for (x = 0; x < SCREEN_W / 4; x++) {     /* two pixels a time */
                ULONG v = *s++, a = (v & 0xFFFF0000UL) | (v >> 16), b = (v << 16) | (v & 0xFFFF);
                *d++ = a; *d++ = b;
                *d2++ = a; *d2++ = b;
            }
        }
    }
#endif
}
