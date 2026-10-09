/*
 * The 3DO version's cel calls (pc.h: pc_quad, pc_sprite, pc_tex_create)
 * on softcel.c, for the classic 68k build: every cel is drawn into the
 * frame as it arrives, in render_cel.c's far-to-near order.
 */
#include "pc.h"
#include "softcel.h"

int  pc_cels_init(void) { return 1; }
void pc_cels_begin(void) { sc_count = 0; }
int  pc_cels_count(void) { return (int)sc_count; }
void *pc_cels_list(void) { return 0; }

void pc_quad(long ax, long ay, long bx, long by, long cx, long cy, long dx, long dy,
             unsigned short rgb15)
{
    sc_quad(ax, ay, bx, by, cx, cy, dx, dy, rgb15, 0);
}

int pc_tex_create(int w, int h, unsigned short *pixels) { return sc_tex(w, h, pixels); }

/* centred on (x, y); half = half the drawn width; the height keeps the
 * texture's shape (as cels.c on the 3DO) */
void pc_sprite(int tex, long x, long y, long half)
{
    long scale, h;
    if (tex < 0 || half <= 0) return;
    scale = (half * 2) / sc_tex_w(tex);                  /* 16.16 px per texel */
    h = scale * sc_tex_h(tex);
    sc_sprite(tex, x - half, y - h / 2, half * 2, h, 0);
}
