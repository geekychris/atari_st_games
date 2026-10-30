// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * warden_keep — isometric Filmation-style single-room adventure.
 *
 * Controls:
 *   CURSOR KEYS   walk (tile-locked, one step per key press)
 *   SPACE         begin / return
 *   ESC           quit
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <graphics/view.h>
#include <graphics/rastport.h>
#include <devices/inputevent.h>
#include <dos/dos.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/dos.h>

#include <string.h>

extern "C" {
#include "bridge_client.h"
}
#include "game.h"
#include "render.h"

#ifndef __PPC__
struct IntuitionBase *IntuitionBase;
struct GfxBase       *GfxBase;
#endif

ULONG __stack = 65536;

/* Cursor keys, WASD, IJKL, and numpad 8-4-6-2 all move — the
 * cursor-key path is often eaten by FS-UAE's default joystick
 * mapping, so we deliberately offer three redundant letter sets.
 * ENTER + RETURN + SPACE all also start / restart. */
#define RK_ESC       0x45
#define RK_SPACE     0x40
#define RK_RETURN    0x44
#define RK_ENTER     0x43     /* numpad Enter */
#define RK_LEFT      0x4F
#define RK_RIGHT     0x4E
#define RK_UP        0x4C
#define RK_DOWN      0x4D
#define RK_A         0x20
#define RK_D         0x22
#define RK_W         0x11
#define RK_S         0x21
#define RK_I         0x17
#define RK_J         0x26
#define RK_K         0x27
#define RK_L         0x28
#define RK_KP4       0x2D     /* numpad 4 */
#define RK_KP6       0x2F     /* numpad 6 */
#define RK_KP8       0x3E     /* numpad 8 */
#define RK_KP2       0x1E     /* numpad 2 */

static UBYTE key_state[128];

static void apply_key(UWORD code)
{
    UBYTE raw = (UBYTE)(code & 0x7F);
    UBYTE up  = (code & 0x80) ? 1 : 0;
    key_state[raw] = up ? 0 : 1;
}

static UBYTE read_input_flags(void)
{
    UBYTE f = 0;
    if (key_state[RK_LEFT]  || key_state[RK_A] ||
        key_state[RK_J]     || key_state[RK_KP4])  f |= INPUT_LEFT;
    if (key_state[RK_RIGHT] || key_state[RK_D] ||
        key_state[RK_L]     || key_state[RK_KP6])  f |= INPUT_RIGHT;
    if (key_state[RK_UP]    || key_state[RK_W] ||
        key_state[RK_I]     || key_state[RK_KP8])  f |= INPUT_UP;
    if (key_state[RK_DOWN]  || key_state[RK_S] ||
        key_state[RK_K]     || key_state[RK_KP2])  f |= INPUT_DOWN;
    if (key_state[RK_SPACE] || key_state[RK_RETURN] ||
        key_state[RK_ENTER])                        f |= INPUT_START;
    return f;
}

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

#ifndef __PPC__
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 33);
    GfxBase       = (struct GfxBase *)OpenLibrary((STRPTR)"graphics.library", 33);
    if (!IntuitionBase || !GfxBase) return 20;
#endif

    if (ab_init((char *)"warden-keep")) { /* uninstrumented if fails */ }

    if (render_open() != 0) { AB_E("render: open failed"); return 20; }

    struct Screen *scr = render_get_screen();
    struct Window *win = NULL;
    if (scr) {
        win = OpenWindowTags(NULL,
            WA_CustomScreen, (ULONG)scr,
            WA_Left, 0, WA_Top, 0,
            WA_Width,  (ULONG)SCREEN_W,
            WA_Height, (ULONG)SCREEN_H,
            WA_Borderless, TRUE,
            WA_Backdrop,   TRUE,
            WA_Activate,   TRUE,
            WA_IDCMP,      IDCMP_RAWKEY,
            TAG_DONE);
    }
    if (!win) { AB_E("input: OpenWindow failed"); render_close(); return 21; }

    GameState state;
    memset(&state, 0, sizeof(state));
    WardenGame game;
    game.init(&state);

    ab_register_var((char *)"mode",         AB_TYPE_I32, &state.mode);
    ab_register_var((char *)"score",        AB_TYPE_I32, &state.score);
    ab_register_var((char *)"jewels_left",  AB_TYPE_I32, &state.jewels_left);

    AB_I("warden-keep: entering main loop");

    LONG running = 1;
    UBYTE prev_input = 0;
    /* Auto-repeat state per-direction. Move fires on rising edge, then
     * again after HOLD_DELAY frames, then every REPEAT_EVERY frames
     * while the key stays held. Feels responsive without spamming. */
    #define HOLD_DELAY    12    /* ~0.24s at 50 Hz PAL */
    #define REPEAT_EVERY   6    /* ~0.12s between repeats */
    LONG held_frames = 0;
    UBYTE held_dirs  = 0;

    while (running) {
        ab_poll();

        struct IntuiMessage *msg;
        while ((msg = (struct IntuiMessage *)GetMsg(win->UserPort))) {
            UWORD code = msg->Code;
            ULONG cls  = msg->Class;
            ReplyMsg((struct Message *)msg);
            if (cls == IDCMP_RAWKEY) {
                if ((code & 0x7F) == RK_ESC) { running = 0; break; }
                apply_key(code);
            }
        }
        if (!running) break;
        if (SetSignal(0L, 0L) & SIGBREAKF_CTRL_C) break;

        UBYTE input = read_input_flags();
        UBYTE edge  = input & ~prev_input;
        prev_input  = input;

        UBYTE dirs = (UBYTE)(input & (INPUT_LEFT | INPUT_RIGHT | INPUT_UP | INPUT_DOWN));
        UBYTE tick_input = 0;

        /* Rising edge on any direction fires immediately + starts the
         * auto-repeat clock. Held key repeats after HOLD_DELAY and then
         * every REPEAT_EVERY frames. Releasing all directions resets. */
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

        if (state.mode == GM_TITLE)      render_title(state);
        else if (state.mode == GM_WIN)   render_endscreen(state);
        else                              render_frame(state);
        render_flip();

        ab_poll();
        WaitTOF();
    }

    AB_I("warden-keep: shutting down");
    if (win) CloseWindow(win);
    render_close();
    ab_cleanup();

#ifndef __PPC__
    if (GfxBase)       CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
#endif
    return 0;
}
