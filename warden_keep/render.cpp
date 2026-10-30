// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#include "render.h"
#include "game.h"

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
#endif

/*
 * Atari Falcon port: the __MINT__ branches below replace the AGA screen
 * with the Falcon layer (../falcon_port/fgfx: 320x240 16 bit true colour,
 * triple buffered). The drawing code is unchanged; the room is only
 * redrawn into a buffer when it changed since that buffer last showed it
 * (the game is turn-based). See README.md.
 *
 * warden_keep renderer — isometric painter's-algorithm scene draw.
 *
 * Every drawable is projected from world (wx,wy,wz) to screen
 * coordinates and rendered with graphics.library primitives (RectFill
 * + Draw lines). Cubes are drawn as three quads: top diamond, left
 * face parallelogram, right face parallelogram. Sort key for
 * painter's algorithm is (wx+wy) — draw back (low) to front (high).
 *
 * Palette follows the Fairlight "monochrome yellow keep" aesthetic:
 *   0 black background
 *   1 dark yellow      (floor / left face shading)
 *   2 mid yellow       (top face)
 *   3 bright yellow    (right face highlight, jewels, outlines)
 *   4 dim floor stroke
 *   5 door
 *   6 white (HUD text)
 *   7 red   (player accent)
 */

#if defined(__MINT__)
/* Atari Falcon port: fgfx owns the screens (320x240 16 bit true colour,
 * triple buffered); the game keeps its 320x256 coordinates and Y is
 * mapped to the 240 lines. rp is the buffer being drawn this frame. */
static struct RastPort       *rp;
static bool render_scene_reset = true;   /* title / end screen drawn since */
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
#endif

static const UBYTE PALETTE[8][3] = {
    /* 0 */ {   0,   0,   0 },
    /* 1 */ { 128, 112,  32 },
    /* 2 */ { 208, 176,  40 },
    /* 3 */ { 248, 232,  80 },
    /* 4 */ {  56,  48,  16 },
    /* 5 */ {  64,  56, 168 },
    /* 6 */ { 240, 240, 240 },
    /* 7 */ { 240,  72,  56 },
};

#if defined(__MINT__)
static void install_palette(void)
{
    for (LONG i = 0; i < 8; i++)
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
    ULONG buf[2 + 8 * 3];
    buf[0] = (8UL << 16) | 0;
    for (LONG i = 0; i < 8; i++) {
        buf[1 + i * 3 + 0] = ((ULONG)PALETTE[i][0]) * 0x01010101ul;
        buf[1 + i * 3 + 1] = ((ULONG)PALETTE[i][1]) * 0x01010101ul;
        buf[1 + i * 3 + 2] = ((ULONG)PALETTE[i][2]) * 0x01010101ul;
    }
    buf[1 + 8 * 3] = 0;
    LoadRGB32(&scr->ViewPort, (ULONG *)buf);
}

LONG render_open(void)
{
    scr = OpenScreenTags(NULL,
        SA_Width,     SCREEN_W,
        SA_Height,    SCREEN_H,
        SA_Depth,     3,
        SA_Type,      CUSTOMSCREEN,
        SA_DisplayID, 0x00000L,
        SA_Title,     (ULONG)"warden_keep",
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

void render_text(LONG x, LONG y, UBYTE pen, const char *text)
{
    SetAPen(rp, pen);
    Move(rp, x, y);
    Text(rp, (STRPTR)text, (LONG)strlen(text));
}

/* World -> screen isometric projection. wz is world height (0 = floor). */
static inline void iso_project(LONG wx, LONG wy, LONG wz, LONG *sx, LONG *sy)
{
    *sx = (wx - wy) * (ISO_TILE_W / 2) + ORG_X;
    *sy = (wx + wy) * (ISO_TILE_H / 2) - wz * ISO_CUBE_H + ORG_Y;
}

#if defined(__MINT__)
/* Falcon: the one-pixel-wide vertical RectFills of the cube faces as a
 * direct column of words (same pixels: rows ymap[y0]..ymap[y1]); with a
 * RectFill call per pixel row (and per diamond row, below) a room redraw
 * took ~250 ms. */
static void vline(LONG x, LONG y0, LONG y1)
{
    if (x < 0 || x >= SCREEN_W || y0 > y1) return;
    int h = fgfx_height();
    int py0 = fgfx_map_y((int)y0), py1 = fgfx_map_y((int)y1);
    if (py0 < 0) py0 = 0;
    if (py1 >= h) py1 = h - 1;
    UWORD c = fgfx_pen(rp->apen);
    UWORD *p = rp->base + (ULONG)py0 * SCREEN_W + x;
    for (int py = py0; py <= py1; py++, p += SCREEN_W) *p = c;
}

/* the diamonds' one-row RectFills, the same way */
static void hline(LONG x0, LONG x1, LONG y)
{
    if (y < 0 || y >= SCREEN_H) return;
    if (x0 < 0) x0 = 0;
    if (x1 >= SCREEN_W) x1 = SCREEN_W - 1;
    if (x0 > x1) return;
    int py = fgfx_map_y((int)y);
    if (py >= fgfx_height()) return;
    UWORD c = fgfx_pen(rp->apen);
    UWORD *p = rp->base + (ULONG)py * SCREEN_W + x0;
    for (LONG n = x1 - x0; n >= 0; n--) *p++ = c;
}
#endif

/* Fill a horizontal-scanline "quad" — no arbitrary polygon fill in
 * plain graphics.library, so we approximate the diamond / parallelogram
 * shapes with a series of RectFill scanlines. Fast enough for a
 * pixel-count budget of a few hundred quads. */
static void fill_top_diamond(LONG cx, LONG cy, UBYTE pen)
{
    /* A tile top-face diamond of full width ISO_TILE_W and height
     * ISO_TILE_H, centred on (cx, cy). Draw as pairs of scanlines
     * expanding from centre outward. */
    SetAPen(rp, pen);
    LONG half_w = ISO_TILE_W / 2;
    LONG half_h = ISO_TILE_H / 2;
    for (LONG i = 0; i < half_h; i++) {
        LONG w = ((i + 1) * half_w) / half_h;
#if defined(__MINT__)
        hline(cx - w, cx + w - 1, cy - i);
        hline(cx - w, cx + w - 1, cy + i);
#else
        RectFill(rp, cx - w, cy - i, cx + w - 1, cy - i);
        RectFill(rp, cx - w, cy + i, cx + w - 1, cy + i);
#endif
    }
}

/* Left face — the SW-facing parallelogram of a cube whose top-face
 * centre is at (cx, cy_top_center). Sits below the top diamond,
 * slanting down-right along the west-to-south edge.
 *
 * Top edge (from top diamond's W corner to S corner):
 *   (cx - half_w, cy_top_center) -> (cx, cy_top_center + half_h)
 * Bottom edge (same, shifted down by ISO_CUBE_H * stack_h):
 *   (cx - half_w, cy_top_center + face_h) -> (cx, cy_top_center + half_h + face_h)
 *
 * Rendered as one-pixel-wide vertical RectFills per column so the
 * parallelogram is completely solid — the earlier WritePixel-with-mask
 * approach was leaving gaps. */
static void fill_left_face(LONG cx, LONG cy_top_center, LONG face_h, UBYTE pen)
{
    SetAPen(rp, pen);
    LONG half_w = ISO_TILE_W / 2;
    LONG half_h = ISO_TILE_H / 2;
    for (LONG col = 0; col < half_w; col++) {
        LONG x  = cx - half_w + col;
        LONG y0 = cy_top_center + (col * half_h) / half_w;
        LONG y1 = y0 + face_h - 1;
#if defined(__MINT__)
        vline(x, y0, y1);
#else
        RectFill(rp, x, y0, x, y1);
#endif
    }
}

/* Right face — the SE-facing parallelogram, mirror of left. */
static void fill_right_face(LONG cx, LONG cy_top_center, LONG face_h, UBYTE pen)
{
    SetAPen(rp, pen);
    LONG half_w = ISO_TILE_W / 2;
    LONG half_h = ISO_TILE_H / 2;
    for (LONG col = 0; col < half_w; col++) {
        LONG x  = cx + col;
        /* Right face's top edge starts at S corner and rises to E
         * corner as we move right, so y decreases with col. */
        LONG y0 = cy_top_center + half_h - (col * half_h) / half_w;
        LONG y1 = y0 + face_h - 1;
#if defined(__MINT__)
        vline(x, y0, y1);
#else
        RectFill(rp, x, y0, x, y1);
#endif
    }
}

/* Outline a cube's silhouette in a dark pen so faces read as edges,
 * not blobs. Draws the 6 visible edges of a wireframe cube seen from
 * the front-right. */
static void outline_cube(LONG cx, LONG cy_top_center, LONG face_h, UBYTE pen)
{
    SetAPen(rp, pen);
    LONG half_w = ISO_TILE_W / 2;
    LONG half_h = ISO_TILE_H / 2;
    /* Top diamond edges. */
    Move(rp, cx - half_w, cy_top_center);         Draw(rp, cx, cy_top_center - half_h);
    Move(rp, cx, cy_top_center - half_h);         Draw(rp, cx + half_w, cy_top_center);
    Move(rp, cx + half_w, cy_top_center);         Draw(rp, cx, cy_top_center + half_h);
    Move(rp, cx, cy_top_center + half_h);         Draw(rp, cx - half_w, cy_top_center);
    /* Three vertical drops from the visible top-front corners. */
    Move(rp, cx - half_w, cy_top_center);         Draw(rp, cx - half_w, cy_top_center + face_h);
    Move(rp, cx, cy_top_center + half_h);         Draw(rp, cx, cy_top_center + half_h + face_h);
    Move(rp, cx + half_w, cy_top_center);         Draw(rp, cx + half_w, cy_top_center + face_h);
    /* Bottom-front edges of the two visible faces. */
    Move(rp, cx - half_w, cy_top_center + face_h);Draw(rp, cx, cy_top_center + half_h + face_h);
    Move(rp, cx, cy_top_center + half_h + face_h);Draw(rp, cx + half_w, cy_top_center + face_h);
}

/* Draw a stacked cube (stack = height in cube units) at world
 * (wx, wy, wz=0). Faces + top + outline. */
static void draw_stacked_cube(LONG wx, LONG wy, LONG stack,
                              UBYTE pen_top, UBYTE pen_left, UBYTE pen_right,
                              UBYTE pen_outline)
{
    LONG sx, sy;
    iso_project(wx, wy, stack, &sx, &sy);   /* top face centre */
    LONG face_h = stack * ISO_CUBE_H;
    fill_left_face(sx, sy, face_h, pen_left);
    fill_right_face(sx, sy, face_h, pen_right);
    fill_top_diamond(sx, sy, pen_top);
    outline_cube(sx, sy, face_h, pen_outline);
}

/* Draw a single unit cube. */
static void draw_cube(LONG wx, LONG wy,
                      UBYTE pen_top, UBYTE pen_left, UBYTE pen_right,
                      UBYTE pen_outline)
{
    draw_stacked_cube(wx, wy, 1, pen_top, pen_left, pen_right, pen_outline);
}

/* Draw a floor tile at wz=0 (flat diamond only). */
static void draw_floor_tile(LONG wx, LONG wy, UBYTE kind)
{
    LONG sx, sy;
    iso_project(wx, wy, 0, &sx, &sy);
    UBYTE pen = (kind == F_DOOR) ? 5 : 1;
    fill_top_diamond(sx, sy, pen);
    /* Faint darker outline for grout. */
    SetAPen(rp, 4);
    LONG half_w = ISO_TILE_W / 2;
    LONG half_h = ISO_TILE_H / 2;
    /* Top-left, top-right, bottom-left, bottom-right edges of the diamond. */
    Move(rp, sx - half_w, sy);       Draw(rp, sx, sy - half_h);
    Move(rp, sx, sy - half_h);       Draw(rp, sx + half_w, sy);
    Move(rp, sx + half_w, sy);       Draw(rp, sx, sy + half_h);
    Move(rp, sx, sy + half_h);       Draw(rp, sx - half_w, sy);
}

/* Draw a small "player" — a red pillar so they're immediately visible
 * against the yellow walls / floor. Facing indicator = a bright white
 * pixel on top diamond in the facing direction. */
static void draw_player(const Player &p)
{
    /* Two-cube tall pillar, red body with lit right shoulder for
     * strong 3D reading. */
    draw_stacked_cube(p.wx, p.wy, 2, /*top*/6, /*left*/7, /*right*/3, /*outline*/4);
    LONG sx, sy;
    iso_project(p.wx, p.wy, 2, &sx, &sy);
    SetAPen(rp, 0);
    switch (p.facing) {
        case 0: RectFill(rp, sx - 3, sy - 6, sx + 2, sy - 4); break;
        case 1: RectFill(rp, sx + 3, sy - 2, sx + 7, sy + 1); break;
        case 2: RectFill(rp, sx - 3, sy + 3, sx + 2, sy + 5); break;
        case 3: RectFill(rp, sx - 7, sy - 2, sx - 3, sy + 1); break;
    }
}

static void draw_jewel(const Prop &o)
{
    LONG sx, sy;
    iso_project(o.wx, o.wy, 0, &sx, &sy);
    /* Small bright rhombus above the floor. */
    SetAPen(rp, 3);
    RectFill(rp, sx - 2, sy - 6, sx + 1, sy - 3);
    RectFill(rp, sx - 3, sy - 5, sx + 2, sy - 4);
    SetAPen(rp, 6);
    WritePixel(rp, sx, sy - 6);
}

/* Sort a small array of object indices by (wx+wy) ascending — plain
 * insertion sort, enough for <=MAX_OBJECTS entries. */
static void sort_objects(const GameState &gs, LONG *idx, LONG n)
{
    for (LONG i = 1; i < n; i++) {
        LONG cur = idx[i];
        LONG ck  = (LONG)gs.objects[cur].wx + (LONG)gs.objects[cur].wy;
        LONG j = i;
        while (j > 0) {
            LONG pk = (LONG)gs.objects[idx[j-1]].wx + (LONG)gs.objects[idx[j-1]].wy;
            if (pk <= ck) break;
            idx[j] = idx[j-1];
            j--;
        }
        idx[j] = cur;
    }
}

static void draw_hud(const GameState &gs)
{
    SetAPen(rp, 4);
    RectFill(rp, 0, PLAY_H, SCREEN_W - 1, SCREEN_H - 1);
    char buf[64];
    sprintf(buf, "SCORE %06ld", (long)gs.score);
    render_text(4, PLAY_H + 12, 6, buf);
    sprintf(buf, "JEWELS %ld", (long)gs.jewels_left);
    render_text(230, PLAY_H + 12, 6, buf);
}

void render_frame(const GameState &gs)
{
#if defined(__MINT__)
    /* Falcon: skip the redraw when this buffer already shows this room. */
    struct SceneKey {
        LONG   score, jewels_left, num_objects;
        Player player;
        Prop   objects[MAX_OBJECTS];
        UBYTE  floor[WORLD_H][WORLD_W];
    };
    static UWORD   *key_base[3];
    static SceneKey key[3];
    static bool     key_valid[3];
    SceneKey want;
    memset(&want, 0, sizeof(want));
    want.score = gs.score;
    want.jewels_left = gs.jewels_left;
    want.num_objects = gs.num_objects;
    want.player = gs.player;
    memcpy(want.objects, gs.objects, sizeof(want.objects));
    memcpy(want.floor, gs.floor, sizeof(want.floor));
    int b = 0;
    while (b < 3 && key_base[b] && key_base[b] != rp->base) b++;
    if (b == 3) b = 0;
    if (render_scene_reset) {
        key_valid[0] = key_valid[1] = key_valid[2] = false;
        render_scene_reset = false;
    }
    if (key_base[b] == rp->base && key_valid[b] &&
        memcmp(&want, &key[b], sizeof(want)) == 0)
        return;
    key_base[b] = rp->base;
    key[b] = want;
    key_valid[b] = true;
    /* another buffer already shows this room: copy it (a redraw takes
     * ~70 ms on a 16 MHz Falcon, a copy of the screen a fraction) */
    for (int o = 0; o < 3; o++)
        if (o != b && key_base[o] && key_valid[o] &&
            memcmp(&want, &key[o], sizeof(want)) == 0) {
            memcpy(rp->base, key_base[o], (size_t)fgfx_height() * SCREEN_W * 2);
            return;
        }
#endif
    /* Clear play area to bg. */
    SetAPen(rp, 0);
    RectFill(rp, 0, 0, SCREEN_W - 1, PLAY_H - 1);

    /* 1. Floor tiles — always at wz=0, drawn back-to-front by row+col
     *    so overlaps are visually consistent. */
    for (LONG wy = 0; wy < WORLD_H; wy++)
        for (LONG wx = 0; wx < WORLD_W; wx++)
            draw_floor_tile(wx, wy, gs.floor[wy][wx]);

    /* 2. Objects + player — combined painter-sorted pass. Player slot
     *    is index gs.num_objects (sentinel). */
    LONG idx[MAX_OBJECTS + 1];
    LONG n = 0;
    for (LONG i = 0; i < gs.num_objects; i++) {
        if (gs.objects[i].kind == O_NONE) continue;
        idx[n++] = i;
    }
    /* Sort objects by (wx+wy). */
    sort_objects(gs, idx, n);

    /* Merge the player into the draw pass by its own (wx+wy) key. */
    LONG pk = gs.player.wx + gs.player.wy;
    LONG drew_player = 0;
    for (LONG i = 0; i < n; i++) {
        LONG ok = (LONG)gs.objects[idx[i]].wx + (LONG)gs.objects[idx[i]].wy;
        if (!drew_player && pk < ok) {
            draw_player(gs.player);
            drew_player = 1;
        }
        const Prop &o = gs.objects[idx[i]];
        switch (o.kind) {
            case O_WALL:
                draw_stacked_cube(o.wx, o.wy, 3, /*top*/2, /*left*/1, /*right*/3, /*outline*/4);
                break;
            case O_BARREL:
                draw_cube(o.wx, o.wy, /*top*/3, /*left*/2, /*right*/1, /*outline*/4);
                break;
            case O_JEWEL:
                draw_jewel(o);
                break;
            default: break;
        }
    }
    if (!drew_player) draw_player(gs.player);

    draw_hud(gs);
}

void render_title(const GameState &gs)
{
#if defined(__MINT__)
    render_scene_reset = true;
#endif
    SetAPen(rp, 0);
    RectFill(rp, 0, 0, SCREEN_W - 1, SCREEN_H - 1);

    SetAPen(rp, 2);
    RectFill(rp, 0, 40, SCREEN_W - 1, 79);
    render_text(96, 66, 0, "WARDEN  KEEP");

    render_text(48, 120, 3, "AN ISOMETRIC KEEP TO PLUNDER");
    render_text(56, 138, 6, "PUSH BARRELS, GRAB JEWELS");

    render_text(56, 176, 3, "CURSOR KEYS :  MOVE");
    render_text(56, 190, 3, "SPACE       :  BEGIN");
    render_text(56, 204, 3, "ESC         :  QUIT");

    if ((gs.tick / 15) & 1)
        render_text(80, 232, 7, "PRESS  SPACE  TO  BEGIN");
}

void render_endscreen(const GameState &gs)
{
#if defined(__MINT__)
    render_scene_reset = true;
#endif
    SetAPen(rp, 0);
    RectFill(rp, 0, 0, SCREEN_W - 1, SCREEN_H - 1);
    SetAPen(rp, 3);
    RectFill(rp, 0, 80, SCREEN_W - 1, 109);
    render_text(64, 100, 0, "EVERY JEWEL CLAIMED");

    char buf[64];
    sprintf(buf, "FINAL SCORE  %06ld", (long)gs.score);
    render_text(88, 156, 6, buf);

    if (gs.state_timer == 0 && ((gs.tick / 15) & 1))
        render_text(72, 208, 2, "PRESS SPACE TO RETURN");
}
