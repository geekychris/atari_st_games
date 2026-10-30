// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * main_falcon.c - the shared amiga3d engine's Atari Falcon030 main, the
 * counterpart of main_68k.c (classic Amiga) and main_os4.c (AmigaOS 4),
 * on the machine layer in falcon_sys.c:
 *
 *  - the screen: 320 x 240 in 256 colours on VGA (320 x 200 on RGB/TV),
 *    triple buffered (drawing never waits for the display); the games'
 *    320 x 256 drawing goes through a row map (a3d_draw.c), so one row in
 *    16 is dropped;
 *  - the palette: the games' 32 pens, and for plat_sky 16 more pens shaded
 *    from the top colour to the bottom one over its rows (the copper's
 *    16 steps on the classic Amiga);
 *  - input: the IKBD keys as the Amiga raw key codes the games'
 *    game_key_bit expects, and joystick 1;
 *  - sound: ptplayer (../st_port/ptplayer, C) on the Paula emulation
 *    (Falcon DMA sound) for the music and the effects, which never take
 *    the music's two channels;
 *  - time: plat_ticks from the 200 Hz system timer, plat_clock from MFP
 *    timer C's counter (26 us steps); at most 25 frames a second as on
 *    the Amigas (the games run their simulations from plat_ticks, so they
 *    keep to time when the drawing is slower).
 *
 * The music and voice files ("PROGDIR:x.mod") are read from DATA\ beside
 * the program under their 8.3 names (a3f_path). Every 5 s a PERF line
 * (frames a second, the drawing's share) goes to the host through
 * NatFeats; the games' own variables and hooks are reached through
 * bridge.c's mailbox.
 */
#include <osbind.h>
#include <stdlib.h>
#include <string.h>

#include "bridge_client.h"
#include "a3d_plat.h"
#include "a3d_sound.h"
#include "a3d_falcon.h"
#include "falcon_sys.h"
#include "natfeats.h"
#include "ptplayer.h"
#include "paula.h"

#define CUSTOM_BASE ((void *)&custom)
#define NBUF 3                          /* shown, queued for the next VBL, drawn */

static Canvas cvs[NBUF];
int32_t plat_in_keys, plat_in_joy;

/* ---------- the palette ---------- */
static uint16_t pens[48];

void plat_palette(const uint16_t pal[32])
{
    memcpy(pens, pal, 32 * sizeof(uint16_t));
    fsys_rgb12(0, 32, pens);
}

static uint16_t mix(uint16_t c, uint16_t to, int num, int den)
{
    int r = (c >> 8) & 15, g = (c >> 4) & 15, b = c & 15;
    r += ((((to >> 8) & 15) - r) * num) / den;
    g += ((((to >> 4) & 15) - g) * num) / den;
    b += (((to & 15) - b) * num) / den;
    return (uint16_t)((r << 8) | (g << 4) | b);
}

void plat_sky(int pen, uint16_t top, uint16_t bottom, int rows)
{
    int k, y;
    if (rows <= 0) { a3f_sky_pen = -1; a3f_sky_rows = 0; return; }
    if (rows > SCREEN_H) rows = SCREEN_H;
    for (k = 0; k < 16; k++) pens[32 + k] = mix(top, bottom, k, 15);
    fsys_rgb12(32, 16, pens + 32);
    for (y = 0; y < rows; y++) a3f_sky_col[y] = (uint8_t)(32 + (y * 16) / rows);
    a3f_sky_rows = rows;
    a3f_sky_pen = pen;
}

void plat_activate(void) {}

/* ---------- time ---------- */
int32_t plat_ticks(void) { return (int32_t)(fsys_hz200() >> 2); }
uint32_t plat_clock(void) { return fsys_clock(); }
int32_t plat_us_since(uint32_t t0) { return fsys_us_since(t0); }

/* ---------- sound ---------- */
void *a3_sound_alloc(int len) { return calloc(1, (size_t)len); }
void a3_sound_free(void *p, int len) { (void)len; free(p); }

void a3_sound_play(int8_t *data, int len, int period, int vol, int pri)
{
    static SfxStructure s[4];
    static int k;
    SfxStructure *x = &s[k++ & 3];      /* ptplayer reads it when its tick comes */
    if (!fsys_sound_on()) return;
    x->sfx_ptr = data;
    x->sfx_len = (WORD)(len / 2);
    x->sfx_per = (WORD)period;
    x->sfx_vol = (WORD)vol;
    x->sfx_cha = -1;
    x->sfx_pri = (BYTE)pri;
    mt_playfx(CUSTOM_BASE, x);
}

/* ---------- the main loop ---------- */
static int a3f_main(void)
{
    int running = 1, n, window10 = 0, cur = 1, i;
    long mod_size;

    ab_init(game_client);
    AB_I("%s starting (Falcon030)", game_title);
    ab_register_var("in_keys", AB_TYPE_I32, &plat_in_keys);
    ab_register_var("in_joy",  AB_TYPE_I32, &plat_in_joy);
    a3_draw_init();
    if (!fsys_screen_init(NBUF)) { AB_E("no memory for the screens"); return 1; }
    for (i = 0; i < NBUF; i++) { cvs[i].base = fsys_buffer(i); cvs[i].rowoff = fsys_rowoff(); cvs[i].h = SCREEN_H; }
    a3f_draw_pens();
    AB_I("SCREEN %ld rows, buffers 0x%06lx 0x%06lx 0x%06lx", (long)fsys_height(), (unsigned long)cvs[0].base,
         (unsigned long)cvs[1].base, (unsigned long)cvs[2].base);
    fsys_input_init();
    game_init();

    if (!fsys_sound_init()) AB_W("no DMA sound - continuing without sound");
    mt_musicmask(CUSTOM_BASE, 0x03);          /* effects never take the melody or bass */
    if ((mod_size = fsys_music(game_music, 2))) AB_I("music loaded (%ld bytes)", mod_size);
    else if (game_music[0]) AB_W("%s not found - no music", game_music);
    game_sound_init();
    n = a3_voice_load(game_voice);
    if (game_voice[0]) AB_I(n ? "voice: %ld clips" : "no voice clips", (long)n);

    while (running) {
        unsigned in, joy, held, pressed;
        uint32_t t0;

        pressed = fsys_keys(game_key_bit, &held);
        joy = fsys_joy();                 /* FSYS_JOY_* are A3_JOY_* */
        /* every input bit seen over the last 10 s, as on the Amigas */
        if (++window10 > 250) { window10 = 0; plat_in_keys = plat_in_joy = 0; }
        plat_in_joy |= (int32_t)joy;
        plat_in_keys |= (int32_t)(held | pressed);
        in = held | pressed | game_joy_bits(joy);
        if (!game_input(in)) running = 0;
        game_update(in);
        t0 = plat_clock();
        game_draw(&cvs[cur], cur);
        t0 = plat_clock() - t0;
        game_after_frame();
        fsys_pace(t0);
        /* with three buffers the next one to draw is neither shown nor
         * queued, so this never waits */
        fsys_show(cur);
        cur = cur == NBUF - 1 ? 0 : cur + 1;
    }
    AB_I("%s shutting down", game_title);
    fsys_sound_exit();
    a3_sfx_cleanup();
    fsys_input_exit();
    fsys_screen_exit();
    ab_cleanup();
    return 0;
}

int main(void)
{
    long old_ssp;
    int rc;
    nf_init();
    old_ssp = Super(0L);
    rc = a3f_main();
    Super((void *)old_ssp);
    return rc;
}
