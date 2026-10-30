// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * emberkeep — clean-room top-down lantern-in-the-dark explorer.
 *
 * Controls (rawkey):
 *   CURSOR KEYS   walk (4-way)
 *   SPACE         begin / restart
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
#include "audio.h"

#ifndef __PPC__
struct IntuitionBase *IntuitionBase;
struct GfxBase       *GfxBase;
#endif

ULONG __stack = 65536;

/* Raw-key codes. */
#define RK_ESC     0x45
#define RK_SPACE   0x40
#define RK_LEFT    0x4F
#define RK_RIGHT   0x4E
#define RK_UP      0x4C
#define RK_DOWN    0x4D
#define RK_A       0x20
#define RK_D       0x22
#define RK_W       0x11
#define RK_S       0x21

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
    if (key_state[RK_LEFT]  || key_state[RK_A]) f |= INPUT_LEFT;
    if (key_state[RK_RIGHT] || key_state[RK_D]) f |= INPUT_RIGHT;
    if (key_state[RK_UP]    || key_state[RK_W]) f |= INPUT_UP;
    if (key_state[RK_DOWN]  || key_state[RK_S]) f |= INPUT_DOWN;
    if (key_state[RK_SPACE])                    f |= INPUT_START;
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

    if (ab_init((char *)"emberkeep")) {
        /* Bridge unavailable — game still runs, uninstrumented. */
    }

    if (render_open() != 0) {
        AB_E("render: open failed");
        return 20;
    }

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
    if (!win) {
        AB_E("input: OpenWindow failed");
        render_close();
        return 21;
    }

    GameState state;
    memset(&state, 0, sizeof(state));
    EmberGame game;
    game.init(&state);

    ab_register_var((char *)"mode",             AB_TYPE_I32, &state.mode);
    ab_register_var((char *)"level",            AB_TYPE_I32, &state.level_index);
    ab_register_var((char *)"lives",            AB_TYPE_I32, &state.lives);
    ab_register_var((char *)"score",            AB_TYPE_I32, &state.score);
    ab_register_var((char *)"lantern",          AB_TYPE_I32, &state.lantern);
    ab_register_var((char *)"runes_remaining", AB_TYPE_I32, &state.runes_remaining);

    audio_init();

    AB_I("emberkeep: entering main loop");

    LONG running = 1;
    UBYTE prev_input = 0;

    while (running) {
        ab_poll();

        struct IntuiMessage *msg;
        while ((msg = (struct IntuiMessage *)GetMsg(win->UserPort))) {
            UWORD  code = msg->Code;
            ULONG  cls  = msg->Class;
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

        /* INPUT_START only fires on rising edge — otherwise a held
         * SPACE re-triggers the state transition every frame. */
        UBYTE tick_input = (UBYTE)(input & (INPUT_LEFT | INPUT_RIGHT | INPUT_UP | INPUT_DOWN));
        if (edge & INPUT_START) tick_input |= INPUT_START;

        game.tick(tick_input);

        if (state.mode == GM_TITLE) {
            render_title(state);
        } else if (state.mode == GM_WIN || state.mode == GM_LOSE) {
            render_endscreen(state);
        } else {
            render_frame(state);
        }
        render_flip();

        ab_poll();
        WaitTOF();
    }

    AB_I("emberkeep: shutting down");
    audio_shutdown();
    if (win) CloseWindow(win);
    render_close();
    ab_cleanup();

#ifndef __PPC__
    if (GfxBase)       CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
#endif
    return 0;
}
