// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/* ST port: graphics.library subset from ../st_port/st_gfx.c (draw.h) */
#include <string.h>
#include "draw.h"
#include "game.h"

/* 5x7 bitmap font - each character is 7 bytes, bits 4-0 = columns L-R */
static const UBYTE font_data[36][7] = {
    /* 0 */ {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},
    /* 1 */ {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
    /* 2 */ {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},
    /* 3 */ {0x0E,0x11,0x01,0x06,0x01,0x11,0x0E},
    /* 4 */ {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},
    /* 5 */ {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
    /* 6 */ {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},
    /* 7 */ {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    /* 8 */ {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
    /* 9 */ {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
    /* A */ {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},
    /* B */ {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
    /* C */ {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
    /* D */ {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C},
    /* E */ {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
    /* F */ {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    /* G */ {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},
    /* H */ {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    /* I */ {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
    /* J */ {0x07,0x02,0x02,0x02,0x02,0x12,0x0C},
    /* K */ {0x11,0x12,0x14,0x18,0x14,0x12,0x11},
    /* L */ {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    /* M */ {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},
    /* N */ {0x11,0x19,0x15,0x13,0x11,0x11,0x11},
    /* O */ {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
    /* P */ {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    /* Q */ {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},
    /* R */ {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    /* S */ {0x0E,0x11,0x10,0x0E,0x01,0x11,0x0E},
    /* T */ {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    /* U */ {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
    /* V */ {0x11,0x11,0x11,0x11,0x0A,0x0A,0x04},
    /* W */ {0x11,0x11,0x11,0x15,0x15,0x1B,0x11},
    /* X */ {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    /* Y */ {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},
    /* Z */ {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
};

static void draw_char(struct RastPort *rp, WORD x, WORD y, UBYTE color,
                       char ch, WORD scale)
{
    int idx, row, col;
    if (ch >= '0' && ch <= '9') idx = ch - '0';
    else if (ch >= 'A' && ch <= 'Z') idx = ch - 'A' + 10;
    else if (ch >= 'a' && ch <= 'z') idx = ch - 'a' + 10;
    else return;

    SetAPen(rp, color);
    for (row = 0; row < 7; row++) {
        UBYTE bits = font_data[idx][row];
        for (col = 0; col < 5; col++) {
            if (bits & (0x10 >> col)) {
                RectFill(rp, x + col * scale, y + row * scale,
                         x + col * scale + scale - 1,
                         y + row * scale + scale - 1);
            }
        }
    }
}

static void draw_text_scaled_direct(struct RastPort *rp, WORD x, WORD y, UBYTE color,
                                    const char *text, WORD scale)
{
    while (*text) {
        if (*text == ' ')
            x += 6 * scale;
        else {
            draw_char(rp, x, y, color, *text, scale);
            x += 6 * scale;
        }
        text++;
    }
}

/* ST port: scaled 5x7 text is one cached HUD operation (key: colour,
 * scale, text) instead of one RectFill per font pixel */
struct text_key { UBYTE color; UBYTE scale; char text[30]; };

static void text_fn(struct RastPort *rp, const void *ctx, WORD x, WORD y, WORD arg)
{
    const struct text_key *k = ctx;
    (void)arg;
    draw_text_scaled_direct(rp, x, y, k->color, k->text, k->scale);
}

void draw_text_scaled(struct RastPort *rp, WORD x, WORD y, UBYTE color,
                      const char *text, WORD scale)
{
    struct text_key k;
    int n = strlen(text);
    if (n > 29) n = 29;
    k.color = color;
    k.scale = (UBYTE)scale;
    memcpy(k.text, text, n);
    k.text[n] = '\0';
    gfx_cached(rp, text_fn, &k, 2 + n + 1, NULL, x, y, 0,
               (WORD)(n * 6 * scale), (WORD)(7 * scale));
}

/* Center text at y position */
static void draw_text_centered(struct RastPort *rp, WORD y, UBYTE color,
                                const char *text, WORD scale)
{
    int len = 0;
    const char *p = text;
    while (*p++) len++;
    WORD w = len * 6 * scale;
    draw_text_scaled(rp, (SCREEN_W - w) / 2, y, color, text, scale);
}

/* Format a number into a string buffer, right-aligned in field_width chars */
static void format_number(char *buf, LONG val, int field_width)
{
    char tmp[12];
    int i = 0, j;
    LONG v = val;

    if (field_width <= 0) {
        if (buf) buf[0] = '\0';
        return;
    }

    if (v < 0) v = 0;

    if (v == 0) {
        tmp[i++] = '0';
    } else {
        while (v > 0 && i < (int)sizeof(tmp)) {
            tmp[i++] = '0' + (char)(v % 10);
            v /= 10;
        }
    }

    /* Clamp displayed digits to field_width so we never write before buf */
    if (i > field_width) i = field_width;

    /* Pad with spaces */
    for (j = 0; j < field_width - i; j++) buf[j] = ' ';
    /* Reverse digits (only the ones that fit) */
    for (j = 0; j < i; j++) buf[field_width - 1 - j] = tmp[j];
    buf[field_width] = '\0';
}

/*
 * Sprites as character art: '#' is the body colour, 'o' the accent
 * colour, anything else is transparent (turned into masks below). All rows of a sprite are the
 * same width.
 */
#define ALIEN_ROWS_PX 10

/* Type C (rows 0-1, 30 pts): eye pod trailing tendrils */
static const char *const art_c[2][ALIEN_ROWS_PX] = {
    { "....######....",
      "..##########..",
      ".####oooo####.",
      "####oo..oo####",
      ".####oooo####.",
      "..##########..",
      "...#..##..#...",
      "..#...##...#..",
      ".#....##....#.",
      "..#........#.." },
    { "....######....",
      "..##########..",
      ".####oooo####.",
      "####o..oo#####",
      ".####oooo####.",
      "..##########..",
      "...#..##..#...",
      "....#.##.#....",
      "...#..##..#...",
      "....#....#...." }
};
/* Type B (rows 2-3, 20 pts): ray beating its wings */
static const char *const art_b[2][ALIEN_ROWS_PX] = {
    { "#............#",
      "##....##....##",
      "###..####..###",
      "##############",
      ".####o##o####.",
      "..##########..",
      "....##..##....",
      "...##....##...",
      "..............",
      ".............." },
    { "..............",
      "..............",
      "......##......",
      "#....####....#",
      "##############",
      ".####o##o####.",
      "..##########..",
      "....##..##....",
      "....#....#....",
      ".............." }
};
/* Type A (rows 4-5, 10 pts): spore crawler */
static const char *const art_a[2][ALIEN_ROWS_PX] = {
    { "......##......",
      ".....####.....",
      "....##oo##....",
      "...########...",
      "..##.####.##..",
      ".##..####..##.",
      "##...#..#...##",
      "....##..##....",
      "...##....##...",
      ".............." },
    { "......##......",
      ".....####.....",
      "....##oo##....",
      "...########...",
      "..##.####.##..",
      "..##.####.##..",
      "..#..#..#..#..",
      "...#.#..#.#...",
      "....#....#....",
      ".............." }
};
/* Player: delta fighter with a cockpit */
static const char *const art_player[PLAYER_H] = {
    ".......#.......",
    "......###......",
    "......#o#......",
    "..#..#####..#..",
    "..#.#######.#..",
    ".#############.",
    "###############",
    "##.##.....##.##"
};
/* Comet: the bonus target crossing the top */
static const char *const art_ufo[UFO_H] = {
    "........oo......",
    "......oooooo....",
    "####ooooooooo...",
    "..##############",
    "####ooooooooo...",
    "......oooooo....",
    "........oo......"
};

/* Colours per alien type: body, accent */
/* (ST port: a body pen of one or two bit planes and an accent pen on
 * another single plane, so a row of aliens is little more than one or
 * two planes of blitting; the 3DO has magenta/yellow, bright green/white
 * and light blue/orange) */
static const UBYTE alien_colors[3][2] = {
    { COL_RED,   COL_YELLOW },     /* Type C */
    { COL_GREEN, COL_WHITE },      /* Type B */
    { COL_CYAN,  COL_YELLOW }      /* Type A */
};

/* ST port: an alien's two colours as 16 pixel masks, built once:
 * mask[type][frame][0] body, [1] accent */
static UWORD alien_mask[3][2][2][ALIEN_ROWS_PX];
/* ... and as bit plane masks for gfx_or16_row_planes() */
static UWORD alien_planes[3][2][4][16];

static void build_alien_masks(void)
{
    static const char *const *const arts[3] = { art_c[0], art_b[0], art_a[0] };
    int t, f, r, c;
    for (t = 0; t < 3; t++)
        for (f = 0; f < 2; f++) {
            const char *const *art = t == ALIEN_TYPE_C ? art_c[f] :
                                     t == ALIEN_TYPE_B ? art_b[f] : art_a[f];
            for (r = 0; r < ALIEN_ROWS_PX; r++) {
                UWORD body = 0, acc = 0;
                for (c = 0; c < ALIEN_W; c++) {
                    if (art[r][c] == '#') body |= 0x8000 >> c;
                    else if (art[r][c] == 'o') acc |= 0x8000 >> c;
                }
                alien_mask[t][f][0][r] = body;
                alien_mask[t][f][1][r] = acc;
                for (c = 0; c < 4; c++)
                    alien_planes[t][f][c][r] =
                        ((alien_colors[t][0] >> c & 1) ? body : 0) |
                        ((alien_colors[t][1] >> c & 1) ? acc : 0);
            }
        }
    (void)arts;
}

static void draw_alien(struct RastPort *rp, WORD x, WORD y, WORD type,
                        WORD frame);

/* ST port: on the HUD layer (title) an alien is one cached operation */
static void alien_fn(struct RastPort *rp, const void *ctx, WORD x, WORD y, WORD arg)
{
    (void)ctx;
    draw_alien(rp, x, y, arg >> 1, arg & 1);   /* rp isn't recording here */
}

static void draw_alien(struct RastPort *rp, WORD x, WORD y, WORD type,
                        WORD frame)
{
    if (rp->record) {
        WORD arg = (WORD)(type << 1 | (frame & 1));
        gfx_cached(rp, alien_fn, &arg, sizeof(arg), NULL, x, y, arg, ALIEN_W, ALIEN_ROWS_PX);
        return;
    }
    if (!alien_mask[0][0][0][0])
        build_alien_masks();
    gfx_mask16(rp, x, y, alien_mask[type][frame & 1][0], ALIEN_ROWS_PX, alien_colors[type][0]);
    gfx_mask16(rp, x, y, alien_mask[type][frame & 1][1], ALIEN_ROWS_PX, alien_colors[type][1]);
}

/* ST port: the player and the comet as two-colour 16 pixel masks on the
 * sprite layer; one cached operation each on the HUD layer */
static UWORD player_mask[2][PLAYER_H], ufo_mask[2][UFO_H];

static void build_mask(const char *const *art, int w, int h, UWORD *body, UWORD *acc)
{
    int r, c;
    for (r = 0; r < h; r++) {
        body[r] = acc[r] = 0;
        for (c = 0; c < w; c++) {
            if (art[r][c] == '#') body[r] |= 0x8000 >> c;
            else if (art[r][c] == 'o') acc[r] |= 0x8000 >> c;
        }
    }
}

static void draw_art2(struct RastPort *rp, WORD x, WORD y, WORD which);

static void art2_fn(struct RastPort *rp, const void *ctx, WORD x, WORD y, WORD arg)
{
    (void)ctx;
    draw_art2(rp, x, y, arg);   /* rp isn't recording here */
}

/* which: 0 player, 1 comet */
static void draw_art2(struct RastPort *rp, WORD x, WORD y, WORD which)
{
    if (rp->record) {
        gfx_cached(rp, art2_fn, &which, sizeof(which), NULL, x, y, which,
                   16, which ? UFO_H : PLAYER_H);
        return;
    }
    if (!player_mask[0][PLAYER_H - 1]) {
        build_mask(art_player, PLAYER_W, PLAYER_H, player_mask[0], player_mask[1]);
        build_mask(art_ufo, UFO_W, UFO_H, ufo_mask[0], ufo_mask[1]);
    }
    if (which) {
        gfx_mask16(rp, x, y, ufo_mask[0], UFO_H, COL_DKORANGE);
        gfx_mask16(rp, x, y, ufo_mask[1], UFO_H, COL_WHITE);
    } else {
        gfx_mask16(rp, x, y, player_mask[0], PLAYER_H, COL_YELLOW);
        gfx_mask16(rp, x, y, player_mask[1], PLAYER_H, COL_CYAN);
    }
}

static void draw_player(struct RastPort *rp, WORD x, WORD y)
{
    draw_art2(rp, x, y, 0);
}

static void draw_ufo(struct RastPort *rp, WORD x, WORD y)
{
    draw_art2(rp, x, y, 1);
}

static void shield_fn(struct RastPort *rp, const void *ctx, WORD px, WORD py, WORD arg)
{
    const Shield *sh = ctx;
    int y, x;
    (void)px; (void)py; (void)arg;
    SetAPen(rp, COL_CYAN);
    for (y = 0; y < SHIELD_H; y++) {
        x = 0;
        while (x < SHIELD_W) {
            WORD start;
            while (x < SHIELD_W && !sh->pixels[y][x]) x++;
            if (x >= SHIELD_W) break;
            start = x;
            while (x < SHIELD_W && sh->pixels[y][x]) x++;
            RectFill(rp, sh->x + start, sh->y + y, sh->x + x - 1, sh->y + y);
        }
    }
    /* facet highlights */
    SetAPen(rp, COL_WHITE);
    for (y = 0; y < SHIELD_H; y++)
        for (x = 0; x < SHIELD_W; x++)
            if (sh->pixels[y][x] == 2)
                WritePixel(rp, sh->x + x, sh->y + y);
}

/* ST port: each shield is one cached HUD operation, redrawn only when
 * bullets have eaten into it (key = checksum of its pixels) */
static void draw_shields(struct RastPort *rp, GameState *gs)
{
    int s, i;
    for (s = 0; s < SHIELD_COUNT; s++) {
        const Shield *sh = &gs->shields[s];
        const ULONG *p = (const ULONG *)&sh->pixels[0][0];   /* even address */
        ULONG sum = 0;
        for (i = 0; i < SHIELD_H * SHIELD_W / 4; i++)
            sum = (sum << 1 | sum >> 31) ^ p[i];
        gfx_cached(rp, shield_fn, &sum, sizeof(sum), sh, sh->x, sh->y, 0,
                   SHIELD_W, SHIELD_H);
    }
}

static void draw_explosions(struct RastPort *rp, GameState *gs)
{
    int i;
    for (i = 0; i < MAX_EXPLOSIONS; i++) {
        Explosion *ex = &gs->explosions[i];
        WORD size, cx, cy;
        if (!ex->active) continue;

        size = 10 - ex->timer;
        SetAPen(rp, ex->color);

        /* Expanding cross pattern */
        cx = ex->x; cy = ex->y;
        if (size > 0) {
            RectFill(rp, cx - size, cy - 1, cx + size, cy + 1);
            RectFill(rp, cx - 1, cy - size, cx + 1, cy + size);
        }
        /* Dots at diagonals */
        if (size > 2) {
            WORD d = size - 1;
            WritePixel(rp, cx - d, cy - d);
            WritePixel(rp, cx + d, cy - d);
            WritePixel(rp, cx - d, cy + d);
            WritePixel(rp, cx + d, cy + d);
        }
    }
}

static void draw_hud(struct RastPort *rp, GameState *gs)
{
    char buf[20];
    int i;

    /* Wave, top right */
    draw_text_scaled(rp, 262, 4, COL_GRAY, "WAVE", 1);
    format_number(buf, (LONG)gs->wave, 2);
    draw_text_scaled(rp, 290, 4, COL_WHITE, buf, 1);

    /* Bottom bar under the ground line: points, best, reserve ships */
    SetAPen(rp, COL_CYAN);
    RectFill(rp, 0, 242, SCREEN_W - 1, 242);

    draw_text_scaled(rp, 6, 246, COL_GRAY, "PTS", 1);
    format_number(buf, gs->score, 6);
    draw_text_scaled(rp, 26, 246, COL_YELLOW, buf, 1);

    draw_text_scaled(rp, 116, 246, COL_GRAY, "BEST", 1);
    format_number(buf, gs->hiscore, 6);
    draw_text_scaled(rp, 142, 246, COL_YELLOW, buf, 1);

    for (i = 0; i < gs->lives - 1 && i < 4; i++) {
        WORD lx = SCREEN_W - 20 - i * 18;
        draw_player(rp, lx, 246);
    }
}

/* ST port: the original draw_game() cleared the screen and redrew
 * everything each frame.  On the ST it's split by layer: */

/* HUD layer (recorded & diffed): score text, reserve ships, shields */
void draw_game_hud(struct RastPort *rp, GameState *gs)
{
    draw_hud(rp, gs);
    draw_shields(rp, gs);

    /* Wave clear message */
    if (gs->state == STATE_WAVE_CLEAR) {
        draw_text_centered(rp, 120, COL_YELLOW, "WAVE CLEAR", 2);
    }
}

/* sprite layer (back buffer, undone next frame): everything that moves */
void draw_game_sprites(struct RastPort *rp, GameState *gs)
{
    int i, r, c;

    /* Aliens: the swarm steps every frame or two, so they're sprites:
     * one OR blit per alien row (black background), all colours at
     * once, see gfx_or16_row_planes().  (On the ST the whole swarm
     * flaps in step: one sprite per row keeps it to one blit.) */
    if (!alien_mask[0][0][0][0])
        build_alien_masks();
    for (r = 0; r < ALIEN_ROWS; r++) {
        WORD type = r <= 1 ? ALIEN_TYPE_C : r <= 3 ? ALIEN_TYPE_B : ALIEN_TYPE_A;
        WORD xs[ALIEN_COLS];
        int n = 0;
        for (c = 0; c < ALIEN_COLS; c++)
            if (gs->swarm.alive[r][c])
                xs[n++] = ALIEN_X(gs, r, c);
        if (n)
            gfx_or16_row_planes(rp, xs, n, ALIEN_Y(gs, r),
                                (const UWORD (*)[16])alien_planes[type][gs->swarm.anim_frame & 1],
                                ALIEN_ROWS_PX);
    }

    /* Draw UFO */
    if (gs->ufo.active) {
        draw_ufo(rp, gs->ufo.x, UFO_Y);
    }

    /* Draw player (unless dying and blinking) */
    if (gs->state != STATE_DYING || (gs->state_timer & 4)) {
        draw_player(rp, gs->player_x, PLAYER_Y);
    }

    /* Draw player bullet */
    if (gs->player_bullet.active) {
        SetAPen(rp, COL_CYAN);
        RectFill(rp, gs->player_bullet.x - 1, gs->player_bullet.y,
                 gs->player_bullet.x, gs->player_bullet.y + 5);
        SetAPen(rp, COL_WHITE);
        RectFill(rp, gs->player_bullet.x - 1, gs->player_bullet.y,
                 gs->player_bullet.x, gs->player_bullet.y + 1);
    }

    /* Draw alien bullets */
    SetAPen(rp, COL_MAGENTA);
    for (i = 0; i < MAX_ALIEN_BULLETS; i++) {
        WORD bx, by;
        if (!gs->alien_bullets[i].active) continue;
        bx = gs->alien_bullets[i].x;
        by = gs->alien_bullets[i].y;
        /* Spinning spore: alternates a plus and an x */
        if (by & 4) {
            RectFill(rp, bx - 1, by, bx + 1, by + 2);
            WritePixel(rp, bx, by - 1);
            WritePixel(rp, bx, by + 3);
        } else {
            WritePixel(rp, bx - 1, by);
            WritePixel(rp, bx + 1, by);
            WritePixel(rp, bx, by + 1);
            WritePixel(rp, bx - 1, by + 2);
            WritePixel(rp, bx + 1, by + 2);
        }
    }

    /* Draw explosions */
    draw_explosions(rp, gs);

}

void draw_title(struct RastPort *rp, GameState *gs)
{
    SetRast(rp, 0);

    /* Title */
    draw_text_centered(rp, 24, COL_MAGENTA, "NOVA", 3);
    draw_text_centered(rp, 54, COL_CYAN, "DEFENSE", 3);

    /* The invaders in a line, with their points underneath */
    {
        WORD f = (gs->title_blink >> 4) & 1;
        draw_text_centered(rp, 92, COL_GRAY, "KNOW YOUR ENEMY", 1);
        draw_alien(rp, 57, 110, ALIEN_TYPE_C, f);
        draw_text_scaled(rp, 55, 126, COL_WHITE, "30", 1);
        draw_alien(rp, 117, 110, ALIEN_TYPE_B, f);
        draw_text_scaled(rp, 115, 126, COL_WHITE, "20", 1);
        draw_alien(rp, 177, 110, ALIEN_TYPE_A, f);
        draw_text_scaled(rp, 175, 126, COL_WHITE, "10", 1);
        draw_ufo(rp, 236, 112);
        draw_text_scaled(rp, 232, 126, COL_WHITE, "COMET", 1);
        draw_player(rp, SCREEN_W / 2 - PLAYER_W / 2, 152);
    }

    /* Hi-score */
    if (gs->hiscore > 0) {
        char buf[20];
        draw_text_centered(rp, 182, COL_GRAY, "BEST", 1);
        format_number(buf, gs->hiscore, 6);
        draw_text_centered(rp, 192, COL_YELLOW, buf, 1);
    }

    /* Blinking "PRESS FIRE" */
    if ((gs->title_blink >> 3) & 1) {
        draw_text_centered(rp, 215, COL_WHITE, "PRESS FIRE TO START", 1);
    }

    /* Credits */
    draw_text_centered(rp, 240, COL_GRAY, "MOUSE AND KEYBOARD", 1);
}

void draw_gameover(struct RastPort *rp, GameState *gs)
{
    char buf[20];

    SetRast(rp, 0);

    draw_text_centered(rp, 80, COL_RED, "GAME OVER", 3);

    draw_text_centered(rp, 130, COL_WHITE, "SCORE", 2);
    format_number(buf, gs->score, 6);
    draw_text_centered(rp, 155, COL_YELLOW, buf, 2);

    draw_text_centered(rp, 185, COL_GRAY, "WAVE", 1);
    format_number(buf, (LONG)gs->wave, 2);
    draw_text_centered(rp, 195, COL_WHITE, buf, 1);

    if ((gs->state_timer & 8)) {
        draw_text_centered(rp, 225, COL_WHITE, "PRESS FIRE", 1);
    }
}
