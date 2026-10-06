// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * Lunar Rider - Drawing functions
 * Parallax backgrounds, terrain, buggy, enemies, HUD, title screen
 * Includes 5x7 bitmap font for text rendering
 */
#include <proto/graphics.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <string.h>
#include "game.h"
#include "draw.h"
#ifdef __MINT__
#include <string.h>
#include <exec/memory.h>
#include "st_blit.h"
#endif

/* 5x7 bitmap font (A-Z, 0-9, space, punctuation) */
static const UBYTE font_5x7[96][7] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* space */
    {0x04,0x04,0x04,0x04,0x04,0x00,0x04}, /* ! */
    {0x0A,0x0A,0x00,0x00,0x00,0x00,0x00}, /* " */
    {0x0A,0x1F,0x0A,0x0A,0x1F,0x0A,0x00}, /* # */
    {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04}, /* $ */
    {0x18,0x19,0x02,0x04,0x08,0x13,0x03}, /* % */
    {0x08,0x14,0x14,0x08,0x15,0x12,0x0D}, /* & */
    {0x04,0x04,0x00,0x00,0x00,0x00,0x00}, /* ' */
    {0x02,0x04,0x08,0x08,0x08,0x04,0x02}, /* ( */
    {0x08,0x04,0x02,0x02,0x02,0x04,0x08}, /* ) */
    {0x00,0x04,0x15,0x0E,0x15,0x04,0x00}, /* * */
    {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}, /* + */
    {0x00,0x00,0x00,0x00,0x00,0x04,0x08}, /* , */
    {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, /* - */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x04}, /* . */
    {0x01,0x01,0x02,0x04,0x08,0x10,0x10}, /* / */
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, /* 0 */
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, /* 1 */
    {0x0E,0x11,0x01,0x06,0x08,0x10,0x1F}, /* 2 */
    {0x0E,0x11,0x01,0x06,0x01,0x11,0x0E}, /* 3 */
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, /* 4 */
    {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, /* 5 */
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, /* 6 */
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, /* 7 */
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, /* 8 */
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, /* 9 */
    {0x00,0x00,0x04,0x00,0x00,0x04,0x00}, /* : */
    {0x00,0x00,0x04,0x00,0x00,0x04,0x08}, /* ; */
    {0x02,0x04,0x08,0x10,0x08,0x04,0x02}, /* < */
    {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}, /* = */
    {0x08,0x04,0x02,0x01,0x02,0x04,0x08}, /* > */
    {0x0E,0x11,0x01,0x06,0x04,0x00,0x04}, /* ? */
    {0x0E,0x11,0x17,0x15,0x17,0x10,0x0E}, /* @ */
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, /* A */
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, /* B */
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, /* C */
    {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}, /* D */
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, /* E */
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, /* F */
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0E}, /* G */
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, /* H */
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, /* I */
    {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, /* J */
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, /* K */
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, /* L */
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, /* M */
    {0x11,0x19,0x15,0x13,0x11,0x11,0x11}, /* N */
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, /* O */
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, /* P */
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, /* Q */
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, /* R */
    {0x0E,0x11,0x10,0x0E,0x01,0x11,0x0E}, /* S */
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, /* T */
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, /* U */
    {0x11,0x11,0x11,0x11,0x0A,0x0A,0x04}, /* V */
    {0x11,0x11,0x11,0x15,0x15,0x1B,0x11}, /* W */
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, /* X */
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, /* Y */
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, /* Z */
    {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}, /* [ */
    {0x10,0x10,0x08,0x04,0x02,0x01,0x01}, /* \ */
    {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}, /* ] */
    {0x04,0x0A,0x11,0x00,0x00,0x00,0x00}, /* ^ */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x1F}, /* _ */
    {0x08,0x04,0x00,0x00,0x00,0x00,0x00}, /* ` */
    {0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F}, /* a */
    {0x10,0x10,0x1E,0x11,0x11,0x11,0x1E}, /* b */
    {0x00,0x00,0x0E,0x10,0x10,0x10,0x0E}, /* c */
    {0x01,0x01,0x0F,0x11,0x11,0x11,0x0F}, /* d */
    {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E}, /* e */
    {0x06,0x08,0x1C,0x08,0x08,0x08,0x08}, /* f */
    {0x00,0x00,0x0F,0x11,0x0F,0x01,0x0E}, /* g */
    {0x10,0x10,0x1E,0x11,0x11,0x11,0x11}, /* h */
    {0x04,0x00,0x0C,0x04,0x04,0x04,0x0E}, /* i */
    {0x02,0x00,0x06,0x02,0x02,0x12,0x0C}, /* j */
    {0x10,0x10,0x12,0x14,0x18,0x14,0x12}, /* k */
    {0x0C,0x04,0x04,0x04,0x04,0x04,0x0E}, /* l */
    {0x00,0x00,0x1A,0x15,0x15,0x15,0x15}, /* m */
    {0x00,0x00,0x1E,0x11,0x11,0x11,0x11}, /* n */
    {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E}, /* o */
    {0x00,0x00,0x1E,0x11,0x1E,0x10,0x10}, /* p */
    {0x00,0x00,0x0F,0x11,0x0F,0x01,0x01}, /* q */
    {0x00,0x00,0x16,0x19,0x10,0x10,0x10}, /* r */
    {0x00,0x00,0x0F,0x10,0x0E,0x01,0x1E}, /* s */
    {0x08,0x08,0x1C,0x08,0x08,0x09,0x06}, /* t */
    {0x00,0x00,0x11,0x11,0x11,0x11,0x0F}, /* u */
    {0x00,0x00,0x11,0x11,0x11,0x0A,0x04}, /* v */
    {0x00,0x00,0x11,0x11,0x15,0x15,0x0A}, /* w */
    {0x00,0x00,0x11,0x0A,0x04,0x0A,0x11}, /* x */
    {0x00,0x00,0x11,0x11,0x0F,0x01,0x0E}, /* y */
    {0x00,0x00,0x1F,0x02,0x04,0x08,0x1F}, /* z */
    {0x02,0x04,0x04,0x08,0x04,0x04,0x02}, /* { */
    {0x04,0x04,0x04,0x04,0x04,0x04,0x04}, /* | */
    {0x08,0x04,0x04,0x02,0x04,0x04,0x08}, /* } */
    {0x00,0x00,0x08,0x15,0x02,0x00,0x00}, /* ~ */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* DEL */
};

static void draw_char(struct RastPort *rp, WORD x, WORD y, char c, WORD scale)
{
    WORD idx, row, col;
    UBYTE bits;

    if (c < 32 || c > 127) return;
    idx = c - 32;

    for (row = 0; row < 7; row++) {
        bits = font_5x7[idx][row];
        for (col = 0; col < 5; col++) {
            if (bits & (0x10 >> col)) {
                if (scale == 1) {
                    /* Use RectFill even for single pixels - never WritePixel in loops */
                    RectFill(rp, x + col, y + row, x + col, y + row);
                } else {
                    RectFill(rp, x + col * scale, y + row * scale,
                             x + col * scale + scale - 1,
                             y + row * scale + scale - 1);
                }
            }
        }
    }
}

static void draw_string_raw(struct RastPort *rp, WORD x, WORD y, const char *s, WORD scale);

#ifdef __MINT__
/* ST port: on the ST layer's HUD layer (title, score line) a string drawn
 * from rectangles becomes one cached operation, re-rendered only when it
 * changes; elsewhere it is drawn directly as before */
static void string_op(struct RastPort *rp, const void *ctx, WORD x, WORD y, WORD arg)
{
    SetAPen(rp, arg >> 8);
    draw_string_raw(rp, x, y, (const char *)ctx, arg & 0xff);
}

void draw_string_at(struct RastPort *rp, WORD x, WORD y, const char *s, WORD scale)
{
    WORD len = 0;
    while (s[len]) len++;
    gfx_cached(rp, string_op, s, len + 1, NULL, x, y, (WORD)((rp->apen << 8) | scale),
               (WORD)(len * 6 * scale), (WORD)(7 * scale));
}
#else
void draw_string_at(struct RastPort *rp, WORD x, WORD y, const char *s, WORD scale)
{
    draw_string_raw(rp, x, y, s, scale);
}
#endif

static void draw_string_raw(struct RastPort *rp, WORD x, WORD y, const char *s, WORD scale)
{
    while (*s) {
        draw_char(rp, x, y, *s, scale);
        x += (5 + 1) * scale;
        s++;
    }
}

WORD string_pixel_width(const char *s, WORD scale)
{
    WORD len = 0;
    while (*s) { len++; s++; }
    if (len == 0) return 0;
    return len * 6 * scale - scale;
}

/* Format a long integer to string (avoids sprintf %d issues on AmigaOS) */
static void long_to_str(char *buf, long val)
{
    char *p = buf;
    int started = 0;
    long dv;

    if (val < 0) {
        *p++ = '-';
        val = -val;
    }

    for (dv = 1000000000L; dv > 0; dv /= 10) {
        int digit = (int)(val / dv);
        val -= (long)digit * dv;
        if (digit || started || dv == 1) {
            *p++ = (char)('0' + digit);
            started = 1;
        }
    }
    *p = '\0';
}

/* ---- Mountain drawing using simple polygons ---- */

/* Approximate sine: input 0-255 maps to 0-255-0 (half wave) */
static WORD approx_sin(WORD phase)
{
    /* Triangle approximation: 0-64=rise, 64-192=fall, 192-256=rise */
    phase = phase & 255;
    if (phase < 64) return (WORD)(phase * 4);
    if (phase < 192) return (WORD)(255 - (phase - 64) * 2);
    return (WORD)(-255 + (phase - 192) * 4);
}

#ifdef __MINT__
/* ST port: the same 30 fixed stars, positions computed once (two 32 bit
 * modulos per star per frame are library calls on the 68000) */
static void draw_stars(struct RastPort *rp)
{
    static WORD sxs[30], sys_[30], ready;
    WORD i;
    if (!ready) {
        ULONG seed = 42;
        for (i = 0; i < 30; i++) {
            seed = seed * 1103515245UL + 12345UL;
            sxs[i] = (WORD)((seed >> 16) % SCREEN_W);
            sys_[i] = (WORD)((seed >> 8) % 80);
        }
        ready = 1;
    }
    SetAPen(rp, COL_WHITE);
    for (i = 0; i < 30; i++)
        RectFill(rp, sxs[i], sys_[i], sxs[i], sys_[i]);
}
#else
static void draw_stars(struct RastPort *rp)
{
    /* Fixed star positions - very fast, just a few RectFills */
    ULONG seed = 42;
    WORD i;

    SetAPen(rp, COL_WHITE);
    for (i = 0; i < 30; i++) {
        WORD sx, sy;
        seed = seed * 1103515245UL + 12345UL;
        sx = (WORD)((seed >> 16) % SCREEN_W);
        sy = (WORD)((seed >> 8) % 80);  /* stars only in top portion */
        RectFill(rp, sx, sy, sx, sy);
    }
}
#endif

#ifdef __MINT__
/* ST port: the same silhouettes (same formulas, 2 pixel columns), filled
 * 16 columns at a time by gfx_vspans instead of 160 RectFills each */
static void draw_far_mountains(struct RastPort *rp, LONG scroll_x)
{
    WORD x, top[SCREEN_W], bot[SCREEN_W];
    WORD offset = (WORD)((scroll_x / 400) % 640);  /* 0.25x parallax speed */

    for (x = 0; x < SCREEN_W; x += 2) {
        WORD phase = (WORD)((x + offset) * 3 / 2);
        WORD peak = approx_sin(phase);
        WORD h = 120 + 20 - (peak * 40 / 255);
        if (h < 90) h = 90;
        if (h > 140) h = 140;
        top[x] = top[x + 1] = h;
        bot[x] = bot[x + 1] = 160;
    }
    gfx_vspans(rp, 0, SCREEN_W, top, bot, COL_DKPURPLE);
}

static void draw_near_mountains(struct RastPort *rp, LONG scroll_x)
{
    WORD x, top[SCREEN_W], bot[SCREEN_W];
    WORD offset = (WORD)((scroll_x / 200) % 640);  /* 0.5x parallax speed */

    for (x = 0; x < SCREEN_W; x += 2) {
        WORD phase = (WORD)((x + offset) * 5 / 2);
        WORD peak = approx_sin(phase);
        WORD h = 100 + 30 - (peak * 60 / 255);
        if (h < 80) h = 80;
        if (h > 160) h = 160;
        top[x] = top[x + 1] = h;
        bot[x] = bot[x + 1] = 170;
    }
    gfx_vspans(rp, 0, SCREEN_W, top, bot, COL_PURPLE);
}
#else
static void draw_far_mountains(struct RastPort *rp, LONG scroll_x)
{
    WORD x;
    WORD offset = (WORD)((scroll_x / 400) % 640);  /* 0.25x parallax speed */

    SetAPen(rp, COL_DKPURPLE);
    for (x = 0; x < SCREEN_W; x += 2) {
        WORD phase = (WORD)((x + offset) * 3 / 2);
        WORD peak = approx_sin(phase);
        WORD h = 120 + 20 - (peak * 40 / 255);
        if (h < 90) h = 90;
        if (h > 140) h = 140;
        RectFill(rp, x, h, x + 1, 160);
    }
}

static void draw_near_mountains(struct RastPort *rp, LONG scroll_x)
{
    WORD x;
    WORD offset = (WORD)((scroll_x / 200) % 640);  /* 0.5x parallax speed */

    SetAPen(rp, COL_PURPLE);
    for (x = 0; x < SCREEN_W; x += 2) {
        WORD phase = (WORD)((x + offset) * 5 / 2);
        WORD peak = approx_sin(phase);
        WORD h = 100 + 30 - (peak * 60 / 255);
        if (h < 80) h = 80;
        if (h > 160) h = 160;
        RectFill(rp, x, h, x + 1, 170);
    }
}

#endif

/* ---- Terrain drawing ---- */

#ifdef __MINT__
/* ST port: the same per-column terrain (surface, body, crater gaps, rocks,
 * mines), gathered into one span per colour and column and filled by
 * gfx_vspans, instead of 2-3 RectFills per column */
static void draw_terrain(struct RastPort *rp, GameState *gs)
{
    static WORD t[5][SCREEN_W], b[5][SCREEN_W];
    enum { SURF, BODY, GAP, ROCK, MINE };
    static const UBYTE col[5] = { COL_BROWN, COL_DKBROWN, COL_BLACK, COL_DKGRAY, COL_RED };
    WORD x, k;
    LONG scroll_px = gs->scroll_x / 100;
    LONG raw_tidx = scroll_px % TERRAIN_LEN;
    WORD tidx;

    if (raw_tidx < 0) raw_tidx += TERRAIN_LEN;
    tidx = (WORD)raw_tidx;
    for (x = 0; x < SCREEN_W; x++) {
        WORD th = gs->terrain_h[tidx];
        WORD ttype = gs->terrain[tidx];
        for (k = 0; k < 5; k++) { t[k][x] = 1; b[k][x] = 0; }
        switch (ttype) {
            case TERRAIN_CRATER_SM:
            case TERRAIN_CRATER_LG:
                t[GAP][x] = GROUND_Y;       b[GAP][x] = GROUND_Y + 15;
                t[BODY][x] = GROUND_Y + 16; b[BODY][x] = SCREEN_H - 25;
                break;
            default:
                t[SURF][x] = th;            b[SURF][x] = th + 2;
                t[BODY][x] = th + 3;        b[BODY][x] = SCREEN_H - 25;
                if (ttype == TERRAIN_ROCK) {
                    t[ROCK][x] = th - 10;   b[ROCK][x] = th - 1;
                } else if (ttype == TERRAIN_MINE && (x & 3) < 2) {
                    t[MINE][x] = th - 4;    b[MINE][x] = th - 1;
                }
                break;
        }
        if (++tidx >= TERRAIN_LEN) tidx = 0;
    }
    for (k = 0; k < 5; k++)
        gfx_vspans(rp, 0, SCREEN_W, t[k], b[k], col[k]);
}
#else
static void draw_terrain(struct RastPort *rp, GameState *gs)
{
    WORD x;
    LONG scroll_px = gs->scroll_x / 100;

    /* Draw ground surface */
    for (x = 0; x < SCREEN_W; x++) {
        LONG raw_tidx = (scroll_px + x) % TERRAIN_LEN;
        WORD tidx;
        WORD th;
        WORD ttype;

        if (raw_tidx < 0) raw_tidx += TERRAIN_LEN;
        tidx = (WORD)raw_tidx;
        th = gs->terrain_h[tidx];
        ttype = gs->terrain[tidx];

        switch (ttype) {
            case TERRAIN_FLAT:
            case TERRAIN_HILL:
                /* Ground surface */
                SetAPen(rp, COL_BROWN);
                RectFill(rp, x, th, x, th + 2);
                SetAPen(rp, COL_DKBROWN);
                RectFill(rp, x, th + 3, x, SCREEN_H - 25);
                break;

            case TERRAIN_CRATER_SM:
            case TERRAIN_CRATER_LG:
                /* Gap in ground - just darker below */
                SetAPen(rp, COL_BLACK);
                RectFill(rp, x, GROUND_Y, x, GROUND_Y + 15);
                SetAPen(rp, COL_DKBROWN);
                RectFill(rp, x, GROUND_Y + 16, x, SCREEN_H - 25);
                break;

            case TERRAIN_ROCK:
                /* Rock on ground */
                SetAPen(rp, COL_BROWN);
                RectFill(rp, x, th, x, th + 2);
                SetAPen(rp, COL_DKBROWN);
                RectFill(rp, x, th + 3, x, SCREEN_H - 25);
                /* Rock body */
                SetAPen(rp, COL_DKGRAY);
                RectFill(rp, x, th - 10, x, th - 1);
                break;

            case TERRAIN_MINE:
                /* Ground + mine on top */
                SetAPen(rp, COL_BROWN);
                RectFill(rp, x, th, x, th + 2);
                SetAPen(rp, COL_DKBROWN);
                RectFill(rp, x, th + 3, x, SCREEN_H - 25);
                /* Mine dot */
                if ((x & 3) < 2) {
                    SetAPen(rp, COL_RED);
                    RectFill(rp, x, th - 4, x, th - 1);
                }
                break;
        }
    }
}

#endif

/* ---- Entity drawing ---- */

static void draw_buggy(struct RastPort *rp, GameState *gs)
{
    Buggy *b = &gs->buggy;
    WORD bx, by;

    if (!b->alive) return;

    bx = b->x;
    by = b->y;

    /* Buggy body */
    SetAPen(rp, COL_YELLOW);
    RectFill(rp, bx + 2, by + 1, bx + 21, by + 5);

    /* Roof/top */
    SetAPen(rp, COL_YELLOW);
    RectFill(rp, bx + 6, by - 2, bx + 17, by);

    /* Windshield */
    SetAPen(rp, COL_CYAN);
    RectFill(rp, bx + 16, by - 1, bx + 18, by);

    /* Gun barrel (forward) */
    SetAPen(rp, COL_ORANGE);
    RectFill(rp, bx + 22, by + 2, bx + 24, by + 3);

    /* Gun barrel (upward) */
    RectFill(rp, bx + 11, by - 5, bx + 12, by - 2);

    /* Wheels */
    SetAPen(rp, COL_GRAY);
    if (b->wheel_frame) {
        /* Frame 1 */
        RectFill(rp, bx + 2, by + 6, bx + 6, by + 10);
        RectFill(rp, bx + 17, by + 6, bx + 21, by + 10);
        /* Spoke detail */
        SetAPen(rp, COL_DKGRAY);
        RectFill(rp, bx + 4, by + 7, bx + 4, by + 9);
        RectFill(rp, bx + 19, by + 7, bx + 19, by + 9);
    } else {
        /* Frame 0 */
        RectFill(rp, bx + 2, by + 6, bx + 6, by + 10);
        RectFill(rp, bx + 17, by + 6, bx + 21, by + 10);
        /* Spoke detail */
        SetAPen(rp, COL_DKGRAY);
        RectFill(rp, bx + 3, by + 8, bx + 5, by + 8);
        RectFill(rp, bx + 18, by + 8, bx + 20, by + 8);
    }

    /* Undercarriage */
    SetAPen(rp, COL_DKGRAY);
    RectFill(rp, bx + 7, by + 6, bx + 16, by + 7);
}

static void draw_bullets_all(struct RastPort *rp, GameState *gs)
{
    WORD i;

    SetAPen(rp, COL_ORANGE);

    /* Forward bullets */
    for (i = 0; i < MAX_FWD_BULLETS; i++) {
        Bullet *b = &gs->fwd_bullets[i];
        if (!b->active) continue;
        RectFill(rp, b->x, b->y, b->x + 3, b->y + 1);
    }

    /* Upward bullets */
    for (i = 0; i < MAX_UP_BULLETS; i++) {
        Bullet *b = &gs->up_bullets[i];
        if (!b->active) continue;
        RectFill(rp, b->x, b->y, b->x + 1, b->y + 3);
    }
}

static void draw_enemies_all(struct RastPort *rp, GameState *gs)
{
    WORD i;

    for (i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &gs->enemies[i];
        if (!e->active) continue;

        switch (e->type) {
            case ENEMY_UFO_SM:
                /* Small UFO - pulsing between cyan and white */
                SetAPen(rp, (e->flash & 8) ? COL_WHITE : COL_CYAN);
                RectFill(rp, e->x + 2, e->y + 2, e->x + 9, e->y + 5);
                /* Dome */
                SetAPen(rp, COL_CYAN);
                RectFill(rp, e->x + 4, e->y, e->x + 7, e->y + 1);
                /* Bottom */
                SetAPen(rp, COL_DKGRAY);
                RectFill(rp, e->x, e->y + 4, e->x + 11, e->y + 6);
                break;

            case ENEMY_UFO_LG:
                /* Large UFO */
                SetAPen(rp, (e->flash & 8) ? COL_WHITE : COL_CYAN);
                RectFill(rp, e->x + 3, e->y + 3, e->x + 16, e->y + 8);
                /* Dome */
                SetAPen(rp, COL_CYAN);
                RectFill(rp, e->x + 6, e->y, e->x + 13, e->y + 2);
                /* Bottom */
                SetAPen(rp, COL_DKGRAY);
                RectFill(rp, e->x, e->y + 7, e->x + 19, e->y + 10);
                /* Lights */
                SetAPen(rp, COL_RED);
                RectFill(rp, e->x + 2, e->y + 9, e->x + 3, e->y + 10);
                RectFill(rp, e->x + 16, e->y + 9, e->x + 17, e->y + 10);
                break;

            case ENEMY_METEOR:
                /* Meteor - pink/orange */
                SetAPen(rp, COL_PINK);
                RectFill(rp, e->x + 1, e->y + 1, e->x + 6, e->y + 6);
                SetAPen(rp, COL_ORANGE);
                RectFill(rp, e->x + 2, e->y + 2, e->x + 5, e->y + 5);
                /* Trail */
                SetAPen(rp, COL_RED);
                RectFill(rp, e->x + 7, e->y - 2, e->x + 8, e->y);
                break;
        }
    }
}

static void draw_bombs_all(struct RastPort *rp, GameState *gs)
{
    WORD i;

    for (i = 0; i < MAX_BOMBS; i++) {
        Bomb *b = &gs->bombs[i];
        if (!b->active) continue;

        SetAPen(rp, COL_BLUE);
        RectFill(rp, b->x - 1, b->y - 1, b->x + 2, b->y + 2);
        SetAPen(rp, COL_WHITE);
        RectFill(rp, b->x, b->y, b->x, b->y);
    }
}

static void draw_explosions_all(struct RastPort *rp, GameState *gs)
{
    WORD i;

    for (i = 0; i < MAX_EXPLOSIONS; i++) {
        Explosion *e = &gs->explosions[i];
        WORD r, x1, y1, x2, y2;
        if (!e->active) continue;

        r = e->radius;

        /* Outer ring */
        SetAPen(rp, (e->life > 6) ? COL_WHITE : COL_ORANGE);
        x1 = e->x - r;
        y1 = e->y - r;
        x2 = e->x + r;
        y2 = e->y + r;
        if (x1 < 0) x1 = 0;
        if (y1 < 0) y1 = 0;
        if (x2 >= SCREEN_W) x2 = SCREEN_W - 1;
        if (y2 >= SCREEN_H) y2 = SCREEN_H - 1;
        if (x1 <= x2 && y1 <= y2)
            RectFill(rp, x1, y1, x2, y2);

        /* Inner core */
        if (r > 3) {
            SetAPen(rp, (e->life > 8) ? COL_BRYELLOW : COL_RED);
            x1 = e->x - r / 2;
            y1 = e->y - r / 2;
            x2 = e->x + r / 2;
            y2 = e->y + r / 2;
            if (x1 < 0) x1 = 0;
            if (y1 < 0) y1 = 0;
            if (x2 >= SCREEN_W) x2 = SCREEN_W - 1;
            if (y2 >= SCREEN_H) y2 = SCREEN_H - 1;
            if (x1 <= x2 && y1 <= y2)
                RectFill(rp, x1, y1, x2, y2);
        }
    }
}

/* ---- HUD ---- */

void draw_hud(struct RastPort *rp, GameState *gs)
{
    char buf[32];
    WORD i;

    /* Score */
    SetAPen(rp, COL_BRYELLOW);
    draw_string_at(rp, 4, 2, "SCORE", 1);
    long_to_str(buf, (long)gs->score);
    draw_string_at(rp, 40, 2, buf, 1);

    /* Lives */
    SetAPen(rp, COL_WHITE);
    draw_string_at(rp, 4, 12, "LIVES", 1);
    for (i = 0; i < gs->lives && i < 5; i++) {
        SetAPen(rp, COL_YELLOW);
        RectFill(rp, 40 + i * 10, 12, 46 + i * 10, 17);
    }

    /* Checkpoint section indicator */
    {
        char sec[6];
        sec[0] = 'A' + gs->checkpoint_cur;
        sec[1] = '-';
        sec[2] = 'A' + gs->checkpoint_cur + 1;
        if (gs->checkpoint_cur + 1 >= 26) sec[2] = 'A';
        sec[3] = '\0';

        SetAPen(rp, COL_GREEN);
        draw_string_at(rp, SCREEN_W / 2 - 12, 2, sec, 1);
    }

    /* Checkpoint progress bar */
    {
        WORD bar_w = 60;
        WORD progress = (WORD)(bar_w - (gs->checkpoint_dist * bar_w / CHECKPOINT_DIST));
        if (progress < 0) progress = 0;
        if (progress > bar_w) progress = bar_w;

        SetAPen(rp, COL_DKGRAY);
        RectFill(rp, SCREEN_W / 2 - 30, 12, SCREEN_W / 2 + 30, 14);
        if (progress > 0) {
            SetAPen(rp, COL_GREEN);
            RectFill(rp, SCREEN_W / 2 - 30, 12,
                     SCREEN_W / 2 - 30 + progress, 14);
        }
    }

    /* High score area */
    SetAPen(rp, COL_WHITE);
    draw_string_at(rp, SCREEN_W - 72, 2, "HI", 1);
    /* Just show current score as high score placeholder */
    long_to_str(buf, (long)gs->score);
    draw_string_at(rp, SCREEN_W - 56, 2, buf, 1);
}

/* ---- Full game frame ---- */

#ifdef __MINT__
/* ST port: rows 0-19 (score line) are the ST layer's HUD layer, drawn by
 * main_st.c only when they change; the rest is cleared (movem fill) and
 * redrawn every frame like the original */
#define HUD_ROWS 20
static WORD mtn_far_top[SCREEN_W], mtn_near_top[SCREEN_W];

/* both silhouettes, same formulas as draw_far/near_mountains; the far
 * one is only drawn above the near one (identical picture, far fewer
 * pixels: the near mountains are drawn over it anyway) */
static void draw_mountains(struct RastPort *rp, LONG scroll_x)
{
    WORD x, bot[SCREEN_W];
    WORD foff = (WORD)((scroll_x / 400) % 640), noff = (WORD)((scroll_x / 200) % 640);
    for (x = 0; x < SCREEN_W; x += 2) {
        WORD peak = approx_sin((WORD)((x + foff) * 3 / 2));
        WORD h = 120 + 20 - (peak * 40 / 255);
        if (h < 90) h = 90;
        if (h > 140) h = 140;
        mtn_far_top[x] = mtn_far_top[x + 1] = h;
        peak = approx_sin((WORD)((x + noff) * 5 / 2));
        h = 100 + 30 - (peak * 60 / 255);
        if (h < 80) h = 80;
        if (h > 160) h = 160;
        mtn_near_top[x] = mtn_near_top[x + 1] = h;
        bot[x] = bot[x + 1] = h - 1 < 160 ? h - 1 : 160;
    }
    gfx_vspans(rp, 0, SCREEN_W, mtn_far_top, bot, COL_DKPURPLE);
    for (x = 0; x < SCREEN_W; x++)
        bot[x] = 170;
    gfx_vspans(rp, 0, SCREEN_W, mtn_near_top, bot, COL_PURPLE);
}

/* STE: both mountain layers are pre-rendered once (same formulas) into
 * strips 960 pixels wide - for both pairings of the 2 pixel columns -
 * and the blitter copies the far window and ORs the near one on top each
 * frame (near colour 3 contains far colour 2's bits, so OR gives the
 * original near-over-far picture) */
#define STRIP_W   960
#define STRIP_BPR (STRIP_W / 2)
static UWORD *strip[2][2];      /* [far/near][column pairing] */
static int band_y0, band_rows, strips_tried;

static WORD mtn_far_h(WORD u)
{
    WORD peak = approx_sin((WORD)(u * 3 / 2));
    WORD h = 120 + 20 - (peak * 40 / 255);
    return h < 90 ? 90 : h > 140 ? 140 : h;
}

static WORD mtn_near_h(WORD u)
{
    WORD peak = approx_sin((WORD)(u * 5 / 2));
    WORD h = 100 + 30 - (peak * 60 / 255);
    return h < 80 ? 80 : h > 160 ? 160 : h;
}

static int build_strips(void)
{
    struct RastPort srp;
    UWORD *scratch = (UWORD *)AllocMem(32000, MEMF_CLEAR);
    WORD top[SCREEN_W], bot[SCREEN_W];
    int layer, par, chunk, x, r;

    band_y0 = GFX_Y(80);
    band_rows = GFX_Y(170) - band_y0 + 1;
    if (!scratch) return 0;
    for (layer = 0; layer < 2; layer++)
        for (par = 0; par < 2; par++)
            if (!(strip[layer][par] = (UWORD *)AllocMem((ULONG)STRIP_BPR * band_rows, MEMF_CLEAR)))
                return 0;
    memset(&srp, 0, sizeof(srp));
    srp.base = scratch;
    for (layer = 0; layer < 2; layer++)
        for (par = 0; par < 2; par++)
            for (chunk = 0; chunk < STRIP_W / SCREEN_W; chunk++) {
                memset(scratch, 0, 32000);
                for (x = 0; x < SCREEN_W; x++) {
                    WORD u = chunk * SCREEN_W + x;
                    WORD ps = u - ((u - par) & 1);      /* pair start */
                    top[x] = layer ? mtn_near_h(ps) : mtn_far_h(ps);
                    bot[x] = layer ? 170 : 160;
                }
                gfx_vspans(&srp, 0, SCREEN_W, top, bot, layer ? COL_PURPLE : COL_DKPURPLE);
                for (r = 0; r < band_rows; r++)
                    memcpy((UBYTE *)strip[layer][par] + (long)r * STRIP_BPR + chunk * 160,
                           scratch + (band_y0 + r) * 80, 160);
            }
    FreeMem(scratch, 32000);
    return 1;
}

/* STE: the terrain ring (TERRAIN_LEN columns) is kept rendered in a
 * strip of TERRAIN_LEN + 320 columns (the first 320 repeated at the end
 * so any window is contiguous).  Columns are re-rendered only when the
 * game rewrites their ring entries (compared with a shadow copy), and the
 * blitter copies the window each frame.  Mine dots depend on the screen
 * x, so they are drawn per frame as before. */
#define TSTRIP_W   (TERRAIN_LEN + SCREEN_W)
#define TSTRIP_BPR (TSTRIP_W / 2)
#define TBAND_Y0   180
#define TBAND_Y1   (SCREEN_H - 25)
static UWORD *tstrip;
static int tband_y0, tband_rows;
static WORD shadow_h[TERRAIN_LEN];
static UBYTE shadow_t[TERRAIN_LEN], shadow_ok;
static LONG shadow_gen;

static WORD band_ly[64];         /* logical row shown on each band row */

/* one ring column, the original's per-column drawing minus mine dots:
 * each physical row of the band gets the colour of the logical row it
 * shows (the last one drawn there in the original's order) */
static void tstrip_render(GameState *gs, int u)
{
    WORD th = gs->terrain_h[u], ttype = gs->terrain[u];
    int k, r;
    UBYTE col[64];
    for (r = 0; r < tband_rows; r++) {
        WORD ly = band_ly[r];
        UBYTE c = COL_BLACK;
        if (ttype == TERRAIN_CRATER_SM || ttype == TERRAIN_CRATER_LG) {
            if (ly >= GROUND_Y + 16) c = COL_DKBROWN;
        } else {
            if (ly >= th + 3) c = COL_DKBROWN;
            else if (ly >= th) c = COL_BROWN;
            else if (ttype == TERRAIN_ROCK && ly >= th - 10) c = COL_DKGRAY;
        }
        col[r] = c;
    }
    for (k = 0; k < (u < SCREEN_W ? 2 : 1); k++) {
        int x = u + k * TERRAIN_LEN;
        UWORD bit = 0x8000 >> (x & 15), nb = ~bit;
        UWORD *q = tstrip + (x >> 4) * 4;
        for (r = 0; r < tband_rows; r++, q += TSTRIP_BPR / 2) {
            UBYTE c = col[r];
            if (c & 1) q[0] |= bit; else q[0] &= nb;
            if (c & 2) q[1] |= bit; else q[1] &= nb;
            if (c & 4) q[2] |= bit; else q[2] &= nb;
            if (c & 8) q[3] |= bit; else q[3] &= nb;
        }
    }
}

static void draw_terrain_blit(struct RastPort *rp, GameState *gs)
{
    int u, x;
    LONG scroll_px = gs->scroll_x / 100;
    LONG raw = scroll_px % TERRAIN_LEN;
    if (raw < 0) raw += TERRAIN_LEN;

    /* game.c only rewrites the ring when it generates terrain */
    if (!shadow_ok || gs->terrain_gen_x != shadow_gen) {
        for (u = 0; u < TERRAIN_LEN; u++)
            if (!shadow_ok || shadow_h[u] != gs->terrain_h[u] || shadow_t[u] != gs->terrain[u]) {
                tstrip_render(gs, u);
                shadow_h[u] = gs->terrain_h[u];
                shadow_t[u] = (UBYTE)gs->terrain[u];
            }
        shadow_ok = 1;
        shadow_gen = gs->terrain_gen_x;
    }
    blit_area(tstrip, TSTRIP_BPR, (int)raw, 0, rp->base, 160, 0, tband_y0,
              SCREEN_W, tband_rows, BLIT_COPY);
    /* mine dots, as in the original (they follow the screen x) */
    SetAPen(rp, COL_RED);
    for (x = 0, u = (int)raw; x < SCREEN_W; x++) {
        if (gs->terrain[u] == TERRAIN_MINE && (x & 3) < 2)
            RectFill(rp, x, gs->terrain_h[u] - 4, x, gs->terrain_h[u] - 1);
        if (++u >= TERRAIN_LEN) u = 0;
    }
}

void draw_game(struct RastPort *rp, GameState *gs)
{
    if (!strips_tried) {
        strips_tried = 1;
        if (!blit_available() || !build_strips())
            strip[1][1] = NULL;
        else {
            tband_y0 = GFX_Y(TBAND_Y0);
            tband_rows = GFX_Y(TBAND_Y1) - tband_y0 + 1;
            tstrip = (UWORD *)AllocMem((ULONG)TSTRIP_BPR * tband_rows, MEMF_CLEAR);
            {
                int ly;
                for (ly = TBAND_Y0; ly <= TBAND_Y1; ly++)
                    band_ly[GFX_Y(ly) - tband_y0] = ly;
            }
            shadow_ok = 0;
        }
    }
    if (strip[1][1]) {
        WORD foff = (WORD)((gs->scroll_x / 400) % 640);
        WORD noff = (WORD)((gs->scroll_x / 200) % 640);
        /* rows outside the mountain band: cleared; the band is copied */
        gfx_fill_band(HUD_ROWS, 79, COL_BLACK);
        gfx_fill_band(171, tstrip ? TBAND_Y0 - 1 : SCREEN_H - 1, COL_BLACK);
        if (tstrip)
            gfx_fill_band(TBAND_Y1 + 1, SCREEN_H - 1, COL_BLACK);
        draw_stars(rp);
        blit_area(strip[0][foff & 1], STRIP_BPR, foff, 0, rp->base, 160, 0, band_y0,
                  SCREEN_W, band_rows, BLIT_COPY);
        blit_area(strip[1][noff & 1], STRIP_BPR, noff, 0, rp->base, 160, 0, band_y0,
                  SCREEN_W, band_rows, BLIT_OR);
    } else {
        gfx_fill_band(HUD_ROWS, SCREEN_H - 1, COL_BLACK);
        draw_stars(rp);
        draw_mountains(rp, gs->scroll_x);
    }

    /* Terrain */
    if (strip[1][1] && tstrip)
        draw_terrain_blit(rp, gs);
    else
        draw_terrain(rp, gs);

    /* Game objects */
    draw_bombs_all(rp, gs);
    draw_enemies_all(rp, gs);
    draw_bullets_all(rp, gs);
    draw_buggy(rp, gs);
    draw_explosions_all(rp, gs);
}
#else
void draw_game(struct RastPort *rp, GameState *gs)
{
    /* Clear screen */
    SetRast(rp, COL_BLACK);

    /* Parallax layers (back to front) */
    draw_stars(rp);
    draw_far_mountains(rp, gs->scroll_x);
    draw_near_mountains(rp, gs->scroll_x);

    /* Terrain */
    draw_terrain(rp, gs);

    /* Game objects */
    draw_bombs_all(rp, gs);
    draw_enemies_all(rp, gs);
    draw_bullets_all(rp, gs);
    draw_buggy(rp, gs);
    draw_explosions_all(rp, gs);

    /* HUD */
    draw_hud(rp, gs);
}
#endif

/* ---- Title screen ---- */

void draw_title(struct RastPort *rp, WORD frame)
{
    WORD cx = SCREEN_W / 2;
    WORD w;

    SetRast(rp, COL_BLACK);

    /* Stars */
    draw_stars(rp);

    /* Title: LUNAR RIDER */
    SetAPen(rp, COL_CYAN);
    w = string_pixel_width("LUNAR RIDER", 3);
    draw_string_at(rp, cx - w / 2, 40, "LUNAR RIDER", 3);

    /* Subtitle */
    SetAPen(rp, COL_PURPLE);
    w = string_pixel_width("LUNAR DEFENSE FORCE", 1);
    draw_string_at(rp, cx - w / 2, 75, "LUNAR DEFENSE FORCE", 1);

    /* Draw a buggy sprite on title */
    SetAPen(rp, COL_YELLOW);
    RectFill(rp, cx - 12, 100, cx + 12, 106);
    RectFill(rp, cx - 6, 96, cx + 6, 99);
    /* Wheels */
    SetAPen(rp, COL_GRAY);
    RectFill(rp, cx - 10, 107, cx - 6, 111);
    RectFill(rp, cx + 6, 107, cx + 10, 111);
    /* Gun */
    SetAPen(rp, COL_ORANGE);
    RectFill(rp, cx + 12, 101, cx + 16, 103);
    RectFill(rp, cx, 92, cx + 2, 96);

    /* Ground line */
    SetAPen(rp, COL_BROWN);
    RectFill(rp, 0, 112, SCREEN_W - 1, 114);

    /* Controls */
    SetAPen(rp, COL_WHITE);
    w = string_pixel_width("CONTROLS", 2);
    draw_string_at(rp, cx - w / 2, 130, "CONTROLS", 2);

    SetAPen(rp, COL_GREEN);
    draw_string_at(rp, 40, 150, "LEFT/RIGHT   SPEED", 1);
    draw_string_at(rp, 40, 160, "UP/SPACE     JUMP", 1);
    draw_string_at(rp, 40, 170, "FIRE/A       SHOOT", 1);
    draw_string_at(rp, 40, 180, "ESC          QUIT", 1);

    SetAPen(rp, COL_PURPLE);
    draw_string_at(rp, 40, 196, "JOYSTICK PORT 2 SUPPORTED", 1);

    /* Blinking prompt */
    if ((frame / 20) & 1) {
        SetAPen(rp, COL_BRYELLOW);
        w = string_pixel_width("PRESS FIRE TO START", 2);
        draw_string_at(rp, cx - w / 2, 220, "PRESS FIRE TO START", 2);
    }
}

/* ---- Game over screen ---- */

void draw_gameover(struct RastPort *rp, LONG score)
{
    WORD cx = SCREEN_W / 2;
    WORD w;
    char buf[32];

    SetAPen(rp, COL_RED);
    w = string_pixel_width("GAME OVER", 3);
    draw_string_at(rp, cx - w / 2, SCREEN_H / 2 - 30, "GAME OVER", 3);

    SetAPen(rp, COL_BRYELLOW);
    long_to_str(buf, (long)score);
    w = string_pixel_width(buf, 2);
    draw_string_at(rp, cx - w / 2, SCREEN_H / 2 + 10, buf, 2);
}

/* ---- Checkpoint celebration ---- */

void draw_checkpoint(struct RastPort *rp, WORD checkpoint)
{
    WORD cx = SCREEN_W / 2;
    WORD w;
    char msg[20];

    msg[0] = 'P';
    msg[1] = 'O';
    msg[2] = 'I';
    msg[3] = 'N';
    msg[4] = 'T';
    msg[5] = ' ';
    msg[6] = 'A' + checkpoint;
    msg[7] = '\0';

    SetAPen(rp, COL_GREEN);
    w = string_pixel_width(msg, 2);
    draw_string_at(rp, cx - w / 2, 30, msg, 2);

    SetAPen(rp, COL_BRYELLOW);
    w = string_pixel_width("1000 BONUS", 1);
    draw_string_at(rp, cx - w / 2, 50, "1000 BONUS", 1);
}
