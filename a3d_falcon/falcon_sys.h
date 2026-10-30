// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * falcon_sys.h - the Falcon030 machine layer the Amiga game ports here
 * share (the amiga3d engine's main_falcon.c, Terra Strike's): the 256-
 * colour screen of interleaved planes with the games' 256 rows mapped to
 * it, the palette, the IKBD keys as Amiga raw keys, joystick 1, the
 * timers, the games' files, and ptplayer's music and effects on the Paula
 * emulation. Needs supervisor mode (the mains run between Super calls).
 *
 * The screen: eight bitplanes interleaved a word at a time (16 pixels =
 * 8 consecutive words), 320 bytes a row. The games use pens 0-47, so only
 * planes 0-5 are written: a3f_pat holds each pen's three longs. A canvas
 * is a base and a table of row offsets, -1 for the 16 rows of 256 the
 * 240-line VGA screen drops (the next row lands on the same screen row).
 */
#ifndef FALCON_SYS_H
#define FALCON_SYS_H

#include <stdint.h>

#define FSYS_W     320
#define FSYS_ROWS  256                  /* the games' rows */
#define FSYS_MAXBUF 3

/* ---------- the screen ---------- */
int  fsys_screen_init(int nbuf);        /* 2 or 3 buffers; 0: no memory */
void fsys_screen_exit(void);
uint8_t *fsys_buffer(int i);
const int32_t *fsys_rowoff(void);       /* FSYS_ROWS offsets, -1: dropped */
int  fsys_height(void);                 /* the screen's rows: 240 (VGA) or 200 */
void fsys_show(int i);                  /* buffer i from the next VBL */
void fsys_wait_shown(void);             /* until the VBL after the last fsys_show */

/* ---------- pens ---------- */
extern uint32_t a3f_pat[48][3];         /* planes 0-1, 2-3, 4-5 of each pen */
void fsys_rgb12(int first, int n, const uint16_t *rgb);    /* 12-bit RGB to the palette */

/* rows of spans (a3d_rows.S): row k fills xl[k]..xr[k] (clamped to 0..319,
 * none if xl > xr) at base + rowoff[k] (none if < 0) with pat's pen */
void a3f_rows(const int16_t *xl, const int16_t *xr, int n, const int32_t *rowoff, uint8_t *base, const uint32_t *pat);
/* poly_spans in assembly: the same spans; -1 if over 1024 rows */
int  a3f_poly_spans(const int16_t *xs, const int16_t *ys, int n, int16_t *xl, int16_t *xr, int *ymin);
/* a filled rectangle, clipped to 0..319 and rows y0..y1 of the canvas (no clipping in y) */
void fsys_rect(uint8_t *base, const int32_t *rowoff, int x0, int y0, int x1, int y1, int col);

/* ---------- text: the TOS 8 x 8 font ---------- */
extern uint8_t a3f_glyph[256][8];       /* one byte a row, bit 7 left */
void fsys_font_init(void);
/* s with its top row at row top, pen col, only rows y0..y1; a row the
 * screen drops is drawn onto the row it shares (text keeps its strokes) */
void fsys_text(uint8_t *base, const int32_t *rowoff, int x, int top, int col, const char *s, int y0, int y1);

/* ---------- input ---------- */
#define FSYS_JOY_LEFT  1
#define FSYS_JOY_RIGHT 2
#define FSYS_JOY_UP    4
#define FSYS_JOY_DOWN  8
#define FSYS_JOY_FIRE  16
void fsys_input_init(void);             /* takes the IKBD over */
void fsys_input_exit(void);
/* the keys through key_bit (an Amiga raw key code -> the game's input
 * bits): those held now in *held, those pressed since the last call
 * returned (a tap within a frame isn't lost) */
unsigned fsys_keys(unsigned (*key_bit)(unsigned rawkey), unsigned *held);
unsigned fsys_joy(void);                /* joystick 1, FSYS_JOY_* */

/* ---------- time ---------- */
long     fsys_hz200(void);              /* the 200 Hz system timer */
uint32_t fsys_clock(void);              /* 1/38400 s */
int32_t  fsys_us_since(uint32_t t0);

/* the end of a frame: at most 25 a second (as the Amigas), and every 5 s
 * a PERF line through the bridge's log (frames a second, and the share of
 * the time draw_ticks - fsys_clock units spent drawing - add up to) */
void fsys_pace(uint32_t draw_ticks);

/* ---------- files ---------- */
/* an AmigaDOS name ("PROGDIR:stack_tower.mod") as the GEMDOS 8.3 name the
 * port's Makefile copies the file to, in the program's DATA folder
 * ("DATA\\STACK_TO.MOD"); out holds 18 */
void a3f_path(const char *amiga, char *out);
uint8_t *fsys_load(const char *amiga, long *size);    /* malloc'd; NULL: none */

/* ---------- sound ---------- */
int  fsys_sound_init(void);             /* ptplayer on the Paula emulation; 0: no DMA sound */
/* the module at path (fsys_load) on music_channels channels (the effects
 * keep off them); returns its size, 0 if there is none */
long fsys_music(const char *amiga, int music_channels);
int  fsys_sound_on(void);
void fsys_sound_exit(void);

#endif
