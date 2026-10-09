/*
 * glcels.c: what the 3DO version's cels.c did, in OpenGL (Mesa's software
 * rasteriser through OSMesa). render.c still projects everything itself in
 * integer maths; it hands over screen positions (16.16 px on the 320 x 240
 * screen) plus each corner's depth along the view (Q12 units), and the depth
 * buffer sorts the faces per pixel.
 *
 * The 3DO's pixel-processor modes become blending: a PIXC word with the top
 * bit set darkens to k/8 of the frame (k = bits 10-12, plus one),
 * PIXC_GHOST is half and half, PIXC_ADD adds.
 */
#ifndef GLCELS_H
#define GLCELS_H

#define PIXC_SHADOW 0x8F008F00UL      /* frame buffer * 4/8 */
#define PIXC_GHOST  0x0F810F81UL      /* (cel + frame buffer) / 2 */
#define PIXC_ADD    0x1F801F80UL      /* cel + frame buffer: flashes, sparks */

#define GLC_NOZ     0x7FFFFFFFL       /* z for overlays: no depth test */
#define SCREEN_W    320
#define SCREEN_H    240

int   glc_open(int scale);            /* buffer of SCREEN_W x SCREEN_H times scale */
void  glc_close(void);
unsigned char *glc_pixels(void);      /* A R G B, rows top to bottom */
int   glc_scale(void);

void  glc_begin(unsigned short bg_rgb15);   /* clear colour and depth */
void  glc_view(int x0, int w);        /* draw into columns x0..x0+w-1 (split screen) */
/* opaque flat quads are batched; glc_flush() draws the batch */
void  glc_quad(const long *x, const long *y, const long *z, unsigned short rgb15, unsigned long pixc);
void  glc_flush(void);
int   glc_tex(int w, int h, const unsigned short *rgb15);   /* 0 = transparent */
void  glc_sprite(int t, long x, long y, long hx, long hy, long z, unsigned long pixc);
void  glc_finish(void);
int   glc_count(void);                /* primitives this frame */
#endif
