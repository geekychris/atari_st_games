/*
 * fastcel.c: Atari Falcon port. The cel engine's jobs (flat quads and
 * scaled sprites) for a stock 16 MHz Falcon030, in the way 8/16-bit era 3D
 * games got their speed: no depth buffer (things are drawn far to near,
 * as render.c sorts them, and nearer ones cover farther ones), 32-bit
 * fixed point only (one divs.l per edge, none per pixel), and spans
 * written as longwords straight into the screen's 16-bit pixels.
 *
 * Positions are 16.16 pixels as in softcel.h; colours and textures are
 * the Falcon's RGB565 (0 = transparent in a texture).
 */
#ifndef FASTCEL_H
#define FASTCEL_H

void fc_setup(unsigned short *frame, int w, int h);
void fc_clear(unsigned short c);
void fc_clip(int x0, int x1, int xoff);          /* draw only x0 <= x < x1, shifted by xoff */
void fc_quad(const long *x, const long *y, unsigned short c, unsigned long pixc);
void fc_sprite(const unsigned short *tex, int tw, int th, long x0, long y0, long w, long h,
               unsigned long pixc);              /* top left + size */
unsigned short fc_blend(unsigned short dst, unsigned short src, unsigned long pixc);
#endif
