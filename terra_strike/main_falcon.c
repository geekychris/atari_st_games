// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * TERRA STRIKE for the Atari Falcon030: the counterpart of main.c (classic
 * Amiga) and main_os4.c (AmigaOS 4), on the machine layer the amiga3d
 * ports use (../a3d_falcon/falcon_sys.c): the 256-colour VGA screen,
 * double buffered (render_hud's cache knows two buffers), the IKBD keys as
 * the Amiga raw keys game.c maps, joystick 1, ptplayer's music and effects
 * on the Paula emulation, and the timers. game.c, the simulation and the
 * renderer are the Amiga game's; render.c draws through ../a3d_falcon.
 *
 *   Arrows / joystick   turn and climb / dive
 *   A / Z               throttle up / down
 *   Space / Alt / fire  cannon
 *   P                   pause        Esc   title / quit
 */
#include <osbind.h>
#include <stdio.h>
#include <stdlib.h>

#include "bridge_client.h"
#include "ts.h"
#include "models.h"
#include "render.h"
#include "title_image.h"
#include "sound.h"
#include "game.h"
#include "falcon_sys.h"
#include "natfeats.h"

static Canvas cvs[2];
static int32_t in_keys, in_joy;

/* the Amiga game's palette (main.c) */
static const uint16_t palette[32] = {
    0x000, 0xFFF, 0x333, 0x999,
    0x027, 0x04A,            /* sea */
    0xB95, 0xDB7,            /* sand */
    0x392, 0x5B3,            /* grass */
    0x161, 0x283,            /* forest */
    0x654, 0x987,            /* rock */
    0xAAB, 0xEEF,            /* snow */
    0xF30, 0x900, 0xFE0, 0xF80,
    0x9EF, 0x59B, 0x357,     /* craft */
    0xC64, 0x732,            /* tank */
    0xDDC, 0x998, 0xB43,     /* building, roof */
    0xABC, 0x567,            /* ship */
    0x2C3, 0x111             /* tree, shadow */
};

/* ---------- what game.c needs from the platform ---------- */
int32_t plat_ticks(void) { return (int32_t)(fsys_hz200() >> 2); }
uint32_t plat_clock(void) { return fsys_clock(); }
int32_t plat_us_since(uint32_t t0) { return fsys_us_since(t0); }

void plat_palette(int title)
{
    fsys_rgb12(0, 32, title ? title_palette : palette);
}

static int ts_main(void)
{
    int running = 1, cur = 1, i, window10 = 0;
    long mod_size;

    ab_init("terra_strike");
    AB_I("Terra Strike starting (Falcon030)");
    ab_register_var("in_keys", AB_TYPE_I32, &in_keys);
    ab_register_var("in_joy",  AB_TYPE_I32, &in_joy);
    game_init();
    if (!render_init() || !fsys_screen_init(2)) { AB_E("display setup failed"); return 1; }
    for (i = 0; i < 2; i++) { cvs[i].base = fsys_buffer(i); cvs[i].rowoff = fsys_rowoff(); cvs[i].h = SCREEN_H; }
    plat_palette(0);
    fsys_input_init();

    if (!fsys_sound_init()) AB_W("no DMA sound - continuing without sound");
    sound_init();
    if ((mod_size = fsys_music("PROGDIR:terra_strike.mod", 2))) AB_I("music loaded (%ld bytes)", mod_size);
    else AB_W("terra_strike.mod not found - no music");

    AB_I("island seed %ld, pad at tile %ld,%ld height %ld, %ld targets",
         (long)world.seed, (long)world.pad_tx, (long)world.pad_tz, (long)world.pad_h,
         (long)world.targets_left);

    while (running) {
        unsigned in, held, pressed, joy;
        uint32_t t0;

        pressed = fsys_keys(game_key_bit, &held);
        joy = fsys_joy();               /* FSYS_JOY_* are IN_LEFT .. IN_FIRE */
        if (++window10 > 250) { window10 = 0; in_keys = in_joy = 0; }
        in_keys |= (int32_t)(held | pressed);
        in_joy |= (int32_t)joy;
        in = held | pressed | joy;
        if (!game_input(in)) running = 0;
        game_update(in);
        fsys_wait_shown();              /* the buffer drawn next is off the screen */
        t0 = plat_clock();
        game_draw(&cvs[cur], cur);
        t0 = plat_clock() - t0;
        game_after_frame();
        fsys_pace(t0);
        fsys_show(cur);
        cur ^= 1;
    }

    AB_I("Terra Strike shutting down, score %ld", (long)world.score);
    fsys_sound_exit();
    sound_cleanup();
    fsys_input_exit();
    fsys_screen_exit();
    render_cleanup();
    ab_cleanup();
    return 0;
}

int main(void)
{
    long old_ssp;
    int rc;
    nf_init();
    old_ssp = Super(0L);
    rc = ts_main();
    Super((void *)old_ssp);
    return rc;
}
