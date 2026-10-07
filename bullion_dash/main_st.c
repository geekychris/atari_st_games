// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * Bullion Dash - Atari STE port.
 *
 * Port of the Amiga version (main.c in geekychris/amiga_games, kept as
 * main.c.amiga; the screen code gfx.c as gfx.c.amiga): player.c,
 * enemy.c, level.c, editor.c and sound.c are unmodified; render.c has an
 * ST port block; modplay.c (the game's own music player, which drives
 * Paula directly) skips the CIA reads and is built as C++
 * (modplay_st.cpp) against the Paula emulation on the STE's DMA sound.
 * Levels saved and loaded in the editor go through the AmigaDOS shim.
 *
 * main.c's loop body runs once per 50 Hz VBL (as on the Amiga, one game
 * step and one music tick per 50 Hz frame), the drawing once per frame:
 *   title                  the page on the HUD layer
 *   playing, dying, level  the playfield as scenery (render_playfield
 *   done, game over        once per level, then only changed tiles),
 *                          the status bar and overlays on the HUD layer,
 *                          player and enemies in the back buffer
 *   editor                 editor_draw redraws the whole back buffer
 *
 * Controls: cursor keys or joystick move and climb, Space / Return /
 * fire + left or right digs, fire starts, E on the title opens the level
 * editor, Esc quits.  With Hatari --natfeats on, events and symbol
 * addresses are logged ("BULL ..." lines).
 */
#include <osbind.h>
#include <stdio.h>
#include <string.h>
#include "game.h"
#include "input.h"
#include "level.h"
#include "render.h"
#include "player.h"
#include "enemy.h"
#include "editor.h"
#include "sound.h"
#include "modplay.h"
#include "natfeats.h"
#include "paula.h"

#define FRCLOCK (*(volatile long *)0x466)

void input_exit(void);

/* Global game state - accessible from all modules via extern */
GameState gs;

static int transition_frames = 0;

/* the palette from gfx.c */
static UWORD palette[16] = {
    0x000,  /*  0 BG:        black */
    0xA52,  /*  1 BRICK:     brown */
    0xC73,  /*  2 BRICK_HI:  light brown */
    0x888,  /*  3 SOLID:     gray */
    0xAAA,  /*  4 SOLID_HI:  light gray */
    0x0AA,  /*  5 LADDER:    cyan */
    0x088,  /*  6 BAR:       dark cyan */
    0xEE0,  /*  7 GOLD:      yellow */
    0xFF8,  /*  8 GOLD_HI:   bright yellow */
    0x0D0,  /*  9 PLAYER:    green */
    0x0F4,  /* 10 PLAYER_HI: light green */
    0xD00,  /* 11 ENEMY:     red */
    0xF80,  /* 12 ENEMY_HI:  orange */
    0xFFF,  /* 13 TEXT:      white */
    0x113,  /* 14 HUD_BG:   dark blue */
    0xA52,  /* 15 TRAP:     same as brick */
};

static void log_line(const char *fmt, long a, long b, long c, long d)
{
    char buf[128];
    strcpy(buf, "BULL ");
    snprintf(buf + 5, sizeof(buf) - 6, fmt, a, b, c, d);
    strcat(buf, "\n");
    nf_print(buf);
}

static const char *state_name(int s)
{
    static const char *n[] = { "TITLE", "PLAYING", "DYING", "LEVEL_DONE", "GAMEOVER", "EDITOR" };
    return s >= 0 && s < 6 ? n[s] : "?";
}

/* one 50 Hz step of main.c's loop, without the drawing; 0 to quit */
static int game_step(void)
{
    int dx, dy, dig_left, dig_right;

    input_update();

    /* ESC quits */
    if (input_key(KEY_ESC))
        return 0;

    switch (gs.state) {
    case STATE_TITLE:
        if (input_fire()) {
            /* Start game */
            gs.score = 0;
            gs.lives = 5;
            gs.level_num = 1;
            level_load(&gs, gs.level_num);
            gs.state = STATE_PLAYING;
        } else if (input_key(KEY_E)) {
            editor_init(&gs, 0);
            gs.state = STATE_EDITOR;
        }
        break;

    case STATE_PLAYING:
        dx = input_dx();
        dy = input_dy();

        /* Digging: fire held + direction */
        dig_left = 0;
        dig_right = 0;
        if (input_fire_held()) {
            if (dx < 0) { dig_left = 1; dx = 0; }
            if (dx > 0) { dig_right = 1; dx = 0; }
        }

        /* Update game logic */
        player_update(&gs, dx, dy, dig_left, dig_right);
        enemy_update_all(&gs);
        level_tick_bricks(&gs);

        /* Check player vs enemy collisions using the shared
         * pixel-level helper so we don't duplicate collision
         * logic and get the correct hit-box behavior during
         * mid-tile animation. */
        if (enemy_check_collision(&gs)) {
            gs.state = STATE_DYING;
            gs.player.state = PS_DEAD;
            transition_frames = 30;
        }

        /* Check if all gold collected - reveal hidden ladders */
        if (gs.state == STATE_PLAYING &&
            gs.gold_collected >= gs.gold_total) {
            level_reveal_hidden_ladders(&gs);
        }

        /* Check win: all gold collected AND player at top row */
        if (gs.state == STATE_PLAYING &&
            gs.gold_collected >= gs.gold_total &&
            gs.player.gy == 0) {
            gs.state = STATE_LEVEL_DONE;
            transition_frames = 60;
        }
        break;

    case STATE_DYING:
        /* Death animation: flash player for 30 frames */
        transition_frames--;
        if (transition_frames <= 0) {
            gs.lives--;
            if (gs.lives <= 0) {
                gs.state = STATE_GAMEOVER;
                transition_frames = 30;
            } else {
                /* Reload current level */
                level_load(&gs, gs.level_num);
                gs.state = STATE_PLAYING;
            }
        }
        break;

    case STATE_LEVEL_DONE:
        transition_frames--;
        if (transition_frames <= 0) {
            gs.score += 1000;
            gs.level_num++;
            if (gs.level_num > MAX_LEVELS) gs.level_num = 1;
            level_load(&gs, gs.level_num);
            gs.state = STATE_PLAYING;
        }
        break;

    case STATE_GAMEOVER:
        if (transition_frames > 0) {
            transition_frames--;
        } else if (input_fire()) {
            gs.state = STATE_TITLE;
        }
        break;

    case STATE_EDITOR:
        editor_update(&gs);
        break;
    }
    modplay_tick();
    gs.frame++;
    return 1;
}

/* main.c's rendering for the current state, in layers */
static void game_draw(void)
{
    static int mode = -1;
    static UBYTE shown[GRID_COLS][GRID_ROWS];
    static int shown_level = -1;
    /* 0 title, 1 game, 2 editor */
    int m = gs.state == STATE_TITLE ? 0 : gs.state == STATE_EDITOR ? 2 : 1;
    struct RastPort *hud = gfx_hud();

    if (m != mode) {
        gfx_bg_clear();
        gfx_bg_to_screens();
        shown_level = -1;
        mode = m;
    }
    if (m == 0) {
        render_title(hud, &gs);
        gfx_hud_commit();
        gfx_restore_back();
        return;
    }
    if (m == 2) {
        gfx_hud_commit();		/* (empty) */
        editor_draw(gfx_back_nomark(), &gs);
        return;
    }

    /* the playfield: in full for a new level, else the changed tiles */
    if (shown_level != gs.level_num) {
        render_playfield(gfx_bg(), &gs);
        gfx_bg_to_screens();
        memcpy(shown, gs.tiles, sizeof(shown));
        shown_level = gs.level_num;
    } else if (memcmp(shown, gs.tiles, sizeof(shown))) {
        int x, y;
        for (x = 0; x < GRID_COLS; x++)
            for (y = 0; y < GRID_ROWS; y++)
                if (shown[x][y] != gs.tiles[x][y]) {
                    int sy = PLAYFIELD_Y + y * TILE_H;
                    render_st_tile(gfx_bg(), &gs, x, y);
                    gfx_bg_dirty_rows(sy, sy + TILE_H - 1);
                    shown[x][y] = gs.tiles[x][y];
                }
    }

    render_status(hud, &gs);
    if (gs.state == STATE_LEVEL_DONE)
        render_level_done(hud, &gs);
    else if (gs.state == STATE_GAMEOVER)
        render_gameover(hud, &gs);
    gfx_hud_commit();
    gfx_restore_back();

    /* (main.c: entities hidden in game over, flashing while dying) */
    if (gs.state == STATE_PLAYING || gs.state == STATE_LEVEL_DONE ||
        (gs.state == STATE_DYING && transition_frames % 4 < 2))
        render_entities(gfx_back(), &gs);
}

int main(void)
{
    long old_ssp, last_vbl, frames = 0;
    int running = 1, last_state = -1, last_level = -1;
    long last_score = -1;

    nf_init();
    old_ssp = Super(0L);

    if (!gfx_init(palette, 16)) {
        Super((void *)old_ssp);
        Cconws("Not enough memory\r\n");
        return 1;
    }
    gfx_set_frame_vbls(1);
    input_init(NULL);

    /* Paula emulation on DMA sound, then the game's sound and music */
    paula_set_rate(6258);
    if (paula_init(NULL, 50) != 0)
        log_line("no DMA sound - continuing without sound", 0, 0, 0, 0);
    /* Init sound + music */
    if (sound_init() != 0)
        log_line("sound init failed - continuing without sound", 0, 0, 0, 0);
    modplay_start();

    /* Initialize game state */
    memset(&gs, 0, sizeof(gs));
    gs.lives = 5;
    gs.level_num = 1;
    gs.state = STATE_TITLE;

    log_line("START", 0, 0, 0, 0);
    log_line("SYMBOL gs 0x%06lx", (long)&gs, 0, 0, 0);
    log_line("SYMBOL player 0x%06lx", (long)&gs.player, 0, 0, 0);
    log_line("SYMBOL tiles 0x%06lx", (long)gs.tiles, 0, 0, 0);
    log_line("SYMBOL state 0x%06lx", (long)&gs.state, 0, 0, 0);
    log_line("SYMBOL frame_sync 0x%06lx", (long)gfx_swap, 0, 0, 0);

    last_vbl = FRCLOCK;
    while (running) {
        long now = FRCLOCK;
        int steps = (int)(now - last_vbl);
        last_vbl = now;
        if (steps < 1) steps = 1;
        if (steps > 4) steps = 4;

        while (steps-- > 0 && running)
            running = game_step();

        if (gs.state != last_state || gs.level_num != last_level) {
            log_line("STATE %s level=%ld lives=%ld gold=%ld", (long)state_name(gs.state),
                     (long)gs.level_num, (long)gs.lives, (long)gs.gold_total);
            last_state = gs.state;
            last_level = gs.level_num;
        }
        if (gs.score != last_score) {
            if (gs.state != STATE_TITLE)
                log_line("SCORE %ld gold=%ld/%ld", (long)gs.score, (long)gs.gold_collected,
                         (long)gs.gold_total, 0);
            last_score = gs.score;
        }

        game_draw();
        gfx_swap();
        frames++;
        if (frames % 100 == 0) {
            static long perf_vbl;
            long t = FRCLOCK;
            if (perf_vbl)
                log_line("PERF frames=100 vbls=%ld state=%ld", t - perf_vbl, gs.state, 0, 0);
            perf_vbl = t;
        }
    }

    log_line("EXIT frames=%ld score=%ld", frames, (long)gs.score, 0, 0);
    modplay_stop();
    sound_cleanup();
    paula_exit();
    input_exit();
    gfx_exit();
    Super((void *)old_ssp);
    return 0;
}
