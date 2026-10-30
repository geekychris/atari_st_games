// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * falcon_sys.c - the Falcon030 machine layer the Amiga ports share (see
 * falcon_sys.h): used by the amiga3d engine's main_falcon.c and by Terra
 * Strike's.
 */
#include <osbind.h>
#include <mint/falcon.h>
#include <stdlib.h>
#include <string.h>

#include "falcon_sys.h"
#include "bridge_client.h"
#include "st_ikbd.h"
#include "ptplayer.h"
#include "paula.h"

#define HZ200    (*(volatile long *)0x4ba)
#define VBCLOCK  (*(volatile long *)0x462)
#define MFP_TCDR (*(volatile uint8_t *)0xFFFA23)
#define CUSTOM_BASE ((void *)&custom)

/* ---------- the screen ---------- */
static void *scr_mem;
static uint8_t *scr[FSYS_MAXBUF];
static int32_t rowoff[FSYS_ROWS];
static int height, nbufs;
static long shown_vbl;
static void *old_phys, *old_log;
static WORD old_mode;
uint32_t a3f_pat[48][3];

int fsys_screen_init(int nbuf)
{
    WORD mode;
    long size;
    int y, c, p;
    if (VgetMonitor() == MON_VGA) mode = BPS8 | COL40 | VGA | VERTFLAG;   /* 320 x 240 */
    else mode = BPS8 | COL40 | PAL;                                       /* 320 x 200 */
    size = VgetSize(mode);
    height = (int)(size / FSYS_W);
    if (height > 240) height = 240;
    nbufs = nbuf < 2 ? 2 : nbuf > FSYS_MAXBUF ? FSYS_MAXBUF : nbuf;
    scr_mem = (void *)Mxalloc(nbufs * size + 256, 0);   /* ST RAM: the video sees only that */
    if (!scr_mem || (long)scr_mem < 0) { scr_mem = NULL; return 0; }
    scr[0] = (uint8_t *)(((uint32_t)scr_mem + 255) & ~255UL);
    for (y = 1; y < nbufs; y++) scr[y] = scr[y - 1] + size;
    memset(scr[0], 0, (size_t)(nbufs * size));
    /* the games' rows to the screen's: of the rows that land on the same
     * screen row only the last is drawn */
    for (y = 0; y < FSYS_ROWS; y++) {
        int py = (y * height) / FSYS_ROWS, next = ((y + 1) * height) / FSYS_ROWS;
        rowoff[y] = (y < FSYS_ROWS - 1 && next == py) ? -1 : (int32_t)py * FSYS_W;
    }
    for (c = 0; c < 48; c++)
        for (p = 0; p < 3; p++)
            a3f_pat[c][p] = ((c >> (2 * p)) & 1 ? 0xFFFF0000UL : 0) | ((c >> (2 * p + 1)) & 1 ? 0xFFFFUL : 0);
    old_phys = Physbase();
    old_log = Logbase();
    old_mode = VsetMode(-1);
    VsetScreen(scr[0], scr[0], 3, mode);
    shown_vbl = VBCLOCK - 1;
    return 1;
}

void fsys_screen_exit(void)
{
    if (!scr_mem) return;
    VsetScreen(old_log, old_phys, 3, old_mode);
    Mfree(scr_mem);
    scr_mem = NULL;
}

uint8_t *fsys_buffer(int i) { return scr[i]; }
const int32_t *fsys_rowoff(void) { return rowoff; }
int fsys_height(void) { return height; }

void fsys_show(int i)
{
    VsetScreen((void *)-1L, scr[i], -1, -1);
    shown_vbl = VBCLOCK;
}

void fsys_wait_shown(void)
{
    while (VBCLOCK == shown_vbl) ;
}

void fsys_rgb12(int first, int n, const uint16_t *pal)
{
    long rgb[48];
    int i;
    if (n > 48) n = 48;
    for (i = 0; i < n; i++) {
        uint16_t c = pal[i];
        rgb[i] = (long)(((c >> 8) & 15) * 17) << 16 | (long)(((c >> 4) & 15) * 17) << 8 | (long)((c & 15) * 17);
    }
    VsetRGB(first, n, rgb);
}

void fsys_rect(uint8_t *base, const int32_t *ro, int x0, int y0, int x1, int y1, int col)
{
    static int16_t rl[FSYS_ROWS], rr[FSYS_ROWS];
    static int lx0 = -1, lx1 = -1, ln;
    int n = y1 - y0 + 1, y;
    if (x0 < 0) x0 = 0;
    if (x1 > FSYS_W - 1) x1 = FSYS_W - 1;
    if (n <= 0 || x0 > x1) return;
    if (n > FSYS_ROWS) n = FSYS_ROWS;
    if (x0 != lx0 || x1 != lx1 || n > ln) {
        for (y = 0; y < n; y++) { rl[y] = (int16_t)x0; rr[y] = (int16_t)x1; }
        lx0 = x0; lx1 = x1; ln = n;
    }
    a3f_rows(rl, rr, n, ro + y0, base, a3f_pat[col < 48 ? col : col & 31]);
}

/* ---------- text ---------- */
uint8_t a3f_glyph[256][8];

void fsys_font_init(void)
{
    register void *fonts __asm__("a1");
    const uint8_t *f8, *data;
    int first, last, width, c, r;
    __asm__ volatile (".dc.w 0xa000" : "=r"(fonts) : : "d0", "d1", "d2", "a0", "a2", "memory");
    f8 = ((const uint8_t **)fonts)[1];  /* the 8 x 8 font header */
    first = *(const uint16_t *)(f8 + 36);
    last = *(const uint16_t *)(f8 + 38);
    data = *(const uint8_t *const *)(f8 + 76);
    width = *(const uint16_t *)(f8 + 80);
    for (c = first; c <= last && c < 256; c++)
        for (r = 0; r < 8; r++) a3f_glyph[c][r] = data[r * width + (c - first)];
}

static inline void group_mask(uint32_t *d, const uint32_t *pt, uint32_t mm)
{
    d[0] = (d[0] & ~mm) | (pt[0] & mm);
    d[1] = (d[1] & ~mm) | (pt[1] & mm);
    d[2] = (d[2] & ~mm) | (pt[2] & mm);
}

void fsys_text(uint8_t *base, const int32_t *ro, int x, int top, int col, const char *s, int y0, int y1)
{
    const uint32_t *pt = a3f_pat[col < 48 ? col : col & 31];
    for (; *s; s++, x += 8) {
        int r, wl = x >> 4, sh = x & 15;
        if (x < 0 || x > FSYS_W - 8) continue;
        for (r = 0; r < 8; r++) {
            int yy = top + r;
            uint32_t m = (uint32_t)a3f_glyph[(unsigned char)*s][r] << (24 - sh), *d;
            int32_t off;
            if (!m || yy < y0 || yy > y1 || yy < 0 || yy >= FSYS_ROWS) continue;
            if ((off = ro[yy]) < 0) off = ro[yy + 1];
            d = (uint32_t *)(base + off + wl * 16);
            group_mask(d, pt, (m & 0xFFFF0000UL) | m >> 16);
            if (m & 0xFFFF) group_mask(d + 4, pt, (m & 0xFFFF) << 16 | (m & 0xFFFF));
        }
    }
}

/* ---------- input ---------- */
/* ST scancode -> Amiga raw key code (0xFF: none) */
static uint8_t st2amiga[128];

void fsys_input_init(void)
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
    ikbd_init(IKBD_JOYSTICK);
}

void fsys_input_exit(void) { ikbd_exit(); }

unsigned fsys_keys(unsigned (*key_bit)(unsigned), unsigned *held)
{
    unsigned keys = 0, pressed = 0;
    int sc;
    for (sc = 1; sc < 128; sc++) {
        unsigned b;
        if (st2amiga[sc] == 0xFF) continue;
        b = key_bit(st2amiga[sc]);
        if (ikbd_key_hit(sc) && b) pressed |= b;
        if (ikbd_keys[sc]) keys |= b;
    }
    *held = keys;
    return pressed;
}

unsigned fsys_joy(void)
{
    UBYTE j = ikbd_joy1();
    return (j & 1 ? FSYS_JOY_UP : 0) | (j & 2 ? FSYS_JOY_DOWN : 0) | (j & 4 ? FSYS_JOY_LEFT : 0) |
           (j & 8 ? FSYS_JOY_RIGHT : 0) | (j & 0x80 ? FSYS_JOY_FIRE : 0);
}

/* ---------- time ---------- */
long fsys_hz200(void) { return HZ200; }

/* the 200 Hz count and timer C's counter (192 steps a tick) */
uint32_t fsys_clock(void)
{
    long h;
    int t;
    do { h = HZ200; t = MFP_TCDR; } while (h != HZ200);
    return (uint32_t)h * 192 + (uint32_t)(192 - t);
}

int32_t fsys_us_since(uint32_t t0)
{
    return (int32_t)(((fsys_clock() - t0) * 625UL) / 24);
}

void fsys_pace(uint32_t draw_ticks)
{
    static long frame_t, perf_t, perf_frames;
    static uint32_t perf_draw;
    if (!frame_t) frame_t = perf_t = HZ200;
    while (HZ200 - frame_t < 8) ;
    frame_t = HZ200 - frame_t > 16 ? HZ200 : frame_t + 8;
    perf_frames++;
    perf_draw += draw_ticks;
    if (HZ200 - perf_t >= 1000) {
        long dt = HZ200 - perf_t;
        AB_I("PERF fps %ld.%ld draw %ld%%", (long)(perf_frames * 200 / dt), (long)((perf_frames * 2000 / dt) % 10),
             (long)((perf_draw / 192) * 100 / dt));
        perf_t = HZ200;
        perf_frames = 0;
        perf_draw = 0;
    }
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

uint8_t *fsys_load(const char *amiga, long *size)
{
    char name[18];
    long fh, len;
    uint8_t *buf;
    if (!amiga || !amiga[0]) return NULL;
    a3f_path(amiga, name);
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
static int sound_on;
static uint8_t *mod_data;

int fsys_sound_init(void)
{
    mt_install_cia(CUSTOM_BASE, NULL, 1);
    sound_on = mt_sound_ok() == 0;
    return sound_on;
}

int fsys_sound_on(void) { return sound_on; }

long fsys_music(const char *amiga, int music_channels)
{
    long size = 0;
    if (!sound_on || !(mod_data = fsys_load(amiga, &size))) return 0;
    mt_init(CUSTOM_BASE, mod_data, NULL, 0);
    mt_MusicChannels = (UBYTE)music_channels;
    mt_Enable = 1;
    return size;
}

void fsys_sound_exit(void)
{
    if (sound_on) mt_end(CUSTOM_BASE);
    mt_remove_cia(CUSTOM_BASE);
    free(mod_data);
    mod_data = NULL;
}
