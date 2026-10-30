// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * WARDEN KEEP - Atari Falcon030 port.
 *
 * Port of the Amiga version (main.cpp, kept as main.cpp.amiga, in
 * geekychris/amiga_games): game.cpp is the original C++; render.cpp has
 * __MINT__ branches for the Falcon's true colour screen
 * (../falcon_port/fgfx). This file is main.cpp with the AmigaOS parts
 * (libraries, IDCMP window, Ctrl-C, WaitTOF) replaced by IKBD input, the
 * VBL counter and NatFeats logging; the auto-repeat is main.cpp's.
 *
 * Controls, as on the Amiga: cursor keys, WASD, IJKL or keypad 8/4/6/2
 * (or joystick) step one tile, held keys repeat; Space, Return or Enter
 * (or fire) start / return to the title; Esc quits.
 */
#include <osbind.h>
#include <string.h>

extern "C" {
#include "bridge_client.h"
#include "st_ikbd.h"
}
#include "game.h"
#include "render.h"

#define FRCLOCK (*(volatile long *)0x466)
#define TICK_HZ 50          /* the Amiga runs one tick per PAL frame */

/* scancodes st_ikbd.h doesn't name */
#define SC_I       0x17
#define SC_J       0x24
#define SC_K       0x25
#define SC_KP8     0x68
#define SC_KP4     0x6a
#define SC_KP6     0x6c
#define SC_KP2     0x6e
#define SC_KPENTER 0x72

static GameState  state;
static WardenGame game;

static const char *mode_names[] = { "TITLE", "PLAYING", "WIN" };

static UBYTE read_input_flags(void)
{
    volatile UBYTE *k = ikbd_keys;
    UBYTE j = ikbd_joy1();
    UBYTE f = 0;
    if (k[SC_LEFT]  || k[SC_A] || k[SC_J] || k[SC_KP4] || (j & 4))  f |= INPUT_LEFT;
    if (k[SC_RIGHT] || k[SC_D] || k[SC_L] || k[SC_KP6] || (j & 8))  f |= INPUT_RIGHT;
    if (k[SC_UP]    || k[SC_W] || k[SC_I] || k[SC_KP8] || (j & 1))  f |= INPUT_UP;
    if (k[SC_DOWN]  || k[SC_S] || k[SC_K] || k[SC_KP2] || (j & 2))  f |= INPUT_DOWN;
    if (k[SC_SPACE] || k[SC_RETURN] || k[SC_KPENTER] || (j & 0x80)) f |= INPUT_START;
    return f;
}

/* Keys (and joystick directions) pressed since the last call, even if
 * already released: a tap shorter than one drawn frame still counts, as
 * the Amiga's queued RAWKEY messages do. Same keys as read_input_flags. */
static UBYTE read_hit_flags(void)
{
    UBYTE j = ikbd_joy1_hits();
    UBYTE f = 0;
    if (ikbd_key_hit(SC_LEFT)  | ikbd_key_hit(SC_A) | ikbd_key_hit(SC_J) | ikbd_key_hit(SC_KP4) | (j & 4))  f |= INPUT_LEFT;
    if (ikbd_key_hit(SC_RIGHT) | ikbd_key_hit(SC_D) | ikbd_key_hit(SC_L) | ikbd_key_hit(SC_KP6) | (j & 8))  f |= INPUT_RIGHT;
    if (ikbd_key_hit(SC_UP)    | ikbd_key_hit(SC_W) | ikbd_key_hit(SC_I) | ikbd_key_hit(SC_KP8) | (j & 1))  f |= INPUT_UP;
    if (ikbd_key_hit(SC_DOWN)  | ikbd_key_hit(SC_S) | ikbd_key_hit(SC_K) | ikbd_key_hit(SC_KP2) | (j & 2))  f |= INPUT_DOWN;
    if (ikbd_key_hit(SC_SPACE) | ikbd_key_hit(SC_RETURN) | ikbd_key_hit(SC_KPENTER) | (j & 0x80)) f |= INPUT_START;
    return f;
}

int main(void)
{
    long old_ssp;

    nf_init();
    old_ssp = Super(0L);              /* IKBD, VBL counter */
    ab_init("WARDEN");

    if (render_open() != 0) {
        Super((void *)old_ssp);
        AB_E("render: open failed (needs a Falcon)");
        return 20;
    }
    ikbd_init(IKBD_JOYSTICK);

    memset(&state, 0, sizeof(state));
    game.init(&state);
    AB_I("SYMBOL state 0x%06lx", (long)&state);
    AB_I("warden-keep: entering main loop");

    const int vbl_hz = fgfx_height() == 240 ? 60 : 50;   /* VGA / RGB */
    long last_vbl = FRCLOCK, fps_vbl = FRCLOCK, tick_acc = 0;
    ULONG frames = 0, fps_frame = 0;
    LONG last_mode = -1, last_jewels = -1, last_x = -1, last_y = -1;
    UBYTE prev_input = 0;

    /* Auto-repeat state per-direction. Move fires on rising edge, then
     * again after HOLD_DELAY frames, then every REPEAT_EVERY frames
     * while the key stays held. Feels responsive without spamming. */
    #define HOLD_DELAY    12    /* ~0.24s at 50 Hz PAL */
    #define REPEAT_EVERY   6    /* ~0.12s between repeats */
    LONG held_frames = 0;
    UBYTE held_dirs  = 0;

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

        UBYTE input = read_input_flags();
        UBYTE hits  = read_hit_flags();
        for (int t = 0; t < ticks; t++) {
            /* ---- per-frame input logic copied from the Amiga main.cpp ---- */
            UBYTE edge  = input & ~prev_input;
            if (t == 0) edge |= hits;    /* port: taps shorter than a frame */
            prev_input  = input;

            UBYTE dirs = (UBYTE)(input & (INPUT_LEFT | INPUT_RIGHT | INPUT_UP | INPUT_DOWN));
            UBYTE tick_input = 0;

            UBYTE edge_dirs = (UBYTE)(edge & (INPUT_LEFT | INPUT_RIGHT | INPUT_UP | INPUT_DOWN));
            if (edge_dirs) {
                tick_input |= edge_dirs;
                held_dirs = dirs;
                held_frames = 0;
            } else if (dirs && dirs == held_dirs) {
                held_frames++;
                if (held_frames == HOLD_DELAY ||
                    (held_frames > HOLD_DELAY &&
                     ((held_frames - HOLD_DELAY) % REPEAT_EVERY) == 0)) {
                    tick_input |= dirs;
                }
            } else {
                held_dirs = dirs;
                held_frames = 0;
            }

            if (edge & INPUT_START) tick_input |= INPUT_START;

            game.tick(tick_input);
        }

        if (state.mode != last_mode || state.jewels_left != last_jewels) {
            AB_I("state=%s score=%ld jewels_left=%ld", mode_names[state.mode % 3],
                 (long)state.score, (long)state.jewels_left);
            last_mode = state.mode;
            last_jewels = state.jewels_left;
        }
        if (state.mode == GM_PLAYING &&
            (state.player.wx != last_x || state.player.wy != last_y)) {
            AB_I("player wx=%ld wy=%ld", (long)state.player.wx, (long)state.player.wy);
            last_x = state.player.wx;
            last_y = state.player.wy;
        }

        if (state.mode == GM_TITLE)      render_title(state);
        else if (state.mode == GM_WIN)   render_endscreen(state);
        else                              render_frame(state);
        render_flip();

        if ((++frames % 100) == 0) {
            long vbls = FRCLOCK - fps_vbl;
            long fps10 = vbls > 0 ? (10L * vbl_hz * (long)(frames - fps_frame)) / vbls : 0;
            AB_I("PERF frame=%ld fps=%ld.%ld mode=%ld", (long)frames,
                 fps10 / 10, fps10 % 10, (long)state.mode);
            fps_vbl = FRCLOCK;
            fps_frame = frames;
        }
    }

    AB_I("warden-keep: shutting down");
    ikbd_exit();
    render_close();
    Super((void *)old_ssp);
    return 0;
}
