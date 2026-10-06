/*
 * LUNAR RIDER - Atari STE port.
 *
 * Port of the Amiga version (main.c in geekychris/amiga_games, kept as
 * main.c.amiga): game.c and sound.c are unmodified, draw.c has ST port
 * blocks (mountains and terrain filled by the ST layer's gfx_vspans,
 * strings as cached operations, the score line on the HUD layer).
 * sound.c pokes Paula; it is built as C++ (sound_st.cpp) against the
 * Paula emulation on the STE's DMA sound.
 *
 * The main loop is main.c's state machine, with the logic run once per
 * 50 Hz VBL (as on the Amiga) and the drawing once per frame.
 *
 * Controls: left / right speed, up / W / Space jump, A or fire shoot,
 * Esc quits.  With Hatari --natfeats on, events and symbol addresses are
 * logged ("LUNAR ..." lines).
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

void input_exit(void);

static GameState gs;
static InputState input;
static WORD startup_delay = 20;

static UWORD palette[16] = {
    0x001,  /*  0: Black (sky) */
    0xFFF,  /*  1: White (stars, text) */
    0x214,  /*  2: Dark purple (far mountains) */
    0x426,  /*  3: Purple (near mountains) */
    0x840,  /*  4: Brown (ground) */
    0x520,  /*  5: Dark brown (ground detail) */
    0xFD0,  /*  6: Yellow (buggy body) */
    0xAAA,  /*  7: Gray (buggy wheels) */
    0xF00,  /*  8: Red (explosions) */
    0xF80,  /*  9: Orange (bullets/fire) */
    0x0EF,  /* 10: Cyan (UFOs) */
    0x0F0,  /* 11: Green (checkpoint text) */
    0x44F,  /* 12: Blue (bombs) */
    0xFF0,  /* 13: Bright yellow (score) */
    0x555,  /* 14: Dark gray (rocks) */
    0xF8F,  /* 15: Pink (meteors) */
};

static void log_line(const char *fmt, long a, long b, long c, long d)
{
    char buf[128];
    strcpy(buf, "LUNAR ");
    snprintf(buf + 6, sizeof(buf) - 7, fmt, a, b, c, d);
    strcat(buf, "\n");
    nf_print(buf);
}

static const char *state_name(int s)
{
    static const char *n[] = { "TITLE", "PLAYING", "DYING", "GAMEOVER", "CHECKPOINT" };
    return s >= 0 && s < 5 ? n[s] : "?";
}

/* one 50 Hz step of main.c's state machine, without the drawing */
static int game_step(void)
{
    int running = 1;

    input_update(&input);
    if (startup_delay > 0) {
        startup_delay--;
        memset(&input, 0, sizeof(input));
    } else {
        if (input.quit) {
            running = 0;
        }
    }

    switch (gs.state) {
        case STATE_TITLE:
            gs.frame++;
            if (input.fire || input.jump) {
                game_init(&gs);
                gs.state = STATE_PLAYING;
            }
            break;

        case STATE_PLAYING:
        case STATE_CHECKPOINT:
            game_update(&gs, &input);
            /* Sound events */
            if (gs.ev_shoot)      sound_shoot();
            if (gs.ev_explode)    sound_explode();
            if (gs.ev_jump)       sound_jump();
            if (gs.ev_checkpoint) sound_checkpoint();
            if (gs.ev_death)      sound_death();
            break;

        case STATE_DYING:
            game_update(&gs, &input);
            if (gs.ev_explode) sound_explode();
            break;

        case STATE_GAMEOVER:
            game_update(&gs, &input);
            if (input.fire || input.jump) {
                gs.state = STATE_TITLE;
            }
            break;
    }

    /* Update sound sequencer */
    sound_update();
    return running;
}

/* main.c's drawing for the current state: title on the HUD layer; in the
 * game, the score line on the HUD layer and the playfield redrawn */
static void game_draw(void)
{
    static int mode = -1;
    int m = gs.state == STATE_TITLE;
    struct RastPort *hud = gfx_hud();

    if (m != mode) {
        gfx_bg_clear();
        gfx_bg_to_screens();
        mode = m;
    }
    if (m) {
        draw_title(hud, gs.frame);
        gfx_hud_commit();
        gfx_restore_back();
        return;
    }
    draw_hud(hud, &gs);
    gfx_hud_commit();
    gfx_restore_back();

    {
        struct RastPort *rp = gfx_back_nomark();
        draw_game(rp, &gs);
        if (gs.state == STATE_CHECKPOINT)
            draw_checkpoint(rp, gs.checkpoint_cur);
        if (gs.state == STATE_GAMEOVER)
            draw_gameover(rp, gs.score);
    }
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
    paula_set_rate(6258);
    if (paula_init(NULL, 50) != 0)
        log_line("no DMA sound - continuing without sound", 0, 0, 0, 0);
    sound_init();
    game_init(&gs);
    gs.state = STATE_TITLE;
    input_init(NULL);
    memset(&input, 0, sizeof(input));

    log_line("START", 0, 0, 0, 0);
    log_line("SYMBOL gs 0x%06lx", (long)&gs, 0, 0, 0);
    log_line("SYMBOL buggy 0x%06lx", (long)&gs.buggy, 0, 0, 0);
    log_line("SYMBOL state 0x%06lx", (long)&gs.state, 0, 0, 0);
    log_line("SYMBOL score 0x%06lx", (long)&gs.score, 0, 0, 0);
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

        if (gs.state != last_state) {
            log_line("STATE %s score=%ld lives=%ld cp=%ld", (long)state_name(gs.state),
                     (long)gs.score, (long)gs.lives, (long)gs.checkpoint_cur);
            last_state = gs.state;
        }
        if ((long)gs.score != last_score) {
            if (gs.state != STATE_TITLE)
                log_line("SCORE %ld lives=%ld", (long)gs.score, (long)gs.lives, 0, 0);
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
    sound_cleanup();
    paula_exit();
    input_exit();
    gfx_exit();
    Super((void *)old_ssp);
    return 0;
}
