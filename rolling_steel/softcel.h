/*
 * softcel.c: the 3DO cel engine's two jobs done by the CPU, for the classic
 * 68k builds: a flat-coloured quadrilateral given its four corners, and a
 * texture scaled into a rectangle, both into the 15-bit RGB frame (fb), in
 * the order they come (the 3DO renderers already sort far to near).
 *
 * Positions are 16.16 pixels on the 3DO's 320 x 240 display; ysc / 15
 * stretches them onto a taller frame (planet_chomp: 16, for 256 lines).
 * The pixel-processor modes the games use become per-pixel blends.
 * The same file is in planet_chomp and rolling_steel.
 */
#ifndef SOFTCEL_H
#define SOFTCEL_H

#define PIXC_SHADOW 0x8F008F00UL      /* frame buffer * 4/8 */
#define PIXC_GHOST  0x0F810F81UL      /* (cel + frame buffer) / 2 */
#define PIXC_ADD    0x1F801F80UL      /* cel + frame buffer */

void sc_setup(unsigned short *frame, int w, int h, int ysc);
void sc_clip(int x0, int x1, int xoff);          /* draw only x0 <= x < x1, shifted by xoff */
void sc_quad(long ax, long ay, long bx, long by, long cx, long cy, long dx, long dy,
             unsigned short rgb15, unsigned long pixc);
int  sc_tex(int w, int h, const unsigned short *pixels);   /* 0 = transparent; -1 if full */
int  sc_tex_w(int t);
int  sc_tex_h(int t);
void sc_sprite(int t, long x0, long y0, long w, long h, unsigned long pixc);   /* top left + size */
extern long sc_count;                             /* primitives since the last sc_setup / reset */

/* Optional depth buffer (one long per pixel), for renderers that know each
 * corner's depth: z is distance along the view (bigger = further), in any
 * units up to +-2^20; it's interpolated as a plane in screen space, which
 * is exact for an orthographic camera. zwrite 0: test only (blends). */
void sc_zbuffer(long *zb);                        /* 0: no depth buffer */
void sc_zclear(void);
void sc_quad_z(const long *x, const long *y, const long *z, unsigned short rgb15,
               unsigned long pixc, int zwrite);
void sc_sprite_z(int t, long x0, long y0, long w, long h, long z, unsigned long pixc, int zwrite);
#endif
