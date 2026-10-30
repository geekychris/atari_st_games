// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * miner-meteor — clean-room single-screen tile platformer.
 * Classic 68k build only for now (see Makefile ARCH= for PPC scaffolding).
 *
 * Controls (rawkey codes):
 *   LEFT / RIGHT   walk
 *   SPACE          jump (also start / continue on title / end screens)
 *   ESC            quit
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
#include <hardware/custom.h>
#include <hardware/cia.h>

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
/* Classic 68k needs the app to own the library bases; on OS4 the
 * proto headers extern these already. */
struct IntuitionBase *IntuitionBase;
struct GfxBase       *GfxBase;
#endif

/* Bigger stack: 64K covers the level ASCII tables + render buffers
 * with a safe margin. Matches void_trader / canyon_rescue. */
ULONG __stack = 65536;

/* Raw-key codes. */
#define RK_ESC     0x45
#define RK_SPACE   0x40
#define RK_LEFT    0x4F
#define RK_RIGHT   0x4E
#define RK_UP      0x4C
#define RK_A       0x20
#define RK_D       0x22

/* Bit-per-key state built from IDCMP_RAWKEY events. */
static UBYTE key_state[128];

/* Amiga chip regs — used for joystick + fire-button reads. Any Amiga
 * game convention: joystick port 1 (the "player 1" port, sold as
 * standard on every stock Amiga) = JOY1DAT; fire = CIA-A PRA bit 7. */
extern volatile struct Custom custom;
extern volatile struct CIA    ciaa;

/* Update key_state from an IDCMP_RAWKEY code. Bit 7 (0x80) = key-up. */
static void apply_key(UWORD code)
{
    UBYTE raw = (UBYTE)(code & 0x7F);
    UBYTE up  = (code & 0x80) ? 1 : 0;
    key_state[raw] = up ? 0 : 1;
}

/* Decode joystick port 1 into INPUT_ bits.
 *
 * Fire button: CIA-A PRA bit 7, active low.
 *
 * Joystick direction decode is INTENTIONALLY DISABLED for now — the
 * naive quadrature read (bit 1 = right, bit 1 XOR bit 0 = left) trips
 * false LEFT positives at rest under FS-UAE, giving the appearance of
 * a stuck stick. Under FS-UAE, arrow keys already reach us as
 * IDCMP_RAWKEY events too, so cursor-key play works via the keyboard
 * path; real-hardware joystick is a follow-up (needs debouncing +
 * per-frame edge detect + potgo/potinp priming). */
static UBYTE read_joystick_flags(void)
{
    UBYTE f = 0;
    if (!(ciaa.ciapra & 0x80)) {
        f |= INPUT_JUMP;
        f |= INPUT_START;
    }
    return f;
}

static UBYTE read_input_flags(void)
{
    UBYTE f = 0;
    /* Keyboard. */
    if (key_state[RK_LEFT]  || key_state[RK_A])     f |= INPUT_LEFT;
    if (key_state[RK_RIGHT] || key_state[RK_D])     f |= INPUT_RIGHT;
    if (key_state[RK_SPACE] || key_state[RK_UP])    f |= INPUT_JUMP;
    if (key_state[RK_SPACE])                        f |= INPUT_START;
    /* Joystick fire button only (see comment on read_joystick_flags). */
    f |= read_joystick_flags();
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

    if (ab_init((char *)"miner-meteor")) {
        /* Bridge unavailable — game still runs, uninstrumented. */
    }

    if (render_open() != 0) {
        AB_E("render: open failed");
        return 20;
    }

    /* Open a borderless backdrop window on the screen so we can
     * receive IDCMP_RAWKEY. The screen belongs to render.cpp; we ask
     * it for a pointer. */
    extern struct Screen *render_get_screen(void);
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

    MinerGame game;
    game.init(&state);

    ab_register_var((char *)"mode",           AB_TYPE_I32, &state.mode);
    ab_register_var((char *)"level",          AB_TYPE_I32, &state.level_index);
    ab_register_var((char *)"lives",          AB_TYPE_I32, &state.lives);
    ab_register_var((char *)"score",          AB_TYPE_I32, &state.score);
    ab_register_var((char *)"air",            AB_TYPE_I32, &state.air);
    ab_register_var((char *)"keys_remaining", AB_TYPE_I32, &state.keys_remaining);

    audio_init();

    AB_I("miner-meteor: entering main loop");

    LONG running = 1;
    UBYTE prev_input = 0;

    while (running) {
        ab_poll();

        /* Drain all IDCMP messages first so the input state is up to
         * date for this frame. */
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

        /* SIGBREAKF_CTRL_C = clean quit request from the bridge. */
        if (SetSignal(0L, 0L) & SIGBREAKF_CTRL_C) break;

        UBYTE input = read_input_flags();
        UBYTE edge  = input & ~prev_input;
        prev_input  = input;

        /* SPACE is JUMP during play and START on the title / end
         * screens. INPUT_START only fires on rising edge — otherwise
         * a held SPACE re-triggers the state transition every frame. */
        UBYTE tick_input = (UBYTE)(input & (INPUT_LEFT | INPUT_RIGHT | INPUT_JUMP));
        if (edge & INPUT_START) tick_input |= INPUT_START;

        game.tick(tick_input);

        if (edge & INPUT_JUMP) sfx_jump();

        if (state.mode == GM_TITLE) {
            render_title(state);
        } else if (state.mode == GM_WIN || state.mode == GM_LOSE) {
            render_endscreen(state);
        } else {
            render_frame(state);
        }
        /* Flip the freshly-drawn back buffer onto the display —
         * eliminates the single-buffer clear/redraw flicker. */
        render_flip();

        ab_poll();

        /* 50 Hz classic PAL frame cap. */
        WaitTOF();
    }

    AB_I("miner-meteor: shutting down");
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
