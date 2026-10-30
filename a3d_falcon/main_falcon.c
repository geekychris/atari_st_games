// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * main_falcon.c - the shared amiga3d engine's Atari Falcon030 main, the
 * counterpart of main_68k.c (classic Amiga) and main_os4.c (AmigaOS 4):
 *
 *  - the screen: 320 x 240 in 256 colours on VGA (320 x 200 on RGB/TV),
 *    triple buffered (drawing never waits for the display); the games'
 *    320 x 256 drawing goes through a row map (a3d_draw.c), so one row in
 *    16 is dropped;
 *  - the palette: the games' 32 pens, and for plat_sky 16 more pens shaded
 *    from the top colour to the bottom one over its rows (the copper's
 *    16 steps on the classic Amiga);
 *  - input: the IKBD (../st_port/st_ikbd) for the keys, mapped to the
 *    Amiga raw key codes the games' game_key_bit expects, and joystick 1;
 *  - sound: ptplayer (../st_port/ptplayer, C) on the Paula emulation
 *    (../st_port/paula, Falcon DMA sound) for the music and the effects,
 *    which never take the music's two channels;
 *  - time: plat_ticks from the 200 Hz system timer, plat_clock from MFP
 *    timer C's counter (26 us steps); at most 25 frames a second as on
 *    the Amigas (the games run their simulations from plat_ticks, so they
 *    keep to time when the drawing is slower).
 *
 * The music and voice files ("PROGDIR:x.mod") are read from DATA\ beside
 * the program under their 8.3 names (a3f_path). Every 5 s a PERF line (frames a second, the drawing's share) goes to
 * the host through NatFeats; the games' own variables and hooks are
 * reached through bridge.c's mailbox.
 */
#include <osbind.h>
#include <mint/falcon.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bridge_client.h"
#include "a3d_plat.h"
#include "a3d_sound.h"
#include "a3d_falcon.h"
#include "natfeats.h"
#include "st_ikbd.h"
#include "ptplayer.h"
#include "paula.h"

#define CUSTOM_BASE ((void *)&custom)
#define HZ200   (*(volatile long *)0x4ba)
#define MFP_TCDR (*(volatile uint8_t *)0xFFFA23)

#define ROW_BYTES SCREEN_W              /* 8 planes x 20 groups x 2 bytes */

#define NBUF 3                          /* shown, queued for the next VBL, drawn */

static void *scr_mem;
static uint8_t *scr[NBUF];
static int32_t rowoff[SCREEN_H];
static Canvas cvs[NBUF];
static int cur, height;
static void *old_phys, *old_log;
static WORD old_mode;
static uint8_t *mod_data;
static int sound_on;
int32_t plat_in_keys, plat_in_joy;

/* ---------- the display ---------- */
static int setup_display(void)
{
    WORD mode;
    long size;
    int y;
    if (VgetMonitor() == MON_VGA) mode = BPS8 | COL40 | VGA | VERTFLAG;   /* 320 x 240 */
    else mode = BPS8 | COL40 | PAL;                                       /* 320 x 200 */
    size = VgetSize(mode);
    height = (int)(size / ROW_BYTES);
    if (height > 240) height = 240;
    scr_mem = (void *)Mxalloc(NBUF * size + 256, 0);    /* ST RAM: the video sees only that */
    if (!scr_mem || (long)scr_mem < 0) return 0;
    scr[0] = (uint8_t *)(((uint32_t)scr_mem + 255) & ~255UL);
    for (y = 1; y < NBUF; y++) scr[y] = scr[y - 1] + size;
    memset(scr[0], 0, (size_t)(NBUF * size));
    /* the games' rows to the screen's: of the rows that land on the same
     * screen row only the last is drawn */
    for (y = 0; y < SCREEN_H; y++) {
        int py = (y * height) / SCREEN_H, next = ((y + 1) * height) / SCREEN_H;
        rowoff[y] = (y < SCREEN_H - 1 && next == py) ? -1 : (int32_t)py * ROW_BYTES;
    }
    for (y = 0; y < NBUF; y++) { cvs[y].base = scr[y]; cvs[y].rowoff = rowoff; cvs[y].h = SCREEN_H; }
    old_phys = Physbase();
    old_log = Logbase();
    old_mode = VsetMode(-1);
    VsetScreen(scr[0], scr[0], 3, mode);
    cur = 1;
    a3f_draw_pens();
    return 1;
}

static void cleanup_display(void)
{
    if (!scr_mem) return;
    VsetScreen(old_log, old_phys, 3, old_mode);
    Mfree(scr_mem);
    scr_mem = NULL;
}

/* the screen just drawn is shown from the next VBL; with three, the next
 * one to draw is neither shown nor queued, so this never waits (at most
 * 25 frames a second leaves the display a VBL to take the address) */
static void swap_buffers(void)
{
    VsetScreen((void *)-1L, scr[cur], -1, -1);
    cur = cur == NBUF - 1 ? 0 : cur + 1;
}

/* ---------- the palette ---------- */
static uint16_t pens[48];

static void load_pens(int first, int n)
{
    long rgb[48];
    int i;
    for (i = 0; i < n; i++) {
        uint16_t c = pens[first + i];
        rgb[i] = (long)(((c >> 8) & 15) * 17) << 16 | (long)(((c >> 4) & 15) * 17) << 8 | (long)((c & 15) * 17);
    }
    VsetRGB(first, n, rgb);
}

void plat_palette(const uint16_t pal[32])
{
    memcpy(pens, pal, 32 * sizeof(uint16_t));
    load_pens(0, 32);
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
    load_pens(32, 16);
    for (y = 0; y < rows; y++) a3f_sky_col[y] = (uint8_t)(32 + (y * 16) / rows);
    a3f_sky_rows = rows;
    a3f_sky_pen = pen;
}

void plat_activate(void) {}

/* ---------- time ---------- */
int32_t plat_ticks(void) { return (int32_t)(HZ200 >> 2); }

/* 1/38400 s: the 200 Hz count and timer C's counter (192 steps a tick) */
uint32_t plat_clock(void)
{
    long h;
    int t;
    do { h = HZ200; t = MFP_TCDR; } while (h != HZ200);
    return (uint32_t)h * 192 + (uint32_t)(192 - t);
}

int32_t plat_us_since(uint32_t t0)
{
    return (int32_t)(((plat_clock() - t0) * 625UL) / 24);
}

/* ---------- files ---------- */
void a3f_path(const char *amiga, char *out)
{
    const char *p = strrchr(amiga, ':'), *s, *dot;
    int n = 5, k;
    memcpy(out, "DATA\\", 5);
    p = p ? p + 1 : amiga;
    if ((s = strrchr(p, '/'))) p = s + 1;
    dot = strrchr(p, '.');
    for (s = p, k = 0; *s && s != dot && k < 8; s++, k++) out[n++] = (char)(*s >= 'a' && *s <= 'z' ? *s - 32 : *s);
    if (dot) {
        out[n++] = '.';
        for (s = dot + 1, k = 0; *s && k < 3; s++, k++) out[n++] = (char)(*s >= 'a' && *s <= 'z' ? *s - 32 : *s);
    }
    out[n] = 0;
}

static uint8_t *load_file(const char *path, long *size)
{
    char name[18];
    long fh, len;
    uint8_t *buf;
    if (!path[0]) return NULL;
    a3f_path(path, name);
    fh = Fopen(name, 0);
    if (fh < 0) return NULL;
    len = Fseek(0, (short)fh, 2);
    Fseek(0, (short)fh, 0);
    buf = len > 0 ? (uint8_t *)malloc((size_t)len) : NULL;
    if (buf && Fread((short)fh, len, buf) != len) { free(buf); buf = NULL; }
    Fclose((short)fh);
    *size = len;
    return buf;
}

/* ---------- sound ---------- */
void *a3_sound_alloc(int len) { return calloc(1, (size_t)len); }
void a3_sound_free(void *p, int len) { (void)len; free(p); }

void a3_sound_play(int8_t *data, int len, int period, int vol, int pri)
{
    static SfxStructure s[4];
    static int k;
    SfxStructure *x = &s[k++ & 3];
    if (!sound_on) return;
    x->sfx_ptr = data;
    x->sfx_len = (WORD)(len / 2);
    x->sfx_per = (WORD)period;
    x->sfx_vol = (WORD)vol;
    x->sfx_cha = -1;
    x->sfx_pri = (BYTE)pri;
    mt_playfx(CUSTOM_BASE, x);
}

/* ---------- input ---------- */
/* ST scancode -> Amiga raw key code (0xFF: none) */
static uint8_t st2amiga[128];

static void key_table(void)
{
    static const uint8_t map[][2] = {
        { 0x01, 0x45 },                                         /* Esc */
        { 0x02, 0x01 }, { 0x03, 0x02 }, { 0x04, 0x03 }, { 0x05, 0x04 }, { 0x06, 0x05 },
        { 0x07, 0x06 }, { 0x08, 0x07 }, { 0x09, 0x08 }, { 0x0A, 0x09 }, { 0x0B, 0x0A },   /* 1-0 */
        { 0x0C, 0x0B }, { 0x0D, 0x0C }, { 0x0E, 0x41 }, { 0x0F, 0x42 },   /* - = backspace tab */
        { 0x10, 0x10 }, { 0x11, 0x11 }, { 0x12, 0x12 }, { 0x13, 0x13 }, { 0x14, 0x14 },
        { 0x15, 0x15 }, { 0x16, 0x16 }, { 0x17, 0x17 }, { 0x18, 0x18 }, { 0x19, 0x19 },   /* Q-P */
        { 0x1A, 0x1A }, { 0x1B, 0x1B }, { 0x1C, 0x44 }, { 0x1D, 0x63 },   /* [ ] return ctrl */
        { 0x1E, 0x20 }, { 0x1F, 0x21 }, { 0x20, 0x22 }, { 0x21, 0x23 }, { 0x22, 0x24 },
        { 0x23, 0x25 }, { 0x24, 0x26 }, { 0x25, 0x27 }, { 0x26, 0x28 },   /* A-L */
        { 0x27, 0x29 }, { 0x28, 0x2A }, { 0x29, 0x00 }, { 0x2A, 0x60 }, { 0x2B, 0x0D },   /* ; ' ` lshift \ */
        { 0x2C, 0x31 }, { 0x2D, 0x32 }, { 0x2E, 0x33 }, { 0x2F, 0x34 }, { 0x30, 0x35 },
        { 0x31, 0x36 }, { 0x32, 0x37 },                         /* Z-M */
        { 0x33, 0x38 }, { 0x34, 0x39 }, { 0x35, 0x3A }, { 0x36, 0x61 },   /* , . / rshift */
        { 0x38, 0x64 }, { 0x39, 0x40 }, { 0x3A, 0x62 },         /* alt space caps */
        { 0x3B, 0x50 }, { 0x3C, 0x51 }, { 0x3D, 0x52 }, { 0x3E, 0x53 }, { 0x3F, 0x54 },
        { 0x40, 0x55 }, { 0x41, 0x56 }, { 0x42, 0x57 }, { 0x43, 0x58 }, { 0x44, 0x59 },   /* F1-F10 */
        { 0x48, 0x4C }, { 0x50, 0x4D }, { 0x4D, 0x4E }, { 0x4B, 0x4F },   /* cursor up down right left */
        { 0x53, 0x46 }, { 0x62, 0x5F },                         /* delete, help */
        { 0x4A, 0x4A }, { 0x4E, 0x5E }, { 0x72, 0x43 }, { 0x71, 0x3C },   /* keypad - + enter . */
        { 0x70, 0x0F }, { 0x6D, 0x1D }, { 0x6E, 0x1E }, { 0x6F, 0x1F }, { 0x6A, 0x2D },
        { 0x6B, 0x2E }, { 0x6C, 0x2F }, { 0x67, 0x3D }, { 0x68, 0x3E }, { 0x69, 0x3F },   /* keypad 0-9 */
    };
    unsigned i;
    memset(st2amiga, 0xFF, sizeof(st2amiga));
    for (i = 0; i < sizeof(map) / sizeof(map[0]); i++) st2amiga[map[i][0]] = map[i][1];
}

static unsigned read_input(unsigned *held)
{
    unsigned keys = 0, pressed = 0;
    int sc;
    for (sc = 1; sc < 128; sc++) {
        unsigned b;
        if (st2amiga[sc] == 0xFF) continue;
        b = game_key_bit(st2amiga[sc]);
        if (ikbd_key_hit(sc) && b) pressed |= b;       /* seen even if released within the frame */
        if (ikbd_keys[sc]) keys |= b;
    }
    *held = keys;
    return pressed;
}

static unsigned read_joystick(void)
{
    UBYTE j = ikbd_joy1();
    return (j & 1 ? A3_JOY_UP : 0) | (j & 2 ? A3_JOY_DOWN : 0) | (j & 4 ? A3_JOY_LEFT : 0) |
           (j & 8 ? A3_JOY_RIGHT : 0) | (j & 0x80 ? A3_JOY_FIRE : 0);
}

/* ---------- the main loop ---------- */
static int a3f_main(void)
{
    int running = 1, n, window10 = 0;
    long mod_size = 0, frame_t, perf_t, perf_frames = 0;
    uint32_t perf_draw = 0;

    ab_init(game_client);
    AB_I("%s starting (Falcon030)", game_title);
    ab_register_var("in_keys", AB_TYPE_I32, &plat_in_keys);
    ab_register_var("in_joy",  AB_TYPE_I32, &plat_in_joy);
    a3_draw_init();
    key_table();
    if (!setup_display()) { AB_E("no memory for the screens"); return 1; }
    AB_I("SCREEN %ld rows, buffers 0x%06lx 0x%06lx 0x%06lx", (long)height, (unsigned long)scr[0], (unsigned long)scr[1], (unsigned long)scr[2]);
    ikbd_init(IKBD_JOYSTICK);
    game_init();

    mt_install_cia(CUSTOM_BASE, NULL, 1);
    sound_on = mt_sound_ok() == 0;
    if (!sound_on) AB_W("no DMA sound - continuing without sound");
    mt_musicmask(CUSTOM_BASE, 0x03);          /* effects never take the melody or bass */
    mod_data = sound_on ? load_file(game_music, &mod_size) : NULL;
    if (mod_data) {
        mt_init(CUSTOM_BASE, mod_data, NULL, 0);
        mt_MusicChannels = 2;
        mt_Enable = 1;
        AB_I("music loaded (%ld bytes)", (long)mod_size);
    } else if (game_music[0]) {
        AB_W("%s not found - no music", game_music);
    }
    game_sound_init();
    n = a3_voice_load(game_voice);
    if (game_voice[0]) AB_I(n ? "voice: %ld clips" : "no voice clips", (long)n);

    frame_t = perf_t = HZ200;
    while (running) {
        unsigned in, joy, held, pressed;
        uint32_t t0;

        pressed = read_input(&held);
        joy = read_joystick();
        /* every input bit seen over the last 10 s, as on the Amigas */
        if (++window10 > 250) { window10 = 0; plat_in_keys = plat_in_joy = 0; }
        plat_in_joy |= (int32_t)joy;
        plat_in_keys |= (int32_t)(held | pressed);
        in = held | pressed | game_joy_bits(joy);
        if (!game_input(in)) running = 0;
        game_update(in);
        t0 = plat_clock();
        game_draw(&cvs[cur], cur);
        perf_draw += plat_clock() - t0;
        game_after_frame();
        /* at most 25 frames a second */
        while (HZ200 - frame_t < 8) ;
        frame_t = HZ200 - frame_t > 16 ? HZ200 : frame_t + 8;
        swap_buffers();

        perf_frames++;
        if (HZ200 - perf_t >= 1000) {
            long dt = HZ200 - perf_t;
            AB_I("PERF fps %ld.%ld draw %ld%%", (long)(perf_frames * 200 / dt), (long)((perf_frames * 2000 / dt) % 10),
                 (long)((perf_draw / 192) * 100 / dt));
            perf_t = HZ200;
            perf_frames = 0;
            perf_draw = 0;
        }
    }
    AB_I("%s shutting down", game_title);
    if (sound_on) mt_end(CUSTOM_BASE);
    mt_remove_cia(CUSTOM_BASE);
    free(mod_data);
    a3_sfx_cleanup();
    ikbd_exit();
    cleanup_display();
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
