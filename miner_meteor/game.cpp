// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#include "game.h"
#include "levels.h"

#include <string.h>

/*
 * Game rules — no rendering, no I/O, no bridge chatter. Everything
 * mutates *gs. main.cpp is responsible for calling tick(input) once
 * per game-frame and reading back state for rendering.
 */

void MinerGame::init(GameState *state)
{
    gs = state;
    memset(gs, 0, sizeof(*gs));
    gs->mode = GM_TITLE;
    gs->lives = 3;
    gs->level_index = 0;
    gs->air = AIR_MAX;
}

/* Decode ASCII cavern layout into (grid + player spawn + guardians). */
void MinerGame::load_level(LONG index)
{
    if (index < 0 || index >= NUM_LEVELS) index = 0;
    const LevelSpec &L = LEVELS[index];

    memset(gs->grid, 0, sizeof(gs->grid));
    memset(gs->guards, 0, sizeof(gs->guards));
    gs->num_guards = 0;
    gs->keys_total = 0;
    gs->keys_remaining = 0;

    /* Tiles + spawn. */
    for (LONG row = 0; row < GRID_H; row++) {
        for (LONG col = 0; col < GRID_W; col++) {
            char c = L.tiles[row * GRID_W + col];
            UBYTE k = T_EMPTY;
            switch (c) {
                case '#': k = T_SOLID;    break;
                case 'K': k = T_KEY;      gs->keys_total++; gs->keys_remaining++; break;
                case 'E': k = T_EXIT;     break;
                case 'c': k = T_COLLAPSE; break;
                case '<': k = T_CONV_L;   break;
                case '>': k = T_CONV_R;   break;
                case '^': k = T_HAZARD;   break;
                case 'P':
                    /* Rendered as empty; capture as player spawn. */
                    gs->spawn_x = col * TILE_W;
                    gs->spawn_y = row * TILE_H;
                    k = T_EMPTY;
                    break;
                default: k = T_EMPTY;     break;
            }
            gs->grid[row][col].kind = k;
            gs->grid[row][col].timer = 0;
        }
    }

    /* Guardians. */
    const GuardianSpec *g = L.guards;
    while (g && g->axis != 0xFF && gs->num_guards < MAX_GUARDIANS) {
        Guardian &G = gs->guards[gs->num_guards++];
        G.axis     = g->axis;
        G.colour   = g->colour;
        G.dx       = g->dx;
        G.dy       = g->dy;
        G.x        = g->x;
        G.y        = g->y;
        G.min_pos  = g->min_pos;
        G.max_pos  = g->max_pos;
        G.alive    = 1;
        G.frame    = 0;
        g++;
    }

    gs->air = AIR_MAX;
    spawn_player();
}

void MinerGame::restart_current()
{
    load_level(gs->level_index);
}

void MinerGame::spawn_player()
{
    Player &p = gs->player;
    p.x = gs->spawn_x;
    p.y = gs->spawn_y;
    p.vx = 0;
    p.vy = 0;
    p.facing = 1;
    p.on_ground = 0;
    p.frame = 0;
    p.jump_lock = 1;    /* release jump before it counts */
}

UBYTE MinerGame::tile_at_pixel(LONG px, LONG py) const
{
    if (px < 0 || py < 0 || px >= PLAY_W || py >= PLAY_H) return T_SOLID;
    LONG col = px / TILE_W;
    LONG row = py / TILE_H;
    return gs->grid[row][col].kind;
}

bool MinerGame::solid_at(LONG px, LONG py) const
{
    UBYTE k = tile_at_pixel(px, py);
    return (k == T_SOLID || k == T_COLLAPSE || k == T_CONV_L || k == T_CONV_R);
}

/* AABB overlap between player (16x16) and any live guardian (12x12
 * hitbox — a touch smaller so grazing isn't instant death). */
bool MinerGame::player_touching_guardian() const
{
    LONG px1 = gs->player.x, py1 = gs->player.y;
    LONG px2 = px1 + 15,     py2 = py1 + 15;
    for (LONG i = 0; i < gs->num_guards; i++) {
        const Guardian &G = gs->guards[i];
        if (!G.alive) continue;
        LONG gx1 = G.x + 2, gy1 = G.y + 2;
        LONG gx2 = G.x + 13, gy2 = G.y + 13;
        if (px1 <= gx2 && px2 >= gx1 && py1 <= gy2 && py2 >= gy1) return true;
    }
    return false;
}

void MinerGame::kill_player()
{
    if (gs->lives > 0) gs->lives--;
    if (gs->lives <= 0) {
        gs->mode = GM_LOSE;
        gs->state_timer = 150;   /* ~6s at 25 fps */
    } else {
        restart_current();
    }
}

/* Horizontal + vertical player integration with tile collision.
 * Standard "resolve X then Y" AABB against the four corners.
 * Note: the tile grid is coarse enough (16 px cells) that stepping
 * WALK_SPEED = 2 px never skips a tile edge — no swept collision needed. */
void MinerGame::update_player(UBYTE input)
{
    Player &p = gs->player;

    /* Input -> intended velocity. */
    p.vx = 0;
    if (input & INPUT_LEFT)  { p.vx = -WALK_SPEED; p.facing = 0; }
    if (input & INPUT_RIGHT) { p.vx = +WALK_SPEED; p.facing = 1; }

    /* Conveyor override — only applies while grounded on a conveyor
     * tile directly beneath the player's centreline. */
    if (p.on_ground) {
        UBYTE below = tile_at_pixel(p.x + 8, p.y + 16);
        if (below == T_CONV_L && p.vx > -WALK_SPEED) p.vx -= CONVEYOR_PUSH;
        if (below == T_CONV_R && p.vx < +WALK_SPEED) p.vx += CONVEYOR_PUSH;
    }

    /* Jump. Only if on ground AND jump wasn't already held from a
     * previous frame (edge trigger). This avoids the "spawn with jump
     * held, auto-launch on frame 0" foot-gun. */
    if (!(input & INPUT_JUMP)) p.jump_lock = 0;
    if ((input & INPUT_JUMP) && !p.jump_lock && p.on_ground) {
        p.vy = JUMP_IMPULSE;
        p.on_ground = 0;
        p.jump_lock = 1;
    }

    /* --- X axis integration --- */
    LONG new_x = p.x + p.vx;
    if (p.vx > 0) {
        /* Moving right — check right edge at head+foot corners. */
        if (solid_at(new_x + 15, p.y + 1) || solid_at(new_x + 15, p.y + 15)) {
            new_x = ((p.x / TILE_W) + 1) * TILE_W - 16;
        }
    } else if (p.vx < 0) {
        if (solid_at(new_x, p.y + 1) || solid_at(new_x, p.y + 15)) {
            new_x = ((p.x / TILE_W)) * TILE_W;
        }
    }
    p.x = new_x;

    /* --- Y axis integration --- */
    p.vy += GRAVITY;
    if (p.vy > MAX_FALL_SPEED) p.vy = MAX_FALL_SPEED;
    LONG new_y = p.y + p.vy;
    p.on_ground = 0;
    if (p.vy > 0) {
        /* Falling — check floor at both feet corners. */
        if (solid_at(p.x + 1, new_y + 15) || solid_at(p.x + 14, new_y + 15)) {
            new_y = ((p.y / TILE_H) + 1) * TILE_H - 16;
            p.vy = 0;
            p.on_ground = 1;
        }
    } else if (p.vy < 0) {
        if (solid_at(p.x + 1, new_y) || solid_at(p.x + 14, new_y)) {
            new_y = ((p.y / TILE_H)) * TILE_H;
            p.vy = 1;   /* nudge downward so we don't stick */
        }
    }
    p.y = new_y;

    /* Animation frame — every 6 ticks, when actually moving. */
    if (p.vx != 0 && (gs->tick % 6) == 0) p.frame = (p.frame + 1) & 3;

    /* Collectibles + hazard tiles at the player's centre. */
    LONG cx = p.x + 8, cy = p.y + 8;
    LONG col = cx / TILE_W, row = cy / TILE_H;
    if (col >= 0 && col < GRID_W && row >= 0 && row < GRID_H) {
        TileState &t = gs->grid[row][col];
        if (t.kind == T_KEY) {
            t.kind = T_EMPTY;
            gs->keys_remaining--;
            gs->score += KEY_SCORE;
        } else if (t.kind == T_HAZARD) {
            kill_player();
            return;
        } else if (t.kind == T_EXIT && gs->keys_remaining == 0) {
            /* Cavern complete. */
            gs->score += LEVEL_SCORE + (gs->air / 2);
            gs->level_index++;
            if (gs->level_index >= NUM_LEVELS) {
                gs->mode = GM_WIN;
                gs->state_timer = 200;
            } else {
                gs->mode = GM_LEVELWIN;
                gs->state_timer = 60;
            }
            return;
        }
    }

    /* Start the collapse timer for a collapsing tile directly under the
     * player's feet. Timer counts down only while the player is on it. */
    if (p.on_ground) {
        LONG below_col = (p.x + 8) / TILE_W;
        LONG below_row = ((p.y + 16) / TILE_H);
        if (below_col >= 0 && below_col < GRID_W && below_row >= 0 && below_row < GRID_H) {
            TileState &b = gs->grid[below_row][below_col];
            if (b.kind == T_COLLAPSE) {
                if (b.timer == 0) b.timer = COLLAPSE_FRAMES;
            }
        }
    }

    /* Fall-out-of-world sanity. */
    if (p.y >= PLAY_H) {
        kill_player();
        return;
    }
}

void MinerGame::update_guardians()
{
    for (LONG i = 0; i < gs->num_guards; i++) {
        Guardian &G = gs->guards[i];
        if (!G.alive) continue;
        if (G.axis == 0) {
            G.x += G.dx;
            if (G.x <= (LONG)G.min_pos) { G.x = G.min_pos; G.dx = -G.dx; }
            if (G.x >= (LONG)G.max_pos) { G.x = G.max_pos; G.dx = -G.dx; }
        } else {
            G.y += G.dy;
            if (G.y <= (LONG)G.min_pos) { G.y = G.min_pos; G.dy = -G.dy; }
            if (G.y >= (LONG)G.max_pos) { G.y = G.max_pos; G.dy = -G.dy; }
        }
        G.frame++;
    }
}

void MinerGame::update_collapse()
{
    /* Any collapsing tile with a nonzero timer counts down every frame
     * — only started once the player touched it (see update_player). */
    for (LONG row = 0; row < GRID_H; row++) {
        for (LONG col = 0; col < GRID_W; col++) {
            TileState &t = gs->grid[row][col];
            if (t.kind == T_COLLAPSE && t.timer > 0) {
                t.timer--;
                if (t.timer == 0) t.kind = T_EMPTY;
            }
        }
    }
}

void MinerGame::drain_air()
{
    if ((gs->tick % AIR_DRAIN_FRAMES) == 0 && gs->air > 0) gs->air--;
    if (gs->air == 0) kill_player();
}

void MinerGame::tick(UBYTE input)
{
    gs->tick++;

    if (gs->mode == GM_TITLE) {
        if (input & INPUT_START) {
            gs->level_index = 0;
            gs->lives = 3;
            gs->score = 0;
            load_level(0);
            gs->mode = GM_PLAYING;
        }
        return;
    }

    if (gs->mode == GM_WIN || gs->mode == GM_LOSE) {
        if (gs->state_timer > 0) gs->state_timer--;
        if ((input & INPUT_START) && gs->state_timer == 0) {
            gs->mode = GM_TITLE;
        }
        return;
    }

    if (gs->mode == GM_LEVELWIN) {
        if (gs->state_timer > 0) { gs->state_timer--; return; }
        load_level(gs->level_index);
        gs->mode = GM_PLAYING;
        return;
    }

    /* GM_PLAYING */
    update_player(input);
    if (gs->mode != GM_PLAYING) return;    /* level ended mid-tick */
    update_guardians();
    update_collapse();
    drain_air();
    if (gs->mode != GM_PLAYING) return;
    if (player_touching_guardian()) kill_player();
}
