// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * Jump Quest - Level System
 * Level data, tile rendering, collision
 */
#include "game.h"
#include <string.h>
#include "levels/level1.h"
#include "levels/level2.h"
#include "levels/level3.h"

static LevelDef levels[] = {
    { LEVEL1_W, 14, (const UBYTE *)level1_tiles, level1_ents, 0x059C, 0x047A, "Office Park" },
    { LEVEL2_W, 14, (const UBYTE *)level2_tiles, level2_ents, 0x0337, 0x0225, "Server Room" },
    { LEVEL3_W, 14, (const UBYTE *)level3_tiles, level3_ents, 0x0D83, 0x0741, "Rooftop" },
};

#define NUM_LEVELS 3

static int cur_level = 0;
static UBYTE tile_buf[200 * 14]; /* mutable copy of tile data */
#ifdef __MINT__
static int view_dirty = 1;      /* ST port: scenery must be rebuilt */
#endif

void level_load(int level_num) {
    const LevelDef *ld;
    ULONG size;

    if (level_num < 0 || level_num >= NUM_LEVELS)
        level_num = 0;

    cur_level = level_num;
    ld = &levels[cur_level];
    size = (ULONG)ld->width * (ULONG)ld->height;
    if (size > sizeof(tile_buf)) size = sizeof(tile_buf);
    memcpy(tile_buf, ld->tiles, size);
#ifdef __MINT__
    view_dirty = 1;
#endif
}

const LevelDef *level_current(void) {
    return &levels[cur_level];
}

UBYTE level_get_tile(int tx, int ty) {
    const LevelDef *ld = &levels[cur_level];
    if (tx < 0 || tx >= (int)ld->width || ty < 0 || ty >= (int)ld->height)
        return TILE_EMPTY;
#ifdef __MINT__
    /* ST port: 16 bit multiply (an int one is a library call on the 68000,
     * and collision code calls this hundreds of times per frame; GCC turns
     * a plain 16 bit product back into one when inlining) */
    {
        ULONG off = (UWORD)ty;
        __asm__ ("mulu.w %1,%0" : "+d"(off) : "dmi"((UWORD)ld->width));
        return tile_buf[off + tx];
    }
#else
    return tile_buf[ty * ld->width + tx];
#endif
}

void level_set_tile(int tx, int ty, UBYTE tile) {
    const LevelDef *ld = &levels[cur_level];
    if (tx < 0 || tx >= (int)ld->width || ty < 0 || ty >= (int)ld->height)
        return;
    tile_buf[ty * ld->width + tx] = tile;
#ifdef __MINT__
    view_dirty = 1;
#endif
}

BOOL level_is_solid(int tx, int ty) {
    UBYTE t = level_get_tile(tx, ty);
    switch (t) {
    case TILE_GROUND:
    case TILE_GRASS:
    case TILE_BRICK:
    case TILE_QBLOCK:
    case TILE_QBLOCK_HIT:
    case TILE_STONE:
    case TILE_PIPE_TL:
    case TILE_PIPE_TR:
    case TILE_PIPE_BL:
    case TILE_PIPE_BR:
        return TRUE;
    default:
        return FALSE;
    }
}

BOOL level_is_platform(int tx, int ty) {
    return level_get_tile(tx, ty) == TILE_PLATFORM;
}

int level_width_pixels(void) {
    return levels[cur_level].width * TILE_SIZE;
}

#ifdef __MINT__
/*
 * ST port: redrawing 280 tiles from rectangles and lines every frame is
 * far too slow on a 68000.  The camera only moves in whole tiles, so
 * tiles are always word aligned in ST low resolution: each tile type is
 * drawn once (by gfx_draw_tile, unchanged) into a cache of plane words
 * per screen row, the visible tiles are copied into the scenery buffer
 * only when the camera or a tile changes, then into each screen once;
 * sprites drawn on top are undone through dirty rectangles.
 */
#define NUM_TILE_TYPES 16
static UWORD *tile_cache;               /* [type][screen row][4 planes] */
static int view_tx = -1, pf_rows;

static int build_tile_cache(void) {
    struct RastPort rp;
    UWORD *scratch;
    int id, ty, r;

    pf_rows = GFX_Y(HUD_Y);             /* screen rows of the 14 tile rows */
    tile_cache = (UWORD *)AllocMem((ULONG)NUM_TILE_TYPES * pf_rows * 8, MEMF_ANY);
    scratch = (UWORD *)AllocMem(32000, MEMF_CLEAR);
    if (!tile_cache || !scratch) return 0;
    memset(&rp, 0, sizeof(rp));
    rp.base = scratch;
    for (id = 0; id < NUM_TILE_TYPES; id++) {
        for (ty = 0; ty < TILES_Y; ty++)
            gfx_draw_tile(&rp, id, 0, ty * TILE_SIZE);
        for (r = 0; r < pf_rows; r++)
            memcpy(tile_cache + ((long)id * pf_rows + r) * 4, scratch + r * 80, 8);
    }
    FreeMem(scratch, 32000);
    return 1;
}

void level_draw(struct RastPort *rp, int cam_x) {
    int start_tx = cam_x / TILE_SIZE;
    const LevelDef *ld = &levels[cur_level];
    (void)rp;

    if (!tile_cache && !build_tile_cache())
        return;
    if (view_dirty || start_tx != view_tx) {
        UWORD *bg = gfx_bg()->base;
        int tx, ty;
        for (ty = 0; ty < TILES_Y; ty++) {
            int r0 = GFX_Y(ty * TILE_SIZE), r1 = GFX_Y((ty + 1) * TILE_SIZE);
            for (tx = 0; tx < TILES_X; tx++) {
                int t = start_tx + tx < (int)ld->width ? level_get_tile(start_tx + tx, ty) : TILE_EMPTY;
                const ULONG *src = (const ULONG *)(tile_cache + ((long)(t & 15) * pf_rows + r0) * 4);
                ULONG *dst = (ULONG *)(bg + r0 * 80 + tx * 4);
                int r;
                for (r = r0; r < r1; r++, dst += 40) {
                    dst[0] = *src++;
                    dst[1] = *src++;
                }
            }
        }
        gfx_bg_commit_rows(0, HUD_Y - 1);
        gfx_bg_dirty_rows(0, HUD_Y - 1);   /* into each screen once */
        view_tx = start_tx;
        view_dirty = 0;
    }
}
#else
void level_draw(struct RastPort *rp, int cam_x) {
    int start_tx = cam_x / TILE_SIZE;
    int tx, ty, sx, sy;
    int end_tx = start_tx + TILES_X;
    const LevelDef *ld = &levels[cur_level];

    if (end_tx >= (int)ld->width) end_tx = (int)ld->width - 1;

    for (ty = 0; ty < TILES_Y; ty++) {
        sy = ty * TILE_SIZE;
        for (tx = start_tx; tx <= end_tx; tx++) {
            sx = (tx - start_tx) * TILE_SIZE;
            if (sx >= SCREEN_W) break;
            gfx_draw_tile(rp, level_get_tile(tx, ty), sx, sy);
        }
    }
}
#endif
