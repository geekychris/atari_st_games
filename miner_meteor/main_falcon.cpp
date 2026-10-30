// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * MINER METEOR - Atari Falcon030 port.
 *
 * Port of the Amiga AGA version (main.cpp, kept as main.cpp.amiga, in
 * geekychris/amiga_games): game.cpp, levels.cpp, audio.cpp and the tile
 * sheet are the original C++; render.cpp has __MINT__ branches for the
 * Falcon's true colour screen (../falcon_port/fgfx). This file is
 * main.cpp with the AmigaOS parts (libraries, IDCMP window, Ctrl-C,
 * WaitTOF) replaced by IKBD input, the VBL counter and NatFeats logging.
 *
 * F1 on the title plays a recorded run through the first level (demo.h).
 *
 * Controls, as on the Amiga: cursor left/right or A/D walk, Space or
 * cursor up jumps (Space also starts / returns on the title), joystick
 * fire jumps / starts, Esc quits. The port also reads the joystick's
 * left/right (the Amiga version has them disabled).
 */
#include <osbind.h>
#include <string.h>

extern "C" {
#include "bridge_client.h"
#include "st_ikbd.h"
}
#include "game.h"
#include "render.h"
#include "audio.h"
#include "demo.h"

#define FRCLOCK (*(volatile long *)0x466)
#define SC_F1   0x3b
#define TICK_HZ 50          /* the Amiga runs one tick per PAL frame */

static GameState state;
static MinerGame game;

/* Port addition: the recorded first-level run in demo.h, started from the
 * title with F1. It feeds the game one input per tick (so it plays
 * the same at any frame rate) and hands control back when the level is
 * done. */
static int demo_run = -1, demo_tick;
static int demo_input(UBYTE *in)
{
    if (demo_run < 0) return 0;
    if (state.mode == GM_TITLE) { *in = INPUT_START; return 1; }
    if (state.mode != GM_PLAYING || state.level_index != 0 || demo_run >= DEMO_RUNS) {
        demo_run = -1;
        AB_I("demo: done, level=%ld score=%ld", (long)state.level_index + 1, (long)state.score);
        return 0;
    }
    *in = DEMO_RUN[demo_run][0];
    if (++demo_tick >= DEMO_RUN[demo_run][1]) { demo_run++; demo_tick = 0; }
    return 1;
}

static const char *mode_names[] = { "TITLE", "PLAYING", "WIN", "LOSE", "LEVELWIN" };

static UBYTE read_input_flags(void)
{
    volatile UBYTE *k = ikbd_keys;
    UBYTE j = ikbd_joy1();
    UBYTE f = 0;
    if (k[SC_LEFT]  || k[SC_A] || (j & 4))     f |= INPUT_LEFT;
    if (k[SC_RIGHT] || k[SC_D] || (j & 8))     f |= INPUT_RIGHT;
    if (k[SC_SPACE] || k[SC_UP] || (j & 0x80)) f |= INPUT_JUMP;
    if (k[SC_SPACE] || (j & 0x80))             f |= INPUT_START;
    return f;
}

/* Keys (and joystick directions) pressed since the last call, even if
 * already released: a tap shorter than one drawn frame still counts, as
 * the Amiga's queued RAWKEY messages do. Same keys as read_input_flags. */
static UBYTE read_hit_flags(void)
{
    UBYTE j = ikbd_joy1_hits();
    int space = ikbd_key_hit(SC_SPACE);
    UBYTE f = 0;
    if (ikbd_key_hit(SC_LEFT)  | ikbd_key_hit(SC_A) | (j & 4))     f |= INPUT_LEFT;
    if (ikbd_key_hit(SC_RIGHT) | ikbd_key_hit(SC_D) | (j & 8))     f |= INPUT_RIGHT;
    if (space | ikbd_key_hit(SC_UP) | (j & 0x80))                  f |= INPUT_JUMP;
    if (space | (j & 0x80))                                        f |= INPUT_START;
    return f;
}

int main(void)
{
    long old_ssp;

    nf_init();
    old_ssp = Super(0L);              /* IKBD, VBL counter */
    ab_init("MINER");

    if (render_open() != 0) {
        Super((void *)old_ssp);
        AB_E("render: open failed (needs a Falcon)");
        return 20;
    }
    ikbd_init(IKBD_JOYSTICK);

    memset(&state, 0, sizeof(state));
    game.init(&state);
    audio_init();
    /* addresses for agents (agent API /mem) */
    AB_I("SYMBOL state 0x%06lx", (long)&state);
    AB_I("miner-meteor: entering main loop");

    const int vbl_hz = fgfx_height() == 240 ? 60 : 50;   /* VGA / RGB */
    long last_vbl = FRCLOCK, fps_vbl = FRCLOCK, tick_acc = 0;
    ULONG frames = 0, fps_frame = 0;
    LONG last_mode = -1, last_level = -1, last_lives = -1, last_keys = -1;
    UBYTE prev_input = 0;

    for (;;) {
        if (ikbd_keys[SC_ESC]) break;

        /* Ticks due at 50 Hz since the last loop (up to 4); wait for
         * the next VBL when none is due yet. */
        long now = FRCLOCK;
        tick_acc += (now - last_vbl) * TICK_HZ;
        last_vbl = now;
        int ticks = (int)(tick_acc / vbl_hz);
        if (ticks == 0) {
            while (FRCLOCK == now) { }
            continue;
        }
        tick_acc -= (long)ticks * vbl_hz;
        if (ticks > 4) { ticks = 4; tick_acc = 0; }

        if (ikbd_key_hit(SC_F1) && state.mode == GM_TITLE && demo_run < 0) {
            demo_run = 0;
            demo_tick = 0;
            AB_I("demo: start");
        }

        UBYTE input = read_input_flags();
        UBYTE hits  = read_hit_flags();
        UBYTE edge  = (UBYTE)((input & ~prev_input) | hits);
        prev_input  = input;

        /* SPACE is JUMP during play and START on the title / end
         * screens. INPUT_START only fires on rising edge — otherwise
         * a held SPACE re-triggers the state transition every frame. */
        for (int t = 0; t < ticks; t++) {
            UBYTE tick_input = (UBYTE)(input & (INPUT_LEFT | INPUT_RIGHT | INPUT_JUMP));
            if (t == 0) tick_input |= (UBYTE)(hits & (INPUT_LEFT | INPUT_RIGHT | INPUT_JUMP));
            if (t == 0 && (edge & INPUT_START)) tick_input |= INPUT_START;
            demo_input(&tick_input);
            game.tick(tick_input);
        }
        if (edge & INPUT_JUMP) sfx_jump();

        if (state.mode != last_mode || state.level_index != last_level ||
            state.lives != last_lives || state.keys_remaining != last_keys) {
            AB_I("state=%s level=%ld lives=%ld score=%ld keys=%ld/%ld air=%ld",
                 mode_names[state.mode % 5], (long)state.level_index + 1,
                 (long)state.lives, (long)state.score,
                 (long)(state.keys_total - state.keys_remaining),
                 (long)state.keys_total, (long)state.air);
            last_mode = state.mode;
            last_level = state.level_index;
            last_lives = state.lives;
            last_keys = state.keys_remaining;
        }

        if (state.mode == GM_TITLE) {
            render_title(state);
        } else if (state.mode == GM_WIN || state.mode == GM_LOSE) {
            render_endscreen(state);
        } else {
            render_frame(state);
        }
        render_flip();

        if ((++frames % 100) == 0) {
            long vbls = FRCLOCK - fps_vbl;
            long fps10 = vbls > 0 ? (10L * vbl_hz * (long)(frames - fps_frame)) / vbls : 0;
            AB_I("PERF frame=%ld fps=%ld.%ld mode=%ld x=%ld y=%ld air=%ld",
                 (long)frames, fps10 / 10, fps10 % 10, (long)state.mode,
                 (long)state.player.x, (long)state.player.y, (long)state.air);
            fps_vbl = FRCLOCK;
            fps_frame = frames;
        }
    }

    AB_I("miner-meteor: shutting down");
    audio_shutdown();
    ikbd_exit();
    render_close();
    Super((void *)old_ssp);
    return 0;
}
