/*
 * ROLLING STEEL - classic Amiga (68020+, AmigaOS 3.x) version, from the 3DO
 * version (3do-dev/projects/rolling_steel) like the AmigaOS 4 one.
 *
 * Same game code and the same renderer as the AmigaOS 4 version (render.c,
 * the 3DO one with each corner's depth), its faces drawn by the CPU with a
 * depth buffer (glcels_soft.c on softcel.c), and the 3DO's sound code on the real
 * Paula (sound_paula.c). The frame goes to an RTG window or screen, or to
 * an AGA screen (amiga68k.c).
 *
 * Player 1: arrows push, Z / X turn the view, = / - zoom, C + up/down
 * tilt, P pause, Space start. Player 2: W A S D, Q / E, Tab / 1.
 * F or F10 switches window / full screen (RTG). Esc quits.
 *
 * Usage: rolling_steel_68k [SCALE=n] [FULLSCREEN] [AGA]
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <graphics/text.h>
#include <devices/inputevent.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "bridge_client.h"
#include "game.h"
#include "amiga68k.h"
#include "paula.h"
#include "glcels.h"

unsigned long __stack = 65536;

void snd_update(void);              /* sound_paula.c */
void snd_exit(void);
int  snd_available(void);

/* the 3DO pad bits game.c reads */
#define B_UP    0x0001
#define B_DOWN  0x0002
#define B_LEFT  0x0004
#define B_RIGHT 0x0008
#define B_A     0x0010
#define B_B     0x0020
#define B_C     0x0040
#define B_P     0x0080
#define B_L     0x0200
#define B_R     0x0400

enum { P_WHITE = 1, P_AMBER, P_CYAN, P_RED, P_GREEN, P_DIM, P_GOLD, P_SILVER, P_BRONZE, P_SHADOW,
       P_ORANGE, P_BAR, P_SEL, P_FAINT, P_COUNT };
static const ULONG pen_rgb[P_COUNT] = { 0, 0xF2F2F2, 0xFFC740, 0x73D9FF, 0xFF594D, 0x73FF80, 0xB3B3C0, 0xFFD140,
                                        0xD1DBEB, 0xD98C4D, 0x060608, 0xFF9E59, 0x404050, 0x30507A, 0x6A6A78 };

/* ---- what game.c / course.c need from the platform ---- */

void rs_log(const char *fmt, ...)
{
    char buf[200];
    int n;
    va_list ap;
    va_start(ap, fmt);
    vsprintf(buf, fmt, ap);
    va_end(ap);
    n = (int)strlen(buf);
    if (n > 0 && buf[n - 1] == '\n') buf[n - 1] = 0;
    AB_I("%s", buf);
}

/* data/<name> next to the program */
void *rs_load(const char *name, long *size) { return sys_load(name, size, 0); }
void rs_free(void *p) { sys_free(p); }
void *rs_alloc(long bytes) { return AllocVec(bytes, MEMF_ANY); }

#define PROG_FILE "PROGDIR:rollingsteel.prog"

void rs_save_progress(void)
{
    BPTR f = Open((CONST_STRPTR)PROG_FILE, MODE_NEWFILE);
    if (!f) return;
    Write(f, G.best, sizeof(G.best));
    Write(f, &G.best_run, 4);
    Write(f, &G.best_run_falls, 4);
    Close(f);
}

void rs_load_progress(void)
{
    BPTR f = Open((CONST_STRPTR)PROG_FILE, MODE_OLDFILE);
    if (!f) return;
    if (Read(f, G.best, sizeof(G.best)) != sizeof(G.best)) memset(G.best, 0, sizeof(G.best));
    Read(f, &G.best_run, 4);
    Read(f, &G.best_run_falls, 4);
    Close(f);
}

/* ---- drawing into the frame (fb: 320 x 240, 15-bit RGB) ---- */

static UWORD pen15[P_COUNT];

static void fill(int x0, int y0, int x1, int y1, int pen)    /* inclusive */
{
    UWORD c = pen15[pen];
    int x, y;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > SCREEN_W - 1) x1 = SCREEN_W - 1;
    if (y1 > SCREEN_H - 1) y1 = SCREEN_H - 1;
    if (x0 > x1 || y0 > y1) return;
    for (y = y0; y <= y1; y++) {
        UWORD *row = fb + (long)y * SCREEN_W;
        for (x = x0; x <= x1; x++) row[x] = c;
    }
}

static unsigned char glyph[256][8];                  /* topaz 8, read from the ROM font */

static int font_init(void)
{
    struct TextAttr ta = { (STRPTR)"topaz.font", 8, 0, 0 };
    struct TextFont *tf = OpenFont(&ta);
    int c;
    if (!tf) return 0;
    for (c = 32; c < 256; c++) {
        const ULONG *loc = (const ULONG *)tf->tf_CharLoc;
        const UBYTE *data = (const UBYTE *)tf->tf_CharData;
        int idx = c, off, w, r, b;
        if (idx < tf->tf_LoChar || idx > tf->tf_HiChar) idx = tf->tf_HiChar + 1;
        idx -= tf->tf_LoChar;
        off = (int)(loc[idx] >> 16);
        w = (int)(loc[idx] & 0xFFFF);
        if (w > 8) w = 8;
        for (r = 0; r < 8 && r < tf->tf_YSize; r++) {
            unsigned char bits = 0;
            for (b = 0; b < w; b++) {
                int bit = off + b;
                if (data[r * tf->tf_Modulo + (bit >> 3)] & (0x80 >> (bit & 7))) bits |= 0x80 >> b;
            }
            glyph[c][r] = bits;
        }
    }
    CloseFont(tf);
    return 1;
}

#ifdef __MINT__
/* Atari Falcon port: each glyph row straight into fb (a fill() call per
 * pixel was an eighth of a stock Falcon's frame) */
static void text_scaled(int x, int y, const char *s, int scale, int pen)
{
    UWORD c = pen15[pen];
    for (; *s; s++, x += 8 * scale) {
        const unsigned char *g = glyph[(unsigned char)*s];
        int r, b, k;
        if (x < 0 || x + 8 * scale > SCREEN_W || y < 0 || y + 8 * scale > SCREEN_H) {
            for (r = 0; r < 8; r++)                 /* at an edge: clipped by fill() */
                for (b = 0; b < 8; b++)
                    if (g[r] & (0x80 >> b))
                        fill(x + b * scale, y + r * scale, x + b * scale + scale - 1, y + r * scale + scale - 1, pen);
            continue;
        }
        for (r = 0; r < 8; r++) {
            unsigned int bits = g[r];
            UWORD *row = fb + (long)(y + r * scale) * SCREEN_W + x;
            if (!bits) continue;
            if (scale == 1) {
                for (b = 0; bits; b++, bits = (bits << 1) & 0xFF)
                    if (bits & 0x80) row[b] = c;
            } else
                for (k = 0; k < scale; k++, row += SCREEN_W)
                    for (b = 0; b < 8; b++)
                        if (bits & (0x80 >> b)) {
                            int i;
                            for (i = 0; i < scale; i++) row[b * scale + i] = c;
                        }
        }
    }
}
#else
static void text_scaled(int x, int y, const char *s, int scale, int pen)
{
    for (; *s; s++, x += 8 * scale) {
        const unsigned char *g = glyph[(unsigned char)*s];
        int r, b;
        for (r = 0; r < 8; r++)
            for (b = 0; b < 8; b++)
                if (g[r] & (0x80 >> b))
                    fill(x + b * scale, y + r * scale, x + b * scale + scale - 1, y + r * scale + scale - 1, pen);
    }
}
#endif

/* ---- the HUD: listed while the GL frame is drawn (its shaded boxes go
 * to GL there and then), drawn on top once GL has finished ---- */

enum { HI_TEXT, HI_BIG, HI_BAR };
typedef struct { short type, x, y, x1, y1, pen; char s[44]; } HudItem;
#define MAXHUD 48
static HudItem hl[MAXHUD];
static int nhud;

static HudItem *hud_add(int type, int x, int y, int pen, const char *s)
{
    HudItem *h;
    if (nhud >= MAXHUD) return 0;
    h = &hl[nhud++];
    h->type = (short)type; h->x = (short)x; h->y = (short)y; h->pen = (short)pen;
    h->s[0] = 0;
    if (s) { strncpy(h->s, s, sizeof(h->s) - 1); h->s[sizeof(h->s) - 1] = 0; }
    return h;
}

static void text(int x, int y, const char *s, int pen) { hud_add(HI_TEXT, x, y, pen, s); }
static void ctext(int cx, int y, const char *s, int pen) { text(cx - 4 * (int)strlen(s), y, s, pen); }
static void rtext(int rx, int y, const char *s, int pen) { text(rx - 8 * (int)strlen(s), y, s, pen); }
static void big(int cx, int y, const char *s, int pen) { hud_add(HI_BIG, cx - 8 * (int)strlen(s), y, pen, s); }

static void bar(int x0, int y0, int x1, int y1, int pen)
{
    HudItem *h = hud_add(HI_BAR, x0, y0, pen, 0);
    if (h) { h->x1 = (short)x1; h->y1 = (short)y1; }
}

static void hud_draw(void)
{
    int i;
    for (i = 0; i < nhud; i++) {
        const HudItem *h = &hl[i];
        switch (h->type) {
        case HI_TEXT:
            text_scaled(h->x + 1, h->y + 1, h->s, 1, P_SHADOW);
            text_scaled(h->x, h->y, h->s, 1, h->pen);
            break;
        case HI_BIG:
            text_scaled(h->x + 2, h->y + 2, h->s, 2, P_SHADOW);
            text_scaled(h->x, h->y, h->s, 2, h->pen);
            break;
        case HI_BAR:
            fill(h->x, h->y, h->x1, h->y1, h->pen);
            break;
        }
    }
    nhud = 0;
}

/* Progress.Format: mm:ss.s, or a dash */
static void fmt_time(char *buf, long steps)
{
    long t = steps * 2;                                     /* hundredths */
    if (steps <= 0) { strcpy(buf, "--:--"); return; }
    if (t >= 6000) sprintf(buf, "%ld:%02ld.%ld", t / 6000, (t % 6000) / 100, (t % 100) / 10);
    else sprintf(buf, "%ld.%ld", t / 100, (t % 100) / 10);
}

static const char *medal_name(int m) { return m == MEDAL_GOLD ? "GOLD" : m == MEDAL_SILVER ? "SILVER" : m == MEDAL_BRONZE ? "BRONZE" : ""; }
static int medal_pen(int m) { return m == MEDAL_GOLD ? P_GOLD : m == MEDAL_SILVER ? P_SILVER : m == MEDAL_BRONZE ? P_BRONZE : P_FAINT; }

static int blink(void) { return (G.clock / 18) % 3 != 2; }

static void banner(int x0, int w, const char *title, int pen, const char *sub)
{
    render_shade(x0, 92, x0 + w, 146, 4);
    big(x0 + w / 2, 100, title, pen);
    if (sub && sub[0]) ctext(x0 + w / 2, 128, sub, P_WHITE);
}

static void hud_player(int i, int x0, int w)
{
    Player *p = &G.p[i];
    char buf[48], t[16];
    int two = G.nplayers > 1;
    long pr;
    render_shade(x0, 0, x0 + w, 20, 4);
    if (two) sprintf(buf, "P%ld", (long)i + 1);
    else sprintf(buf, "%ld/%ld %s", (long)G.level + 1, (long)course_count, C.name);
    if (two && p->wins) sprintf(buf + strlen(buf), " WON %ld", (long)p->wins);
    text(x0 + 4, 6, buf, two ? (i ? P_ORANGE : P_CYAN) : P_CYAN);
    sprintf(t, "%ld.%ld", p->time_left / 50, (p->time_left % 50) / 5);
    if (two) ctext(x0 + w / 2 + 8, 6, t, p->time_left <= 500 ? P_RED : P_AMBER);
    else big(x0 + 200, 2, t, p->time_left <= 500 ? P_RED : P_AMBER);
    sprintf(buf, "FALLS %ld", (long)p->deaths);
    rtext(x0 + w - 4, 6, two ? buf + 6 : buf, P_AMBER);
    /* progress along the route */
    pr = course_progress(p);
    bar(x0 + w / 4, 23, x0 + w * 3 / 4 - 1, 24, P_BAR);
    if (pr > 0) bar(x0 + w / 4, 23, x0 + w / 4 + (int)(((w / 2 - 1) * pr) >> 16), 24, two && i ? P_ORANGE : P_CYAN);
    fmt_time(t, p->course_time);
    if (!two && G.best[G.level] > 0) {
        char b[16];
        fmt_time(b, G.best[G.level]);
        sprintf(buf, "%s   BEST %s", t, b);
    } else strcpy(buf, t);
    ctext(x0 + w / 2, 29, buf, P_DIM);
    if (p->dying) banner(x0, w, p->death_reason, P_RED, "-3 SECONDS");
    else if (p->out_of_time && G.state == ST_PLAYING) banner(x0, w, "OUT OF TIME", P_RED, "");
    else if (p->finished && G.state == ST_PLAYING) { fmt_time(t, p->finish_time); banner(x0, w, "FINISHED", P_GREEN, t); }
}

static void hud_title(void)
{
    char buf[48], t[16];
    int i;
    render_shade(0, 0, 320, 240, 5);
    big(160, 10, "ROLLING STEEL", P_AMBER);
    ctext(160, 32, "SIX COURSES, ONE CLOCK", P_CYAN);
    for (i = 0; i < course_count; i++) {
        static const char *names[6] = { "PRACTICE", "BEGINNER", "INTERMEDIATE", "AERIAL", "SILLY", "ULTIMATE" };
        int y = 50 + i * 12, sel = i == G.title_select;
        if (sel) bar(36, y - 2, 284, y + 8, P_SEL);
        sprintf(buf, "%ld  %s", (long)i + 1, names[i]);
        text(44, y, buf, sel ? P_WHITE : P_DIM);
        fmt_time(t, G.best[i]);
        rtext(222, y, t, G.best[i] > 0 ? P_AMBER : P_FAINT);
        if (G.best[i] > 0 && i == G.level) text(230, y, medal_name(medal_for(G.best[i])), medal_pen(medal_for(G.best[i])));
    }
    ctext(160, 128, G.want_players > 1 ? "TWO PLAYERS - ARROWS AND WASD" : "ONE PLAYER",
          G.want_players > 1 ? P_ORANGE : P_CYAN);
    if (G.best_run > 0) {
        fmt_time(t, G.best_run);
        sprintf(buf, "BEST FULL RUN %s (%ld FALLS)", t, G.best_run_falls);
        ctext(160, 140, buf, P_CYAN);
    }
    ctext(160, 156, "UP/DOWN COURSE  LEFT/RIGHT PLAYERS", P_DIM);
    ctext(160, 167, "Z/X TURN  +/- ZOOM  C+UP/DOWN TILT", P_FAINT);
    ctext(160, 178, G.music_on ? "C MUSIC: ON   P PAUSE" : "C MUSIC: OFF   P PAUSE", P_FAINT);
    if (blink()) big(160, 204, "PRESS SPACE", P_GREEN);
}

static void hud(void)
{
    char buf[64], t[16];
    int i;
    if (G.state == ST_TITLE) { hud_title(); return; }
    if (G.nplayers > 1) {
        for (i = 0; i < 2; i++) hud_player(i, i * 160, 160);
        bar(159, 0, 160, 239, P_SHADOW);
    } else hud_player(0, 0, 320);
    if (G.state == ST_CLEAR) {
        fmt_time(t, G.last_course_time);
        if (G.nplayers > 1) {
            sprintf(buf, "%s - P1 %ld - %ld P2", t, (long)G.p[0].wins, (long)G.p[1].wins);
            banner(0, 320, G.last_winner >= 0 ? (G.last_winner ? "P2 TAKES IT" : "P1 TAKES IT") : "COURSE CLEAR",
                   G.last_winner == 1 ? P_ORANGE : G.last_winner == 0 ? P_CYAN : P_GREEN, buf);
        } else {
            sprintf(buf, "%s %s%s", t, medal_name(G.last_medal), G.last_was_best ? " - NEW BEST" : "");
            banner(0, 320, "COURSE CLEAR", G.last_medal ? medal_pen(G.last_medal) : P_GREEN, buf);
        }
    } else if (G.state == ST_WON) {
        if (G.nplayers > 1) {
            const char *who = G.p[0].wins == G.p[1].wins ? "A DRAW" : G.p[0].wins > G.p[1].wins ? "P1 WINS" : "P2 WINS";
            sprintf(buf, "P1 %ld - %ld P2 - PRESS SPACE", (long)G.p[0].wins, (long)G.p[1].wins);
            banner(0, 320, who, G.p[0].wins >= G.p[1].wins ? P_CYAN : P_ORANGE, buf);
        } else {
            fmt_time(t, G.p[0].run_time);
            sprintf(buf, "%s FALLS %ld%s", t, (long)G.p[0].deaths, G.last_was_best ? " - NEW BEST RUN" : "");
            banner(0, 320, "ALL CLEAR", P_GREEN, buf);
        }
    } else if (G.state == ST_GAMEOVER)
        banner(0, 320, "OUT OF TIME", P_RED, "PRESS SPACE TO TRY AGAIN");
    if (G.paused) banner(0, 320, "PAUSED", P_WHITE, "P TO CARRY ON");
    if (G.demo && blink()) ctext(160, 226, "DEMO - PRESS SPACE", P_DIM);
}

/* ---- the keyboard as two pads ---- */

static ULONG held[2], pressed[2], inj_held[2], inj_pressed[2];
static int inj_frames[2], quit;
static const char *quit_why = "";
static ULONG joy_held;                   /* the joystick's bits (sys_joystick) */

static ULONG key_bit(UWORD code, int *pad)
{
    *pad = 0;
    switch (code) {
    case 0x4C: case 0x3E: return B_UP;              /* cursor keys, keypad 8 4 6 2 */
    case 0x4D: case 0x1E: return B_DOWN;
    case 0x4F: case 0x2D: return B_LEFT;
    case 0x4E: case 0x2F: return B_RIGHT;
    case 0x40: case 0x44: case 0x43: return B_A;    /* space, return, enter */
    case 0x0C: case 0x5E: return B_A;               /* = and keypad +: zoom in */
    case 0x0B: case 0x4A: return B_B;               /* - and keypad -: zoom out */
    case 0x31: return B_L;                          /* Z */
    case 0x32: return B_R;                          /* X */
    case 0x33: return B_C;                          /* C */
    case 0x19: return B_P;                          /* P */
    }
    *pad = 1;
    switch (code) {
    case 0x11: return B_UP;                         /* W A S D */
    case 0x21: return B_DOWN;
    case 0x20: return B_LEFT;
    case 0x22: return B_RIGHT;
    case 0x10: return B_L;                          /* Q */
    case 0x12: return B_R;                          /* E */
    case 0x42: return B_A;                          /* Tab */
    case 0x01: return B_B;                          /* 1 */
    }
    return 0;
}

/* bridge hooks: "press UP" holds pad 1's button for a few frames, press2 pad 2 */
static int press(int pad, const char *args, char *res, int len)
{
    static const struct { const char *n; ULONG b; } names[] = {
        { "UP", B_UP }, { "DOWN", B_DOWN }, { "LEFT", B_LEFT }, { "RIGHT", B_RIGHT }, { "A", B_A },
        { "B", B_B }, { "C", B_C }, { "P", B_P }, { "L", B_L }, { "R", B_R }
    };
    unsigned i;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (args && strcmp(args, names[i].n) == 0) {
            inj_held[pad] = names[i].b;
            inj_pressed[pad] |= names[i].b;
            inj_frames[pad] = 8;
            snprintf(res, len, "pad %ld: %s", (long)pad + 1, names[i].n);
            return 0;
        }
    snprintf(res, len, "unknown button");
    return -1;
}
static int hk_press(const char *a, char *r, int n) { return press(0, a, r, n); }
static int hk_press2(const char *a, char *r, int n) { return press(1, a, r, n); }
static int hk_quit(const char *a, char *r, int n) { (void)a; snprintf(r, n, "quitting"); quit = 1; quit_why = "bridge"; return 0; }



/* ---- main ---- */

int main(int argc, char **argv)
{
    int scale = 2, mode = SYS_AUTO, bridge, rc = 0, i;
    long frames = 0, fps_frames = 0, fps10 = 0, steps_done = 0, quads = 0, prims = 0;
    long cur_state = 0, cur_level = 0, time_left = 0, falls = 0;
    unsigned long long t_last, t_fps, acc = 0, prof[4] = { 0, 0, 0, 0 };

    for (i = 1; i < argc; i++) {
        if (strncmp(argv[i], "SCALE=", 6) == 0 || strncmp(argv[i], "scale=", 6) == 0) scale = atoi(argv[i] + 6);
        else if (!strcmp(argv[i], "FULLSCREEN") || !strcmp(argv[i], "fullscreen")) mode = SYS_RTG_SCREEN;
        else if (!strcmp(argv[i], "AGA") || !strcmp(argv[i], "aga")) mode = SYS_AGA;
        else if (strcmp(argv[i], "?") == 0) { printf("Usage: rolling_steel_68k [SCALE=n] [FULLSCREEN] [AGA]\n"); return 0; }
    }
    if (scale < 1) scale = 1;
    if (scale > 4) scale = 4;
    bridge = ab_init("ROLL") == 0;
#ifdef __MINT__
    sys_rgb565 = 1;                     /* Falcon port: draw straight into the screen */
#endif
    if (!sys_open("Rolling Steel", SCREEN_H, scale, mode)) {
        printf("rolling_steel: can't open a window or screen\n");
        rc = 20;
        goto done;
    }
    if (!glc_open(1)) { printf("rolling_steel: no memory for the depth buffer\n"); rc = 20; goto done; }
    for (i = 0; i < P_COUNT; i++)
#ifdef __MINT__
        pen15[i] = (UWORD)((((pen_rgb[i] >> 19) & 31) << 11) | (((pen_rgb[i] >> 10) & 63) << 5) | ((pen_rgb[i] >> 3) & 31));
#else
        pen15[i] = (UWORD)((((pen_rgb[i] >> 19) & 31) << 10) | (((pen_rgb[i] >> 11) & 31) << 5) | ((pen_rgb[i] >> 3) & 31));
#endif
    if (!timer_open()) { printf("rolling_steel: no timer.device\n"); rc = 20; goto done; }
    if (!font_init()) AB_W("topaz 8 not available: no HUD text");

    /* something to look at while the courses and sounds load */
    fill(0, 0, SCREEN_W - 1, SCREEN_H - 1, P_SHADOW);
    text_scaled(160 - 13 * 8 + 2, 102, "ROLLING STEEL", 2, P_BAR);
    text_scaled(160 - 13 * 8, 100, "ROLLING STEEL", 2, P_AMBER);
    text_scaled(160 - 4 * 10, 136, "LOADING...", 1, P_DIM);
    sys_present();

    if (!render_init()) { AB_E("render_init failed"); rc = 20; goto done; }
    if (!snd_init()) AB_W("no sound (ahi.device or data/sfx.raw unavailable)");
    game_init();

    if (bridge) {
        ab_register_var("state", AB_TYPE_I32, &cur_state);
        ab_register_var("level", AB_TYPE_I32, &cur_level);
        ab_register_var("time_left", AB_TYPE_I32, &time_left);
        ab_register_var("falls", AB_TYPE_I32, &falls);
        ab_register_var("fps10", AB_TYPE_I32, &fps10);
        ab_register_var("quads", AB_TYPE_I32, &quads);
        ab_register_var("prims", AB_TYPE_I32, &prims);
        ab_register_hook("press", "hold a pad 1 button: UP DOWN LEFT RIGHT A B C P L R", hk_press);
        ab_register_hook("press2", "hold a pad 2 button", hk_press2);
        ab_register_hook("quit", "quit the game", hk_quit);
    }
    AB_I("ready %s sound=%ld courses=%ld", sys_mode_name(), (long)snd_available(), (long)course_count);

    t_last = t_fps = now_us();
    while (!quit) {
        struct IntuiMessage *m;
        unsigned long long t, t0, t1, t2, t3;
        ULONG h[2], p[2];
        int steps, pad;

        while ((m = (struct IntuiMessage *)GetMsg(sys_window()->UserPort)) != 0) {
            ULONG cls = m->Class;
            UWORD code = m->Code, qual = m->Qualifier;
            ReplyMsg((struct Message *)m);
            if (cls == IDCMP_CLOSEWINDOW) { quit = 1; quit_why = "close gadget"; }
            else if (cls == IDCMP_INACTIVEWINDOW) held[0] = held[1] = 0;
            else if (cls == IDCMP_RAWKEY) {
                ULONG b;
                if (code == 0x45) { quit = 1; quit_why = "Esc"; }
                else if (code == SYS_KEY_F || code == SYS_KEY_F10) {
                    if (!sys_toggle()) { quit = 1; quit_why = "no display"; }
                    AB_I("display: %s", sys_mode_name());
                    break;                                         /* the old window's port is gone */
                }
                else if (code & IECODE_UP_PREFIX) {
                    b = key_bit(code & 0x7F, &pad);
                    held[pad] &= ~b;
                } else if (!(qual & IEQUALIFIER_REPEAT)) {
                    b = key_bit(code, &pad);
                    held[pad] |= b;
                    pressed[pad] |= b;
                }
            }
        }
        if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) { quit = 1; quit_why = "Ctrl-C"; }
        {   /* the joystick in port 1 (FS-UAE puts it on the cursor keys) */
            ULONG j = sys_joystick();
            pressed[0] |= j & ~joy_held;
            joy_held = j;
        }
        if (bridge) ab_poll();
        if (quit) break;

        /* 50 logic steps a second whatever the frame rate */
        t = now_us();
        acc += t - t_last;
        t_last = t;
        steps = (int)(acc / 20000);
        if (steps == 0) { WaitTOF(); continue; }
        acc -= (unsigned long long)steps * 20000;
        if (steps > 6) { steps = 6; acc = 0; }

        for (pad = 0; pad < 2; pad++) {
            h[pad] = held[pad] | pressed[pad] | inj_held[pad] | (pad == 0 ? joy_held : 0);
            p[pad] = pressed[pad] | inj_pressed[pad];
            pressed[pad] = inj_pressed[pad] = 0;
            if (inj_frames[pad] > 0 && --inj_frames[pad] == 0) inj_held[pad] = 0;
        }
        t0 = now_us();
        while (steps-- > 0) {
            game_step(h[0], p[0], h[1], p[1]);
            paula_tick();                       /* the 3DO's 50 Hz audio tick */
            p[0] = p[1] = 0;
            steps_done++;
        }
        t1 = now_us();
        render_begin();
        if (G.loaded) game_draw();
        if (G.state != ST_TITLE) {
            Player *pl = &G.p[0];
            if (pl->flash > 0)
                render_flash((pl->flash_r * pl->flash) >> 7, (pl->flash_g * pl->flash) >> 7, (pl->flash_b * pl->flash) >> 7);
        }
        hud();                              /* its shaded boxes are GL; text waits */
        render_end();
        t2 = now_us();
        hud_draw();
        sys_present();
        t3 = now_us();
        snd_update();
        prof[0] += t1 - t0; prof[1] += t2 - t1; prof[2] += t3 - t2;

        cur_state = G.state;
        cur_level = G.level + 1;
        time_left = G.p[0].time_left;
        falls = G.p[0].deaths;
        quads = render_stats_quads;
        prims = render_stats_cels;
        frames++;
        fps_frames++;
        if (t - t_fps >= 5000000ULL) {
            fps10 = (long)(fps_frames * 10000000ULL / (t - t_fps));
            AB_I("fps=%ld.%ld quads=%ld prims=%ld state=%ld course=%ld steps=%ld ms/frame logic=%ld draw=%ld hud+blit=%ld",
                 fps10 / 10, fps10 % 10, quads, prims, cur_state, cur_level, steps_done,
                 (long)(prof[0] / 1000 / fps_frames), (long)(prof[1] / 1000 / fps_frames),
                 (long)(prof[2] / 1000 / fps_frames));
            memset(prof, 0, sizeof(prof));
            fps_frames = 0;
            t_fps = t;
        }
    }
    AB_I("quit (%s) after %ld frames", quit_why, frames);
done:
    snd_exit();
    course_free();
    glc_close();
    sys_close();
    timer_close();
    if (bridge) ab_cleanup();
    return rc;
}
