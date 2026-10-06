/*
 * DOT CHASE - Atari STE port.
 *
 * Port of the Amiga version (main.c in geekychris/amiga_games, kept as
 * main.c.amiga): game.c is unmodified and runs on the shared ST layer
 * in ../st_port.  draw.c draws the maze once per buffer and then only
 * restores the tiles under the moving sprites, so it draws straight into
 * the back buffer; its ST port blocks restore those tiles from a copy of
 * the maze and cache strings.  sound.c pokes Paula; it is built as C++
 * against the Paula emulation (STE DMA sound).
 *
 * The main loop is main.c's, with game_update and the sound handling
 * run once per 50 Hz VBL (as on the Amiga) and drawing once per frame.
 *
 * Controls: cursor keys / WASD / joystick; Space / Return / fire to
 * start; Esc quits.  With Hatari --natfeats on, events and symbol
 * addresses are logged ("DOTS ..." lines).
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

static GameState gs;
static WORD startup_delay = 30;

static UWORD palette[16] = {
    0x000,  /*  0: Black (bg) */
    0xFF0,  /*  1: Yellow (dot-eater) */
    0xF00,  /*  2: Red (Blinky) */
    0xFBD,  /*  3: Pink (Pinky) */
    0x0FF,  /*  4: Cyan (Inky) */
    0xF80,  /*  5: Orange (Clyde) */
    0x22F,  /*  6: Blue (walls/frightened ghost) */
    0xFFF,  /*  7: White (text) */
    0x118,  /*  8: Dark blue (maze outline) */
    0xFDA,  /*  9: Peach (ghost face) */
    0xF9B,  /* 10: Dark pink */
    0xA00,  /* 11: Dark red */
    0x0F0,  /* 12: Green */
    0xA0F,  /* 13: Purple */
    0x888,  /* 14: Gray */
    0xEED,  /* 15: Cream (dots) */
};

static void log_line(const char *fmt, long a, long b, long c, long d)
{
    char buf[128];
    strcpy(buf, "DOTS ");
    snprintf(buf + 5, sizeof(buf) - 6, fmt, a, b, c, d);
    strcat(buf, "\n");
    nf_print(buf);
}

static const char *state_name(int s)
{
    static const char *n[] = { "TITLE", "READY", "PLAYING", "DYING", "GAMEOVER",
                               "LEVEL_DONE", "EAT_GHOST" };
    return s >= 0 && s < 7 ? n[s] : "?";
}

/* one 50 Hz step of main.c's loop body, without the drawing */
static int game_step(void)
{
    InputState input;
    WORD prev_state = gs.state;
    WORD prev_level = gs.level;
    int running = 1;

    /* Read input */
    memset(&input, 0, sizeof(input));
    input.dir = DIR_NONE;
    input_read(&input);

    /* Startup delay - suppress all input */
    if (startup_delay > 0) {
        startup_delay--;
        memset(&input, 0, sizeof(input));
        input.dir = DIR_NONE;
        input_reset();
    } else {
        /* ESC to quit */
        if (input.quit) {
            running = 0;
        }
    }

    /* Update game */
    game_update(&gs, &input);

    /* Detect state transitions that require full maze redraw */
    if (gs.level != prev_level ||
        (prev_state == STATE_TITLE && gs.state == STATE_READY) ||
        (prev_state == STATE_GAMEOVER && gs.state == STATE_TITLE)) {
        draw_set_dirty();
    }

    /* Handle sound events */
    if (gs.ev_flags & EV_CHOMP)     sound_chomp();
    if (gs.ev_flags & EV_EAT_GHOST) sound_eat_ghost();
    if (gs.ev_flags & EV_DIE)       sound_die();
    if (gs.ev_flags & EV_FRUIT)     sound_fruit();
    if (gs.ev_flags & EV_EXTRA)     sound_extra_life();
    if (gs.ev_flags & EV_POWER)     sound_power();
    gs.ev_flags = 0;

    /* Siren + frightened-drone control. Track prior fright state
     * so we shut off the power drone (channel 3) on the transition
     * out of frightened mode, letting the siren resume cleanly. */
    {
        static WORD prev_fright = 0;
        WORD fright_now = gs.fright_active ? 1 : 0;
        if (prev_fright && !fright_now) {
            sound_power_off();
        }
        prev_fright = fright_now;
    }
    if (gs.state == STATE_PLAYING) {
        if (gs.fright_active) {
            sound_siren_off();
        } else {
            sound_set_siren(gs.siren_speed);
        }
    } else {
        sound_siren_off();
        sound_power_off();
    }

    /* Update sound */
    sound_update();
    return running;
}

int main(void)
{
    long old_ssp, last_vbl, frames = 0;
    int running = 1, last_state = -1;
    long last_score = -1;

    nf_init();
    old_ssp = Super(0L);

    if (!gfx_init(palette, 16)) {
        Super((void *)old_ssp);
        Cconws("Not enough memory\r\n");
        return 1;
    }
    gfx_set_frame_vbls(1);
    input_init();
    paula_set_rate(6258);
    if (paula_init(NULL, 50) != 0)
        log_line("no DMA sound - continuing without sound", 0, 0, 0, 0);
    if (sound_init() == -1) {
        log_line("sound_init failed", 0, 0, 0, 0);
        running = 0;
    }

    game_init(&gs);
    draw_set_dirty();

    log_line("START", 0, 0, 0, 0);
    log_line("SYMBOL gs 0x%06lx", (long)&gs, 0, 0, 0);
    log_line("SYMBOL maze 0x%06lx", (long)&gs.maze, 0, 0, 0);
    log_line("SYMBOL muncher 0x%06lx", (long)&gs.muncher, 0, 0, 0);
    log_line("SYMBOL ghosts 0x%06lx", (long)&gs.ghosts, 0, 0, 0);
    log_line("SYMBOL state 0x%06lx", (long)&gs.state, 0, 0, 0);
    log_line("SYMBOL score 0x%06lx", (long)&gs.score, 0, 0, 0);
    log_line("SYMBOL frame_sync 0x%06lx", (long)gfx_swap, 0, 0, 0);

    last_vbl = FRCLOCK;
    while (running) {
        long now = FRCLOCK;
        int steps = (int)(now - last_vbl);
        struct RastPort *rp;
        last_vbl = now;
        if (steps < 1) steps = 1;
        if (steps > 3) steps = 3;

        while (steps-- > 0 && running)
            running = game_step();

        if (gs.state != last_state) {
            log_line("STATE %s level=%ld score=%ld lives=%ld", (long)state_name(gs.state),
                     gs.level, gs.score, gs.lives);
            last_state = gs.state;
        }
        if (gs.score != last_score) {
            if (gs.state != STATE_TITLE && (gs.score / 100 != last_score / 100))
                log_line("SCORE %ld level=%ld", gs.score, gs.level, 0, 0);
            last_score = gs.score;
        }

        /* Render.  The title and game over screens are redrawn every
         * frame by draw.c: on the HUD layer only what changed is
         * rendered.  In the game, draw.c restores tiles under the
         * sprites itself, straight in the back buffer. */
        {
            static int screen_mode = -1;	/* 0 game, 1 full-screen pages */
            int mode = gs.state == STATE_TITLE || gs.state == STATE_GAMEOVER;
            if (mode != screen_mode) {
                gfx_bg_clear();
                gfx_bg_to_screens();
                if (!mode)
                    draw_set_dirty();
                screen_mode = mode;
            }
            if (mode) {
                rp = gfx_hud();
                if (gs.state == STATE_TITLE)
                    draw_title(rp, &gs);
                else
                    draw_gameover(rp, &gs);
                gfx_hud_commit();
                gfx_restore_back();
            } else
                draw_game(gfx_back_nomark(), &gs);
        }
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

    log_line("EXIT frames=%ld score=%ld", frames, gs.score, 0, 0);
    sound_cleanup();
    paula_exit();
    input_exit();
    gfx_exit();
    Super((void *)old_ssp);
    return 0;
}
