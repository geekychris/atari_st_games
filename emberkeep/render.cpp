// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#include "render.h"
#include "game.h"
#include "tilesheet_data.h"

#if defined(__MINT__)
#include <stdio.h>
#include <string.h>
#include "fgfx.h"
#else
#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <graphics/view.h>
#include <graphics/rastport.h>
#include <graphics/text.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>

#include <stdio.h>
#include <string.h>

/*
 * Atari Falcon port: the __MINT__ branches below replace the AGA screen
 * with the Falcon layer (../falcon_port/fgfx: 320x240 16 bit true colour,
 * triple buffered) and draw tiles straight into it. The picture is the
 * same; see README.md.
 *
 * emberkeep renderer — 320x256 8bpp AGA screen, ScreenBuffer double-
 * buffered, per-scanline RectFill runs against a chunky tile sheet.
 * Same architecture that worked reliably for miner_meteor.
 *
 * The signature effect: a rectangular "lantern cone" masks the field
 * outside a small radius around the player with pen 0 (black). Every
 * frame we redraw the whole level normally, then paint four black
 * rectangles around the lantern box. Fast, works on any display, no
 * blitting tricks needed. The visual is a hard-edge square light —
 * softer circle mask is a follow-up.
 *
 * Palette (16 slots at the top of the 256-entry AGA colormap):
 *   0  black
 *   1  white
 *   2  brick red      (walls)
 *   3  brown          (wall variant / trim)
 *   4  cyan           (enemy A)
 *   5  magenta        (enemy B)
 *   6  yellow         (rune / oil highlight)
 *   7  green          (portal)
 *   8  dark grey      (hazard)
 *   9  bright red     (player body)
 *   10 orange         (lantern flame)
 *   11 blue           (title chrome)
 *   12..15            greys / dim
 */
#endif

#if defined(__MINT__)
/* Atari Falcon port: fgfx owns the screens (320x240 16 bit true colour,
 * triple buffered); the game keeps its 320x256 coordinates and Y is
 * mapped to the 240 lines. rp is the buffer being drawn this frame. */
static struct RastPort       *rp;
#else
#ifndef __PPC__
extern struct IntuitionBase *IntuitionBase;
extern struct GfxBase       *GfxBase;
#endif

static struct Screen         *scr;
static struct RastPort       *rp;
static struct ScreenBuffer   *sbuf[2];
static struct RastPort        rp_buf[2];
static UWORD                  cur_buf = 1;

static UBYTE                  scratch_tile[TILE_H][TILE_W];
#endif

static const UBYTE PALETTE[16][3] = {
    /* 0  bg           */ {   0,   0,   0 },
    /* 1  white        */ { 240, 240, 240 },
    /* 2  brick        */ { 168,  48,  48 },
    /* 3  brown        */ { 128,  88,  40 },
    /* 4  cyan         */ {  64, 208, 224 },
    /* 5  magenta      */ { 224,  80, 192 },
    /* 6  yellow       */ { 240, 224,  64 },
    /* 7  green        */ {  64, 200,  64 },
    /* 8  dark grey    */ {  72,  72,  80 },
    /* 9  bright red   */ { 240,  64,  64 },
    /* 10 orange       */ { 240, 160,  64 },
    /* 11 blue         */ {  80, 128, 240 },
    /* 12 grey         */ { 128, 128, 128 },
    /* 13 light green  */ { 168, 224, 128 },
    /* 14 pink         */ { 240, 168, 200 },
    /* 15 dim          */ {  40,  40,  40 },
};

#if defined(__MINT__)
static void install_palette(void)
{
    for (LONG i = 0; i < 16; i++)
        fgfx_set_rgb((int)i, PALETTE[i][0], PALETTE[i][1], PALETTE[i][2]);
}

static void begin_buffer(void)
{
    rp = fgfx_back();
    /* Port fix: text in JAM1. The Amiga RastPort default is JAM2 with
     * background pen 0, so the title / end-screen captions (pen 0 on a
     * coloured bar) come out as solid black boxes there. */
    SetDrMd(rp, JAM1);
}

LONG render_open(void)
{
    if (fgfx_init()) return 1;
    install_palette();
    for (int i = 0; i < 3; i++) {
        begin_buffer();
        SetRast(rp, 0);
        fgfx_swap();
    }
    begin_buffer();
    return 0;
}

void render_close(void)
{
    fgfx_exit();
}

struct Screen *render_get_screen(void) { return NULL; }

void render_flip(void)
{
    fgfx_swap();
    begin_buffer();
}
#else
static void install_palette(void)
{
    ULONG buf[2 + 16 * 3];
    buf[0] = (16UL << 16) | 0;
    for (LONG i = 0; i < 16; i++) {
        buf[1 + i * 3 + 0] = ((ULONG)PALETTE[i][0]) * 0x01010101ul;
        buf[1 + i * 3 + 1] = ((ULONG)PALETTE[i][1]) * 0x01010101ul;
        buf[1 + i * 3 + 2] = ((ULONG)PALETTE[i][2]) * 0x01010101ul;
    }
    buf[1 + 16 * 3] = 0;
    LoadRGB32(&scr->ViewPort, (ULONG *)buf);
}

LONG render_open(void)
{
    scr = OpenScreenTags(NULL,
        SA_Width,     SCREEN_W,
        SA_Height,    SCREEN_H,
        SA_Depth,     8,
        SA_Type,      CUSTOMSCREEN,
        SA_DisplayID, 0x00000L,
        SA_Title,     (ULONG)"emberkeep",
        SA_ShowTitle, FALSE,
        SA_Quiet,     TRUE,
        TAG_END);
    if (!scr) return 1;
    install_palette();

    sbuf[0] = AllocScreenBuffer(scr, NULL, SB_SCREEN_BITMAP);
    sbuf[1] = AllocScreenBuffer(scr, NULL, 0);
    if (!sbuf[0] || !sbuf[1]) { CloseScreen(scr); scr = NULL; return 2; }

    InitRastPort(&rp_buf[0]); rp_buf[0].BitMap = sbuf[0]->sb_BitMap;
    InitRastPort(&rp_buf[1]); rp_buf[1].BitMap = sbuf[1]->sb_BitMap;

    cur_buf = 1;
    rp = &rp_buf[cur_buf];

    for (int i = 0; i < 2; i++) {
        SetAPen(&rp_buf[i], 0);
        RectFill(&rp_buf[i], 0, 0, SCREEN_W - 1, SCREEN_H - 1);
    }
    return 0;
}

void render_close(void)
{
    if (scr && sbuf[0]) {
        int tries = 0;
        while (!ChangeScreenBuffer(scr, sbuf[0]) && ++tries < 5) WaitTOF();
        WaitTOF(); WaitTOF();
    }
    if (sbuf[1]) { FreeScreenBuffer(scr, sbuf[1]); sbuf[1] = NULL; }
    if (sbuf[0]) { FreeScreenBuffer(scr, sbuf[0]); sbuf[0] = NULL; }
    if (scr)     { CloseScreen(scr); scr = NULL; }
}

struct Screen *render_get_screen(void) { return scr; }

void render_flip(void)
{
    if (ChangeScreenBuffer(scr, sbuf[cur_buf])) {
        cur_buf ^= 1;
        rp = &rp_buf[cur_buf];
    }
}
#endif

static inline void fill_rect(LONG x, LONG y, LONG w, LONG h, UBYTE pen)
{
    if (w <= 0 || h <= 0) return;
    SetAPen(rp, pen);
    RectFill(rp, x, y, x + w - 1, y + h - 1);
}

void render_text(LONG x, LONG y, UBYTE pen, const char *text)
{
    SetAPen(rp, pen);
    Move(rp, x, y);
    Text(rp, (STRPTR)text, (LONG)strlen(text));
}

#if defined(__MINT__)
/* Falcon: each grid cell of each of the three screen buffers remembers
 * what it holds. Cells wholly outside the lantern's light only need to
 * be black and are filled once when they go dark; cells the light square
 * touches are redrawn every frame (tile, then sprites and mask clipped
 * to those cells). Tiles are written as RGB565 words, one physical row
 * per Amiga row except the 1 in 16 that the 256 -> 240 mapping drops. */
#define CELL_DIRTY  0xFFFF
#define CELL_BLACK  0xFFFE
static UWORD *cache_base[3];
static UWORD  cell_have[3][GRID_H][GRID_W];
static UWORD  pen_lut[16];

static void invalidate_cells(void)
{
    memset(cell_have, 0xFF, sizeof(cell_have));
}

/* the cache of the buffer being drawn */
static UWORD (*buffer_cells(void))[GRID_W]
{
    for (int i = 0; i < 3; i++)
        if (cache_base[i] == rp->base) return cell_have[i];
    for (int i = 0; i < 3; i++)
        if (!cache_base[i]) {
            cache_base[i] = rp->base;
            memset(cell_have[i], 0xFF, sizeof(cell_have[i]));
            return cell_have[i];
        }
    return cell_have[0];
}

/* Copy a 16x16 tile (opaque, every pixel - as the Amiga's run fills
 * did) to x,y, clipped to [cx0,cx1) x [cy0,cy1) (logical coordinates). */
static void blit_tile_clip(LONG x, LONG y, const UBYTE *src,
                           LONG cx0, LONG cy0, LONG cx1, LONG cy1)
{
    LONG x0 = x, y0 = y, x1 = x + TILE_W, y1 = y + TILE_H;
    if (x0 < cx0) x0 = cx0;
    if (y0 < cy0) y0 = cy0;
    if (x1 > cx1) x1 = cx1;
    if (y1 > cy1) y1 = cy1;
    if (x0 >= x1 || y0 >= y1) return;
    const WORD *ymap = fgfx_ymap();
    int w = (int)(x1 - x0), last = -1, h = fgfx_height();
    for (LONG row = y0; row < y1; row++) {
        int py = ymap[row];
        if (py == last || py >= h) continue;
        last = py;
        UWORD *d = rp->base + (ULONG)py * SCREEN_W + x0;
        const UBYTE *s = src + (row - y) * TILE_W + (x0 - x);
        for (int i = 0; i < w; i++) d[i] = pen_lut[s[i]];
    }
}

#define SHEET(r, c) (&tilesheet_data[r][c][0][0])
#else
/* Per-scanline same-pen RectFill runs from a 16x16 chunky tile — the
 * blit path that Just Worked in miner_meteor. */
static void blit_tile_data(LONG dest_x, LONG dest_y,
                           const UBYTE data[TILE_H][TILE_W])
{
    for (LONG py = 0; py < TILE_H; py++) {
        UBYTE run_pen = data[py][0];
        LONG  run_start = 0;
        for (LONG px = 1; px <= TILE_W; px++) {
            UBYTE p = (px < TILE_W) ? data[py][px] : (UBYTE)(run_pen ^ 1);
            if (p != run_pen) {
                SetAPen(rp, run_pen);
                RectFill(rp,
                    dest_x + run_start, dest_y + py,
                    dest_x + px - 1,    dest_y + py);
                run_pen   = p;
                run_start = px;
            }
        }
    }
}

static inline void blit_sheet_tile(LONG dest_x, LONG dest_y,
                                   LONG sheet_row, LONG sheet_col)
{
    blit_tile_data(dest_x, dest_y, tilesheet_data[sheet_row][sheet_col]);
}
#endif

/*
 * Sheet layout — mirror of tools/bake_tilesheet.py's grid.
 */
enum {
    SR_TERRAIN     = 0,
    SR_DECOR       = 1,
    SR_PLAYER_R    = 2,
    SR_PLAYER_L    = 3,
    SR_PLAYER_POSE = 4,
    SR_ENEMY_A     = 5,
    SR_ENEMY_B     = 6,
    SR_ENEMY_C     = 7,
};
enum {
    /* Terrain row */
    SC_FLOOR       = 0,
    SC_WALL_A      = 1,
    SC_WALL_B      = 2,
    SC_RUNE        = 3,
    SC_OIL         = 4,
    SC_HAZARD      = 5,
    /* SC 6-7 spare — leaves room to add e.g. torch sconce, chest */
    /* Decor row */
    SC_PORTAL_LOCKED = 0,
    SC_PORTAL_OPEN   = 1,
    /* Player-pose row */
    SC_POSE_UP     = 0,
    SC_POSE_RIGHT  = 1,
    SC_POSE_DOWN   = 2,
    SC_POSE_LEFT   = 3,
};

#if defined(__MINT__)
/* The sheet tile draw_tile() below picks, as row * 32 + col. */
static UWORD tile_choice(LONG col, LONG row, UBYTE kind,
                         LONG runes_remaining, ULONG tick)
{
    switch (kind) {
        case T_FLOOR:  return SR_TERRAIN * 32 + SC_FLOOR;
        case T_WALL:   return SR_TERRAIN * 32 + (((col + row) & 1) ? SC_WALL_A : SC_WALL_B);
        case T_RUNE:   return SR_TERRAIN * 32 + SC_RUNE;
        case T_OIL:    return SR_TERRAIN * 32 + SC_OIL;
        case T_HAZARD: return SR_TERRAIN * 32 + SC_HAZARD;
        case T_PORTAL:
            if (runes_remaining == 0)
                return SR_DECOR * 32 + (((tick / 6) & 1) ? SC_PORTAL_OPEN : SC_PORTAL_LOCKED);
            return SR_DECOR * 32 + SC_PORTAL_LOCKED;
        default:       return SR_TERRAIN * 32 + SC_FLOOR;
    }
}

static void draw_player(const Player &p, LONG cx0, LONG cy0, LONG cx1, LONG cy1)
{
    LONG col;
    switch (p.facing) {
        case 0:  col = SC_POSE_UP;    break;
        case 1:  col = SC_POSE_RIGHT; break;
        case 3:  col = SC_POSE_LEFT;  break;
        default: col = SC_POSE_DOWN;  break;
    }
    blit_tile_clip(p.x, p.y, SHEET(SR_PLAYER_POSE, col), cx0, cy0, cx1, cy1);
}

static void draw_enemy(const Enemy &E, LONG cx0, LONG cy0, LONG cx1, LONG cy1)
{
    if (!E.alive) return;
    LONG row = SR_ENEMY_A;
    if (E.colour == 5) row = SR_ENEMY_B;
    else if (E.colour == 6) row = SR_ENEMY_C;
    LONG col = (E.frame >> 2) & 7;
    blit_tile_clip(E.x, E.y, SHEET(row, col), cx0, cy0, cx1, cy1);
}

/* RectFill in pen 0 of the inclusive rectangle, clipped to [cx0,cx1) x [cy0,cy1) */
static void black_clip(LONG x0, LONG y0, LONG x1, LONG y1,
                       LONG cx0, LONG cy0, LONG cx1, LONG cy1)
{
    if (x0 < cx0) x0 = cx0;
    if (y0 < cy0) y0 = cy0;
    if (x1 > cx1 - 1) x1 = cx1 - 1;
    if (y1 > cy1 - 1) y1 = cy1 - 1;
    if (x0 > x1 || y0 > y1) return;
    RectFill(rp, x0, y0, x1, y1);
}
#else
static void draw_tile(LONG col, LONG row, UBYTE kind,
                      LONG runes_remaining, ULONG tick)
{
    LONG x = col * TILE_W, y = row * TILE_H;
    switch (kind) {
        case T_FLOOR:  blit_sheet_tile(x, y, SR_TERRAIN, SC_FLOOR); break;
        case T_WALL:
            blit_sheet_tile(x, y, SR_TERRAIN,
                            ((col + row) & 1) ? SC_WALL_A : SC_WALL_B);
            break;
        case T_RUNE:   blit_sheet_tile(x, y, SR_TERRAIN, SC_RUNE); break;
        case T_OIL:    blit_sheet_tile(x, y, SR_TERRAIN, SC_OIL);  break;
        case T_HAZARD: blit_sheet_tile(x, y, SR_TERRAIN, SC_HAZARD); break;
        case T_PORTAL:
            if (runes_remaining == 0) {
                blit_sheet_tile(x, y, SR_DECOR,
                                ((tick / 6) & 1) ? SC_PORTAL_OPEN : SC_PORTAL_LOCKED);
            } else {
                blit_sheet_tile(x, y, SR_DECOR, SC_PORTAL_LOCKED);
            }
            break;
        default:       blit_sheet_tile(x, y, SR_TERRAIN, SC_FLOOR); break;
    }
}

static void draw_player(const Player &p)
{
    LONG col;
    switch (p.facing) {
        case 0:  col = SC_POSE_UP;    break;
        case 1:  col = SC_POSE_RIGHT; break;
        case 3:  col = SC_POSE_LEFT;  break;
        default: col = SC_POSE_DOWN;  break;
    }
    blit_sheet_tile(p.x, p.y, SR_PLAYER_POSE, col);
}

static void draw_enemy(const Enemy &E)
{
    if (!E.alive) return;
    LONG row = SR_ENEMY_A;
    if (E.colour == 5) row = SR_ENEMY_B;
    else if (E.colour == 6) row = SR_ENEMY_C;
    LONG col = (E.frame >> 2) & 7;
    blit_sheet_tile(E.x, E.y, row, col);
}

/* Paint pen-0 rectangles around the lantern cone so the field beyond
 * the player's light stays black. The cone follows the player's centre
 * and only covers the play area (rows 0..PLAY_H-1) — HUD strip below
 * always renders. */
static void apply_lantern_mask(const GameState &gs)
{
    LONG cx = gs.player.x + 8;
    LONG cy = gs.player.y + 8;

    /* Radius shrinks as the lantern runs low so the endgame gets
     * cramped and tense. Full = LIGHT_RADIUS_PX, empty = 8 px. */
    LONG r = 8 + (LIGHT_RADIUS_PX - 8) * gs.lantern / LANTERN_MAX;
    if (r < 8) r = 8;

    LONG lx = cx - r; if (lx < 0)      lx = 0;
    LONG rx = cx + r; if (rx > PLAY_W) rx = PLAY_W;
    LONG ty = cy - r; if (ty < 0)      ty = 0;
    LONG by = cy + r; if (by > PLAY_H) by = PLAY_H;

    /* Four bands around the light square. */
    SetAPen(rp, 0);
    if (ty > 0)          RectFill(rp, 0,        0,  PLAY_W - 1, ty - 1);
    if (by < PLAY_H)     RectFill(rp, 0,        by, PLAY_W - 1, PLAY_H - 1);
    if (lx > 0)          RectFill(rp, 0,        ty, lx - 1,     by - 1);
    if (rx < PLAY_W)     RectFill(rp, rx,       ty, PLAY_W - 1, by - 1);

    /* Cheap "circle" — chop the 4 corners of the light square with
     * small triangular blocks so it feels round-ish, not blockish. */
    LONG chop = r / 3;
    if (chop > 0) {
        if (ty > 0 && lx > 0)               RectFill(rp, lx, ty, lx + chop - 1, ty + chop - 1);
        if (ty > 0 && rx < PLAY_W)          RectFill(rp, rx - chop, ty, rx - 1, ty + chop - 1);
        if (by < PLAY_H && lx > 0)          RectFill(rp, lx, by - chop, lx + chop - 1, by - 1);
        if (by < PLAY_H && rx < PLAY_W)     RectFill(rp, rx - chop, by - chop, rx - 1, by - 1);
    }
}
#endif

static void draw_hud(const GameState &gs)
{
    fill_rect(0, PLAY_H, SCREEN_W, HUD_H, 15);

    char buf[64];
    sprintf(buf, "SCORE %06ld", (long)gs.score);
    render_text(4, PLAY_H + 12, 1, buf);

    /* Lantern fuel bar (orange -> red as it runs low). */
    LONG bar_w = 120, bar_x = 100;
    fill_rect(bar_x, PLAY_H + 4, bar_w, 8, 8);
    LONG fill = (gs.lantern * bar_w) / LANTERN_MAX;
    if (fill < 0) fill = 0;
    if (fill > bar_w) fill = bar_w;
    UBYTE pen = (gs.lantern < LANTERN_MAX / 4) ? 9 : 10;
    fill_rect(bar_x, PLAY_H + 4, fill, 8, pen);

    sprintf(buf, "LV%ld R%ld/%ld L%ld",
        (long)(gs.level_index + 1),
        (long)(gs.runes_total - gs.runes_remaining),
        (long)gs.runes_total,
        (long)gs.lives);
    render_text(230, PLAY_H + 12, 1, buf);
}

#if defined(__MINT__)
void render_frame(const GameState &gs)
{
    UWORD (*have)[GRID_W] = buffer_cells();
    LONG cx = gs.player.x + 8, cy = gs.player.y + 8;

    /* The light square, as apply_lantern_mask() works it out. */
    LONG r = 8 + (LIGHT_RADIUS_PX - 8) * gs.lantern / LANTERN_MAX;
    if (r < 8) r = 8;
    LONG lx = cx - r; if (lx < 0)      lx = 0;
    LONG rx = cx + r; if (rx > PLAY_W) rx = PLAY_W;
    LONG ty = cy - r; if (ty < 0)      ty = 0;
    LONG by = cy + r; if (by > PLAY_H) by = PLAY_H;

    /* the cells it touches, and their pixel rectangle */
    LONG c0 = lx / TILE_W, c1 = (rx - 1) / TILE_W;
    LONG r0 = ty / TILE_H, r1 = (by - 1) / TILE_H;
    if (c1 < c0) c1 = c0;
    if (r1 < r0) r1 = r0;
    LONG kx0 = c0 * TILE_W, kx1 = (c1 + 1) * TILE_W;
    LONG ky0 = r0 * TILE_H, ky1 = (r1 + 1) * TILE_H;

    for (int i = 0; i < 16; i++) pen_lut[i] = fgfx_pen(i);
    SetAPen(rp, 0);
    for (LONG row = 0; row < GRID_H; row++) {
        for (LONG col = 0; col < GRID_W; col++) {
            if (row >= r0 && row <= r1 && col >= c0 && col <= c1) {
                UWORD t = tile_choice(col, row, gs.grid[row][col],
                                      gs.runes_remaining, gs.tick);
                blit_tile_clip(col * TILE_W, row * TILE_H, SHEET(t >> 5, t & 31),
                               0, 0, PLAY_W, PLAY_H);
                have[row][col] = CELL_DIRTY;    /* sprites + mask go on it */
            } else if (have[row][col] != CELL_BLACK) {
                RectFill(rp, col * TILE_W, row * TILE_H,
                         col * TILE_W + TILE_W - 1, row * TILE_H + TILE_H - 1);
                have[row][col] = CELL_BLACK;
            }
        }
    }

    /* Sprites, then the mask, inside the lit cells only (anything
     * outside them is black on the Amiga too). */
    for (LONG i = 0; i < gs.num_enemies; i++)
        draw_enemy(gs.enemies[i], kx0, ky0, kx1, ky1);
    draw_player(gs.player, kx0, ky0, kx1, ky1);

    SetAPen(rp, 0);
    if (ty > 0)      black_clip(0,  0,  PLAY_W - 1, ty - 1,     kx0, ky0, kx1, ky1);
    if (by < PLAY_H) black_clip(0,  by, PLAY_W - 1, PLAY_H - 1, kx0, ky0, kx1, ky1);
    if (lx > 0)      black_clip(0,  ty, lx - 1,     by - 1,     kx0, ky0, kx1, ky1);
    if (rx < PLAY_W) black_clip(rx, ty, PLAY_W - 1, by - 1,     kx0, ky0, kx1, ky1);

    /* Cheap "circle" — chop the 4 corners of the light square. */
    LONG chop = r / 3;
    if (chop > 0) {
        if (ty > 0 && lx > 0)           black_clip(lx, ty, lx + chop - 1, ty + chop - 1, kx0, ky0, kx1, ky1);
        if (ty > 0 && rx < PLAY_W)      black_clip(rx - chop, ty, rx - 1, ty + chop - 1, kx0, ky0, kx1, ky1);
        if (by < PLAY_H && lx > 0)      black_clip(lx, by - chop, lx + chop - 1, by - 1, kx0, ky0, kx1, ky1);
        if (by < PLAY_H && rx < PLAY_W) black_clip(rx - chop, by - chop, rx - 1, by - 1, kx0, ky0, kx1, ky1);
    }

    draw_hud(gs);
}
#else
void render_frame(const GameState &gs)
{
    /* All tiles first — the lantern mask then blacks out any that
     * fall outside the light cone. */
    for (LONG row = 0; row < GRID_H; row++) {
        for (LONG col = 0; col < GRID_W; col++) {
            draw_tile(col, row, gs.grid[row][col], gs.runes_remaining, gs.tick);
        }
    }
    for (LONG i = 0; i < gs.num_enemies; i++) draw_enemy(gs.enemies[i]);
    draw_player(gs.player);
    apply_lantern_mask(gs);
    draw_hud(gs);
}
#endif

void render_title(const GameState &gs)
{
#if defined(__MINT__)
    invalidate_cells();
#endif
    SetAPen(rp, 0);
    RectFill(rp, 0, 0, SCREEN_W - 1, SCREEN_H - 1);

    fill_rect(0, 40, SCREEN_W, 40, 11);
    render_text(96, 66, 1, "EMBERKEEP");

    render_text(40, 120, 6, "A LANTERN IN THE DEEP DARK");
    render_text(56, 138, 1, "INSPIRED BY 8-BIT CLASSICS");

    render_text(56, 176, 7, "CURSOR KEYS :  MOVE");
    render_text(56, 190, 7, "SPACE       :  BEGIN / RESTART");
    render_text(56, 204, 7, "ESC         :  QUIT");
#if defined(__MINT__)
    render_text(56, 218, 7, "F1          :  DEMO OF LEVEL 1");   /* port addition */
#endif

    if ((gs.tick / 15) & 1) {
        render_text(80, 232, 9, "PRESS  SPACE  TO  BEGIN");
    }
}

void render_endscreen(const GameState &gs)
{
#if defined(__MINT__)
    invalidate_cells();
#endif
    SetAPen(rp, 0);
    RectFill(rp, 0, 0, SCREEN_W - 1, SCREEN_H - 1);

    if (gs.mode == GM_WIN) {
        fill_rect(0, 80, SCREEN_W, 30, 7);
        render_text(80, 100, 0, "THE DARK IS BEATEN");
    } else {
        fill_rect(0, 80, SCREEN_W, 30, 9);
        render_text(112, 100, 0, "GAME OVER");
    }

    char buf[64];
    sprintf(buf, "FINAL SCORE  %06ld", (long)gs.score);
    render_text(88, 156, 1, buf);

    if (gs.state_timer == 0 && ((gs.tick / 15) & 1)) {
        render_text(72, 208, 6, "PRESS SPACE TO RETURN");
    }
}
