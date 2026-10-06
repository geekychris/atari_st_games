// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * SKY KNIGHTS - Atari STE port.
 *
 * Port of the Amiga version (main.c in geekychris/amiga_games, kept as
 * main.c.amiga): game.c is unmodified; draw.c has ST port blocks (cached
 * text, the screen split into layers); sound.c pokes Paula and is built
 * as C++ (sound_st.cpp) against the Paula emulation on the STE's DMA
 * sound, loading FLAP.RAW and SMASH.RAW through the AmigaDOS shim.
 *
 * main.c's loop body runs once per 50 Hz VBL (as on the Amiga); the
 * drawing once per frame:
 *   title, game over   the whole page on the HUD layer
 *   wave intro         platforms as scenery, text + HUD on the HUD layer
 *   playing            ground + platforms as scenery, HUD on the HUD
 *                      layer, riders and pods in the back buffer
 *
 * Controls: P1 cursor keys / A,D move, up / W / Space flap; P2 joystick,
 * up or fire flaps; F1 / 1 one player, F2 / 2 two players; Esc quits.
 * With Hatari --natfeats on, events and symbol addresses are logged
 * ("SKYK ..." lines).
 */
#include <osbind.h>
#include <stdio.h>
#include <string.h>
#include "game.h"
#include "draw.h"
#include "input.h"
#include "sound.h"
#include "natfeats.h"
#include "paula.h"

#define FRCLOCK (*(volatile long *)0x466)

void input_init(void);
void input_exit(void);

/* Game state */
static GameState gs;

/* Startup delay to suppress accidental input */
static WORD startup_delay = 30;

/* Color palette: 16 colors */
static UWORD palette[16] = {
    0x000,  /*  0: Black (bg) */
    0xFFF,  /*  1: White (text) */
    0x840,  /*  2: Brown (platforms) */
    0x520,  /*  3: Dark brown (platform shadow) */
    0xFC0,  /*  4: Yellow (P1 mount) */
    0x06F,  /*  5: Blue (P1 knight) */
    0x0CF,  /*  6: Cyan (P2 mount) */
    0xF22,  /*  7: Red (P2 knight/enemies) */
    0xF80,  /*  8: Orange (Swooper) */
    0xAAA,  /*  9: Gray (Raider) */
    0x22A,  /* 10: Dark blue (Wraith) */
    0x0E0,  /* 11: Green (pods) */
    0xFF0,  /* 12: Bright yellow (score) */
    0xCA8,  /* 13: Tan (ground) */
    0xF8F,  /* 14: Pink (egg about to hatch) */
    0xCCC,  /* 15: Light gray */
};

static void log_line(const char *fmt, long a, long b, long c, long d)
{
    char buf[128];
    strcpy(buf, "SKYK ");
    snprintf(buf + 5, sizeof(buf) - 6, fmt, a, b, c, d);
    strcat(buf, "\n");
    nf_print(buf);
}

static const char *state_name(int s)
{
    static const char *n[] = { "TITLE", "PLAYING", "WAVE_INTRO", "DYING", "GAMEOVER" };
    return s >= 0 && s < 5 ? n[s] : "?";
}

/* one 50 Hz step of main.c's loop, without the drawing */
static int game_step(void)
{
    InputState input;
    int running = 1;

    /* Read input */
    memset(&input, 0, sizeof(input));
    input_read(&input);

    /* Startup delay - suppress all input */
    if (startup_delay > 0) {
        startup_delay--;
        input.p1 = 0;
        input.p2 = 0;
        input.sys = 0;
        /* Clear any stale key state */
        input_reset();
    } else {
        /* ESC to quit */
        if (input.sys & INP_ESC) {
            running = 0;
        }
    }

    /* Update game */
    game_update(&gs, &input);

    /* Handle sound events */
    if (gs.ev_flags & EV_FLAP) sound_flap();
    if (gs.ev_flags & EV_KILL) sound_kill();
    if (gs.ev_flags & EV_EGG)  sound_egg();
    if (gs.ev_flags & EV_DIE)  sound_die();
    if (gs.ev_flags & EV_WAVE) sound_wave();
    gs.ev_flags = 0;

    /* Update sound */
    sound_update();
    return running;
}

/* main.c's drawing for the current state, in layers */
static void game_draw(void)
{
    static int mode = -1;
    /* 0 title / game over pages, 1 wave intro, 2 playing (and dying) */
    int m = gs.state == STATE_WAVE_INTRO ? 1 :
            gs.state == STATE_PLAYING || gs.state == STATE_DYING ? 2 : 0;
    struct RastPort *hud = gfx_hud();

    if (m != mode) {
        gfx_bg_clear();
        if (m)
            draw_st_scenery(gfx_bg(), &gs, m == 2);
        gfx_bg_to_screens();
        mode = m;
    }
    switch (gs.state) {
    case STATE_TITLE:
        draw_title(hud, &gs);
        break;
    case STATE_WAVE_INTRO:
        draw_st_wave_text(hud, &gs);
        break;
    case STATE_GAMEOVER:
        draw_gameover(hud, &gs);
        break;
    default:
        draw_st_hud(hud, &gs);
        break;
    }
    gfx_hud_commit();
    gfx_restore_back();
    if (m == 2)
        draw_st_actors(gfx_back(), &gs);
}

int main(void)
{
    long old_ssp, last_vbl, frames = 0;
    int running = 1, last_state = -1, last_wave = -1;
    long last_score = -1;

    nf_init();
    old_ssp = Super(0L);

    if (!gfx_init(palette, 16)) {
        Super((void *)old_ssp);
        Cconws("Not enough memory\r\n");
        return 1;
    }
    gfx_set_frame_vbls(1);
    paula_set_rate(6258);
    if (paula_init(NULL, 50) != 0)
        log_line("no DMA sound - continuing without sound", 0, 0, 0, 0);

    /* Init sound */
    sound_init();

    /* Init game */
    game_init(&gs);
    input_init();

    log_line("START", 0, 0, 0, 0);
    log_line("SYMBOL gs 0x%06lx", (long)&gs, 0, 0, 0);
    log_line("SYMBOL players 0x%06lx", (long)gs.players, 0, 0, 0);
    log_line("SYMBOL enemies 0x%06lx", (long)gs.enemies, 0, 0, 0);
    log_line("SYMBOL state 0x%06lx", (long)&gs.state, 0, 0, 0);
    log_line("SYMBOL score 0x%06lx", (long)gs.score, 0, 0, 0);
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

        if (gs.state != last_state || gs.wave != last_wave) {
            log_line("STATE %s wave=%ld players=%ld lives=%ld", (long)state_name(gs.state),
                     (long)gs.wave, (long)gs.num_players, (long)gs.players[0].lives);
            last_state = gs.state;
            last_wave = gs.wave;
        }
        if (gs.score[0] + gs.score[1] != last_score) {
            if (gs.state != STATE_TITLE)
                log_line("SCORE p1=%ld p2=%ld", (long)gs.score[0], (long)gs.score[1], 0, 0);
            last_score = gs.score[0] + gs.score[1];
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

    log_line("EXIT frames=%ld p1=%ld", frames, (long)gs.score[0], 0, 0);
    sound_cleanup();
    paula_exit();
    input_exit();
    gfx_exit();
    Super((void *)old_ssp);
    return 0;
}
