// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#include "game.h"
#include <string.h>

void WardenGame::init(GameState *state)
{
    gs = state;
    memset(gs, 0, sizeof(*gs));
    gs->mode = GM_TITLE;
}

void WardenGame::load_room()
{
    memset(gs->floor, F_STONE, sizeof(gs->floor));
    memset(gs->objects, 0, sizeof(gs->objects));
    gs->num_objects = 0;
    gs->score = 0;
    gs->jewels_left = 0;

    /* Filmation-style two-wall room: only the back (north wy=0) and
     * the left (west wx=0) walls exist so the "camera" sees the front-
     * right corner open — the classic Knight Lore / Fairlight cutaway
     * view. Player still can't leave the map because blocked() clamps
     * at grid edges. */
    for (LONG i = 0; i < WORLD_W; i++) {
        if (gs->num_objects < MAX_OBJECTS) {
            Prop &o = gs->objects[gs->num_objects++];
            o.kind = O_WALL; o.wx = (UBYTE)i; o.wy = 0;
        }
    }
    for (LONG i = 1; i < WORLD_H; i++) {
        if (gs->num_objects < MAX_OBJECTS) {
            Prop &o = gs->objects[gs->num_objects++];
            o.kind = O_WALL; o.wx = 0; o.wy = (UBYTE)i;
        }
    }
    /* Door marker on the front-right corner as visual exit hint. */
    gs->floor[WORLD_H - 1][WORLD_W - 1] = F_DOOR;

    /* Barrels — pushable. */
    struct { UBYTE wx, wy; } barrel_at[] = { {2,2}, {5,2}, {3,5}, {5,5} };
    for (LONG i = 0; i < 4; i++) {
        if (gs->num_objects < MAX_OBJECTS) {
            Prop &o = gs->objects[gs->num_objects++];
            o.kind = O_BARREL;
            o.wx = barrel_at[i].wx;
            o.wy = barrel_at[i].wy;
        }
    }

    /* Jewels — pickups. */
    struct { UBYTE wx, wy; } jewel_at[] = { {6,3}, {2,6}, {6,6} };
    for (LONG i = 0; i < 3; i++) {
        if (gs->num_objects < MAX_OBJECTS) {
            Prop &o = gs->objects[gs->num_objects++];
            o.kind = O_JEWEL;
            o.wx = jewel_at[i].wx;
            o.wy = jewel_at[i].wy;
            gs->jewels_left++;
        }
    }

    /* Player spawn just inside the doorway. */
    gs->player.wx = 3;
    gs->player.wy = 6;
    gs->player.facing = 0;
    gs->player.anim = 0;
}

Prop *WardenGame::object_at(LONG wx, LONG wy) const
{
    for (LONG i = 0; i < gs->num_objects; i++) {
        Prop &o = gs->objects[i];
        if (o.kind == O_NONE) continue;
        if ((LONG)o.wx == wx && (LONG)o.wy == wy) return &o;
    }
    return NULL;
}

bool WardenGame::blocked(LONG wx, LONG wy) const
{
    if (wx < 0 || wx >= WORLD_W || wy < 0 || wy >= WORLD_H) return true;
    Prop *o = object_at(wx, wy);
    if (!o) return false;
    /* Walls + barrels block; jewels don't. */
    return (o->kind == O_WALL || o->kind == O_BARREL);
}

void WardenGame::pickup_at(LONG wx, LONG wy)
{
    Prop *o = object_at(wx, wy);
    if (o && o->kind == O_JEWEL) {
        o->kind = O_NONE;
        gs->score += 100;
        gs->jewels_left--;
        if (gs->jewels_left <= 0) {
            gs->mode = GM_WIN;
            gs->state_timer = 200;
        }
    }
}

void WardenGame::try_move(LONG dx, LONG dy)
{
    Player &p = gs->player;

    /* Update facing regardless of whether the move succeeds. */
    if      (dx > 0)  p.facing = 1;
    else if (dx < 0)  p.facing = 3;
    else if (dy < 0)  p.facing = 0;
    else if (dy > 0)  p.facing = 2;

    LONG tx = p.wx + dx;
    LONG ty = p.wy + dy;

    /* Target cell blocked by wall = can't move. Blocked by barrel =
     * try to push it into the tile beyond. */
    Prop *o = object_at(tx, ty);
    if (o) {
        if (o->kind == O_WALL) return;
        if (o->kind == O_BARREL) {
            LONG bx = tx + dx, by = ty + dy;
            if (bx < 0 || bx >= WORLD_W || by < 0 || by >= WORLD_H) return;
            if (blocked(bx, by)) return;
            /* Slide the barrel. */
            o->wx = (UBYTE)bx;
            o->wy = (UBYTE)by;
        }
    }

    if (tx < 0 || tx >= WORLD_W || ty < 0 || ty >= WORLD_H) return;
    p.wx = tx;
    p.wy = ty;
    p.anim++;

    /* Auto-pick jewels the player walks over. */
    pickup_at(tx, ty);
}

void WardenGame::tick(UBYTE input)
{
    gs->tick++;

    if (gs->mode == GM_TITLE) {
        if (input & INPUT_START) {
            load_room();
            gs->mode = GM_PLAYING;
        }
        return;
    }
    if (gs->mode == GM_WIN) {
        if (gs->state_timer > 0) gs->state_timer--;
        if ((input & INPUT_START) && gs->state_timer == 0) gs->mode = GM_TITLE;
        return;
    }

    /* GM_PLAYING — tile-locked step per input EDGE, not per held frame.
     * (Edge detection happens in main.cpp; here we just apply the
     * direction bits.) */
    if      (input & INPUT_UP)    try_move( 0, -1);
    else if (input & INPUT_DOWN)  try_move( 0, +1);
    else if (input & INPUT_LEFT)  try_move(-1,  0);
    else if (input & INPUT_RIGHT) try_move(+1,  0);
}
