/*
 * PLANET CHOMP - classic Amiga (68020+, AmigaOS 3.x) version, from the 3DO
 * version (3do-dev/projects/planet_chomp) like the AmigaOS 4 one.
 *
 * Same game code; the 3DO renderer unchanged (render_cel.c) with its cels
 * drawn by the CPU (softcel.c), and the 3DO's synthesised sounds on the
 * real Paula (sfx_paula.c). The frame is 320 x 256 (the 3DO's 240-line
 * display stretched onto it, as its layer did) and goes to an RTG window
 * or screen, or an AGA screen (amiga68k.c).
 *
 * Keys: arrows / WASD / keypad steer, Q/E or Z/X spin the view, C whole
 * planet, P pause, Space start, Esc quit; F or F10 window / full screen.
 *
 * Usage: planet_chomp_68k [SCALE=n] [FULLSCREEN] [AGA]
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
#include "amiga3do.h"
#include "bridge_client.h"
#include "pc.h"
#include "game.h"
#include "sfx.h"
#include "sprites.h"
#include "amiga68k.h"
#include "paula.h"
#include "softcel.h"

#define LOGICAL_W 320
#define LOGICAL_H 256

unsigned long __stack = 65536;

enum { P_WHITE = 1, P_DIM, P_YELLOW, P_GOLD, P_RED, P_PINK, P_CYAN, P_ORANGE, P_RING, P_RINGDIM,
       P_FRIGHT, P_EYES, P_BAR_BG, P_COUNT };
static const ULONG pen_rgb[P_COUNT] = {
    0x000000, 0xF0F4FF, 0x98A0C0, 0xFFD838, 0xFFB828, 0xFF4C40, 0xFF8CD8, 0x60F0FF,
    0xFFA640, 0x5080FF, 0x284080, 0x3048FF, 0xC0C0D0, 0x101828
};

/* ---- the frame buffer the HUD draws into (logical 320 x 256 coordinates) ---- */

static UWORD pen15[P_COUNT];       /* the pens in 15-bit RGB; the frame is fb (amiga68k.c) */

static void fill(int x0, int y0, int x1, int y1, int pen)    /* inclusive, logical */
{
    UWORD c = pen15[pen];
    int x, y;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > LOGICAL_W - 1) x1 = LOGICAL_W - 1;
    if (y1 > LOGICAL_H - 1) y1 = LOGICAL_H - 1;
    if (x0 > x1 || y0 > y1) return;
    for (y = y0; y <= y1; y++) {
        UWORD *row = fb + (long)y * LOGICAL_W;
        for (x = x0; x <= x1; x++) row[x] = c;
    }
}

static void plot(int x, int y, int pen) { fill(x, y, x, y, pen); }

static void line(int x0, int y0, int x1, int y1, int pen)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        int e2;
        plot(x0, y0, pen);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* topaz 8, read out of the ROM font once */
static unsigned char glyph[256][8];

static int font_init(void)
{
    struct TextAttr ta = { (STRPTR)"topaz.font", 8, 0, 0 };
    struct TextFont *tf = OpenFont(&ta);
    int c;
    if (!tf) return 0;
    for (c = 32; c < 256; c++) {
        int idx = c, r, b;
        const ULONG *loc = (const ULONG *)tf->tf_CharLoc;
        const UBYTE *data = (const UBYTE *)tf->tf_CharData;
        int off, w;
        if (idx < tf->tf_LoChar || idx > tf->tf_HiChar) idx = tf->tf_HiChar + 1;   /* "no glyph" */
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

static void text_scaled(int x, int y, const char *s, int scale, int pen)
{
    for (; *s; s++, x += 8 * scale) {
        const unsigned char *gl = glyph[(unsigned char)*s];
        int r, b;
        for (r = 0; r < 8; r++)
            for (b = 0; b < 8; b++)
                if (gl[r] & (0x80 >> b))
                    fill(x + b * scale, y + r * scale, x + b * scale + scale - 1, y + r * scale + scale - 1, pen);
    }
}

/* the 3DO HUD calls, drawn immediately */
static void text(int x, int y, const char *s, int pen) { text_scaled(x, y, s, 1, pen); }
static void ctext(int y, const char *s, int pen) { text(160 - 4 * (int)strlen(s), y, s, pen); }

static void big(int y, const char *s, int scale, int pen)
{
    int x = 160 - 8 * scale * (int)strlen(s) / 2;
    text_scaled(x + 1, y + 1, s, scale, P_BAR_BG);              /* drop shadow */
    text_scaled(x, y, s, scale, pen);
}

static void disc(int cx, int cy, int r, int pen)
{
    int k;
    for (k = -r; k <= r; k++) {
        int w = 0;
        while ((w + 1) * (w + 1) + k * k <= r * r) w++;
        fill(cx - w, cy + k, cx + w, cy + k, pen);
    }
}

static void ring(int cx, int cy, int r, int pen)
{
    static const short cs[32][2] = {
        {16384,0},{16069,3196},{15137,6270},{13623,9102},{11585,11585},{9102,13623},{6270,15137},{3196,16069},
        {0,16384},{-3196,16069},{-6270,15137},{-9102,13623},{-11585,11585},{-13623,9102},{-15137,6270},{-16069,3196},
        {-16384,0},{-16069,-3196},{-15137,-6270},{-13623,-9102},{-11585,-11585},{-9102,-13623},{-6270,-15137},{-3196,-16069},
        {0,-16384},{3196,-16069},{6270,-15137},{9102,-13623},{11585,-11585},{13623,-9102},{15137,-6270},{16069,-3196}
    };
    int k, px = cx + r, py = cy;
    for (k = 1; k <= 32; k++) {
        int nx = cx + (r * cs[k & 31][0]) / 16384, ny = cy - (r * cs[k & 31][1]) / 16384;
        line(px, py, nx, ny, pen);
        px = nx; py = ny;
    }
}

static void bar(int x0, int y0, int x1, int y1, int pen) { fill(x0, y0, x1, y1, pen); }

/* ---- radar: the whole sphere around the player; centre = here, rim =
 * the far side, in the camera's screen frame ---- */

static void radar_dot(V3 p, V3 d, V3 cr, V3 cu, int cx, int cy, int rad, int size, int pen)
{
    fix cosang = v3_dot14(p, d), x, y, a;
    V3 t = v3_sub(d, v3_scale14(p, cosang));
    if (!(t.x | t.y | t.z)) { disc(cx, cy, size, pen); return; }
    t = v3_norm14(t);
    a = (ONE14 - cosang) / 2;
    a = (a * 3 + (fix)isqrt32((unsigned long)a << 14)) / 4;
    x = (v3_dot14(t, cr) * a) >> 14;
    y = (v3_dot14(t, cu) * a) >> 14;
    disc(cx + (int)((x * rad) >> 14), cy - (int)((y * rad) >> 14), size, pen);
}

static void radar(void)
{
    int cx = 282, cy = 214, rad = 30, i;
    V3 p = mover_dir(&g.player);
    V3 cr = v3_norm14(v3_cross14(g.cam.focus, g.cam.up)), cu = g.cam.up;
    ring(cx, cy, rad + 4, P_RING);
    ring(cx, cy, (rad + 4) / 2, P_RINGDIM);
    for (i = 0; i < 4; i++)
        if (g.key[mz_keys[i]]) radar_dot(p, mz_dir[mz_keys[i]], cr, cu, cx, cy, rad, 2, P_GOLD);
    radar_dot(p, mz_dir[mz_nest], cr, cu, cx, cy, rad, 2, P_PINK);
    for (i = 0; i < 4; i++) {
        static const int tint[4] = { P_RED, P_PINK, P_CYAN, P_ORANGE };
        Ghost *gh = &g.ghost[i];
        int pen = gh->mode == GM_EATEN ? P_EYES : gh->fright ? P_FRIGHT : tint[i];
        radar_dot(p, mover_dir(&gh->mv), cr, cu, cx, cy, rad, 2, pen);
    }
    disc(cx, cy, 2, P_YELLOW);
    text(cx - 20, cy - rad - 16, "RADAR", P_DIM);
}

/* ---- sprites for this frame ---- */

static RSprite spr[6];

static int chomper_dir(void)
{
    V3 p = player_world(), q = v3_add(p, v3_scale14(g.player.heading, Q8(1)));
    long x0, y0, x1, y1, dx, dy;
    if (!rd_project(p, &x0, &y0) || !rd_project(q, &x1, &y1)) return 0;
    dx = x1 - x0; dy = y1 - y0;
    if ((dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy)) return dx > 0 ? 0 : 2;
    return dy < 0 ? 1 : 3;
}

static int build_sprites(void)
{
    int n = 0, i;
    if (g.state != GS_TITLE && g.state != GS_OVER) {
        int mouth = 1;
        fix half = Q8(0.55);
        if (g.player.moving) {
            int ph = (int)(g.mouth % 12);
            mouth = ph < 4 ? 0 : ph < 8 ? 1 : 2;
        }
        if (g.state == GS_DYING) {
            long t = g.timer - TICKS(0.4);
            mouth = 2;
            if (t > 0) half = half * (TICKS(1.4) - (t > TICKS(1.4) ? TICKS(1.4) : t)) / TICKS(1.4);
        }
        if (half > 0) {
            spr[n].pos = player_world();
            spr[n].tex = tex_chomper[g.powered][chomper_dir()][mouth];
            spr[n].half = half;
            n++;
        }
    }
    if (!(g.state == GS_DYING && g.timer > TICKS(0.4)) && g.state != GS_CLEAR && g.state != GS_OVER)
        for (i = 0; i < 4; i++) {
            Ghost *gh = &g.ghost[i];
            int t;
            if (gh->mode == GM_EATEN) t = tex_ghost[GT_EYES];
            else if (gh->fright)
                t = (g.power_left < TICKS(2) && (g.power_left % 20) < 10) ? tex_ghost[GT_FLASH]
                                                                          : tex_ghost[GT_FRIGHT];
            else t = tex_ghost[i];
            spr[n].pos = ghost_world(gh);
            spr[n].tex = t;
            spr[n].half = Q8(0.6);
            n++;
        }
    return n;
}

/* ---- screens ---- */

static long fps10;
static unsigned long long prof[4];      /* us: logic, draw, hud, blit */

static void hud_play(void)
{
    char buf[40];
    int i, lives;
    text(8, 4, "SCORE", P_DIM);
    sprintf(buf, "%ld", g.score);
    text(8, 14, buf, P_WHITE);
    ctext(4, "HIGH SCORE", P_DIM);
    sprintf(buf, "%ld", g.hiscore);
    ctext(14, buf, P_WHITE);
    sprintf(buf, "LEVEL %ld", (long)g.level);
    text(312 - 8 * (int)strlen(buf), 4, buf, P_YELLOW);
    sprintf(buf, "CRUMBS %ld", (long)g.crumbs_left);
    text(312 - 8 * (int)strlen(buf), 14, buf, P_DIM);
    lives = g.lives - ((g.state == GS_READY || g.state == GS_PLAYING) ? 1 : 0);
    for (i = 0; i < lives && i < 8; i++) disc(14 + i * 14, 244, 5, P_YELLOW);
    if (g.power_left > 0) {
        int w = (int)(160 * g.power_left / (g.power_total ? g.power_total : 1));
        int blink = g.power_left < TICKS(2) && (g.power_left % 20) < 10;
        bar(80, 28, 239, 32, P_BAR_BG);
        if (w > 0) bar(80, 28, 80 + w - 1, 32, blink ? P_WHITE : P_GOLD);
        ctext(36, "INVINCIBLE!", P_GOLD);
    }
    radar();
    /* score popups float up from eaten spooks */
    for (i = 0; i < 8; i++) {
        long x, y;
        if (g.popup[i].age < 0 || !rd_project(g.popup[i].world, &x, &y)) continue;
        sprintf(buf, "%ld", (long)g.popup[i].pts);
        text((int)(x >> 16) - 4 * (int)strlen(buf), (int)(((y >> 16) * 16) / 15) - 10 - g.popup[i].age / 2, buf, P_CYAN);
    }
    switch (g.state) {
    case GS_READY:
        big(150, "READY!", 2, P_YELLOW);
        if (g.level == 1) ctext(172, "EAT EVERY CRUMB - GRAB A KEY", P_DIM);
        break;
    case GS_CLEAR:
        big(80, "PLANET CLEARED!", 2, P_YELLOW);
        break;
    }
    if (g.paused) big(110, "PAUSED", 2, P_WHITE);
    if (g.demo) ctext(228, "DEMO - PRESS SPACE", P_DIM);
}

static void hud_title(void)
{
    char buf[40];
    int i;
    static const int tint[4] = { P_RED, P_PINK, P_CYAN, P_ORANGE };
    big(34, "PLANET CHOMP", 3, P_YELLOW);
    ctext(64, "A PAC-MAN HOMAGE ON A VERY SMALL WORLD", P_DIM);
    for (i = 0; i < 4; i++) {
        int x = 28 + i * 72;
        disc(x, 92, 5, tint[i]);
        text(x + 9, 88, ghost_name[i], tint[i]);
    }
    ctext(118, "EAT EVERY CRUMB ON THE PLANET.", P_WHITE);
    ctext(130, "GRAB A GOLDEN KEY TO TURN THE TABLES.", P_WHITE);
    ctext(148, "ARROWS MOVE   Q/E SPIN THE VIEW", P_DIM);
    ctext(160, "C WHOLE PLANET   P PAUSE   ESC QUIT", P_DIM);
    if ((g.clock / 25) % 3 != 2) big(190, "PRESS SPACE TO START", 2, P_YELLOW);
    sprintf(buf, "HIGH SCORE  %ld", g.hiscore);
    ctext(236, buf, P_DIM);
}

static void hud_over(void)
{
    char buf[48];
    big(100, "GAME OVER", 3, P_RED);
    ctext(132, "PRESS SPACE TO PLAY AGAIN", P_DIM);
    sprintf(buf, "SCORE %ld   HIGH SCORE %ld", g.score, g.hiscore);
    ctext(150, buf, P_WHITE);
}

/* ---- high score ---- */

#define HI_FILE "PROGDIR:planetchomp.hi"

static void hiscore_load(void)
{
    BPTR f = Open((CONST_STRPTR)HI_FILE, MODE_OLDFILE);
    long v = 0;
    if (f) {
        if (Read(f, &v, 4) == 4 && v > 0 && v < 100000000L) g.hiscore = v;
        Close(f);
    }
}

static void hiscore_save(void)
{
    BPTR f = Open((CONST_STRPTR)HI_FILE, MODE_NEWFILE);
    if (f) {
        Write(f, &g.hiscore, 4);
        Close(f);
    }
}

/* ---- keyboard as a pad ---- */

static ULONG keys_held, keys_pressed, inject_held, inject_pressed;
static int   inject_frames, quit;
static const char *quit_why = "";
static ULONG joy_held;                   /* the joystick's bits (sys_joystick) */

static ULONG key_bit(UWORD code)
{
    switch (code) {
    case 0x4C: case 0x11: case 0x3E: return PAD_UP;       /* cursor, W, keypad 8 */
    case 0x4D: case 0x21: case 0x1E: return PAD_DOWN;     /* cursor, S, keypad 2 */
    case 0x4F: case 0x20: case 0x2D: return PAD_LEFT;     /* cursor, A, keypad 4 */
    case 0x4E: case 0x22: case 0x2F: return PAD_RIGHT;    /* cursor, D, keypad 6 */
    case 0x40: case 0x44: case 0x43: return PAD_A;        /* space, return, enter */
    case 0x10: case 0x31: return PAD_L;                   /* Q, Z */
    case 0x12: case 0x32: return PAD_R;                   /* E, X */
    case 0x33: return PAD_C;                              /* C */
    case 0x19: return PAD_P;                              /* P */
    }
    return 0;
}

/* bridge hook: "press UP" / "press A" ... holds a pad button for a few frames */
static int hk_press(const char *args, char *res, int len)
{
    static const struct { const char *n; ULONG b; } names[] = {
        { "UP", PAD_UP }, { "DOWN", PAD_DOWN }, { "LEFT", PAD_LEFT }, { "RIGHT", PAD_RIGHT },
        { "A", PAD_A }, { "B", PAD_B }, { "C", PAD_C }, { "P", PAD_P }, { "L", PAD_L }, { "R", PAD_R }
    };
    unsigned i;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (args && strcmp(args, names[i].n) == 0) {
            inject_held = names[i].b;
            inject_pressed |= names[i].b;
            inject_frames = 6;
            snprintf(res, len, "pressed %s", names[i].n);
            return 0;
        }
    snprintf(res, len, "unknown button");
    return -1;
}

static int hk_quit(const char *args, char *res, int len)
{
    (void)args;
    snprintf(res, len, "quitting");
    quit = 1;
    quit_why = "bridge";
    return 0;
}

/* ---- main ---- */

static int parse_args(int argc, char **argv, int *scale, int *mode)
{
    int i;
    for (i = 1; i < argc; i++) {
        if (strncmp(argv[i], "SCALE=", 6) == 0 || strncmp(argv[i], "scale=", 6) == 0) *scale = atoi(argv[i] + 6);
        else if (!strcmp(argv[i], "FULLSCREEN") || !strcmp(argv[i], "fullscreen")) *mode = SYS_RTG_SCREEN;
        else if (!strcmp(argv[i], "AGA") || !strcmp(argv[i], "aga")) *mode = SYS_AGA;
        else if (strcmp(argv[i], "?") == 0) {
            printf("Usage: planet_chomp_68k [SCALE=n] [FULLSCREEN] [AGA]\n");
            return 0;
        }
    }
    if (*scale < 1) *scale = 1;
    if (*scale > 4) *scale = 4;
    return 1;
}

int main(int argc, char **argv)
{
    int scale = 2, mode = SYS_AUTO, bridge, i;
    long saved_hi = 0, frames = 0, steps_done = 0;
    int last_state = -1, rc = 0;
    unsigned long long t_last, acc = 0, t_fps;
    long fps_frames = 0;
    long dbg_walls = 0, dbg_prims = 0;
    unsigned long long tp0, tp1, tp2, tp3, tp4;

    if (!parse_args(argc, argv, &scale, &mode)) return 0;
    bridge = ab_init("PLANET") == 0;
    if (!sys_open("Planet Chomp", LOGICAL_H, scale, mode)) {
        printf("planet_chomp: can't open a window or screen\n");
        rc = 20;
        goto done;
    }
    sc_setup(fb, LOGICAL_W, LOGICAL_H, 16);            /* 240 display lines onto 256 */
    for (i = 0; i < P_COUNT; i++)
        pen15[i] = (UWORD)((((pen_rgb[i] >> 19) & 31) << 10) | (((pen_rgb[i] >> 11) & 31) << 5) | ((pen_rgb[i] >> 3) & 31));
    if (!timer_open()) { printf("planet_chomp: no timer.device\n"); rc = 20; goto done; }
    if (!font_init()) AB_W("topaz 8 not available: no HUD text");

    /* something to look at while the sounds and sprites are made */
    fill(0, 0, LOGICAL_W - 1, LOGICAL_H - 1, P_BAR_BG);
    big(100, "PLANET CHOMP", 3, P_YELLOW);
    ctext(146, "LOADING...", P_DIM);
    sys_present();

    if (!pc_cels_init() || !sprites_init()) { AB_E("no memory for sprites"); rc = 20; goto done; }
    tex_key_id = tex_key;
    if (!sfx_init()) AB_W("no sound (ahi.device unavailable)");
    rd_init();
    game_init();
    hiscore_load();
    saved_hi = g.hiscore;

    if (bridge) {
        ab_register_var("score", AB_TYPE_I32, &g.score);
        ab_register_var("hiscore", AB_TYPE_I32, &g.hiscore);
        ab_register_var("level", AB_TYPE_I32, &g.level);
        ab_register_var("lives", AB_TYPE_I32, &g.lives);
        ab_register_var("state", AB_TYPE_I32, &g.state);
        ab_register_var("crumbs", AB_TYPE_I32, &g.crumbs_left);
        ab_register_var("demo", AB_TYPE_I32, &g.demo);
        ab_register_var("fps10", AB_TYPE_I32, &fps10);
        ab_register_var("walls", AB_TYPE_I32, &dbg_walls);
        ab_register_var("prims", AB_TYPE_I32, &dbg_prims);
        ab_register_hook("press", "hold a pad button: UP DOWN LEFT RIGHT A C P L R", hk_press);
        ab_register_hook("quit", "quit the game", hk_quit);
    }
    AB_I("ready %s sound=%ld walls=%ld", sys_mode_name(), (long)sfx_available(), (long)mz_nwalls);

    t_last = t_fps = now_us();
    while (!quit) {
        struct IntuiMessage *m;
        ULONG held, pressed;
        int steps, nspr, flags = 0;
        unsigned long long t;

        while ((m = (struct IntuiMessage *)GetMsg(sys_window()->UserPort)) != 0) {
            ULONG cls = m->Class;
            UWORD code = m->Code, qual = m->Qualifier;
            ReplyMsg((struct Message *)m);
            if (cls == IDCMP_CLOSEWINDOW) { quit = 1; quit_why = "close gadget"; }
            else if (cls == IDCMP_INACTIVEWINDOW) keys_held = 0;
            else if (cls == IDCMP_RAWKEY) {
                if (code == 0x45) { quit = 1; quit_why = "Esc"; }  /* Esc */
                else if (code == SYS_KEY_F || code == SYS_KEY_F10) {
                    if (!sys_toggle()) { quit = 1; quit_why = "no display"; }
                    AB_I("display: %s", sys_mode_name());
                    break;                                         /* the old window's port is gone */
                }
                else if (code & IECODE_UP_PREFIX) keys_held &= ~key_bit(code & 0x7F);
                else if (!(qual & IEQUALIFIER_REPEAT)) {
                    keys_held |= key_bit(code);
                    keys_pressed |= key_bit(code);
                }
            }
        }
        if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) { quit = 1; quit_why = "Ctrl-C"; }
        {   /* the joystick in port 1 (FS-UAE puts it on the cursor keys) */
            ULONG j = sys_joystick();
            keys_pressed |= j & ~joy_held;
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
        if (steps > 8) { steps = 8; acc = 0; }

        tp0 = now_us();
        held = keys_held | keys_pressed | inject_held | joy_held;
        pressed = keys_pressed | inject_pressed;
        keys_pressed = inject_pressed = 0;
        if (inject_frames > 0 && --inject_frames == 0) inject_held = 0;
        while (steps-- > 0) {
            game_step(held, pressed);
            pressed = 0;                                   /* an edge counts once */
            steps_done++;
        }

        if (g.state == GS_CLEAR && g.timer > TICKS(0.6) && (g.timer % 18) < 9)
            flags |= RD_WALL_FLASH;
        tp1 = now_us();
        nspr = build_sprites();
        rd_frame(&g.cam, g.crumb, g.key, spr, nspr, flags);
        dbg_walls = rd_stats_walls;
        dbg_prims = rd_stats_cels;

        tp2 = now_us();
        if (g.state == GS_TITLE) hud_title();
        else {
            hud_play();
            if (g.state == GS_OVER) hud_over();
        }
        tp3 = now_us();

        sys_present();
        tp4 = now_us();
        prof[0] += tp1 - tp0; prof[1] += tp2 - tp1; prof[2] += tp3 - tp2; prof[3] += tp4 - tp3;
        sfx_update();

        if (g.state != last_state) {
            if (g.state == GS_OVER && g.hiscore > saved_hi) {
                hiscore_save();
                saved_hi = g.hiscore;
            }
            last_state = g.state;
        }
        frames++;
        fps_frames++;
        if (t - t_fps >= 5000000ULL) {
            fps10 = (long)(fps_frames * 10000000ULL / (t - t_fps));
            AB_I("fps=%ld.%ld walls=%ld cels=%ld steps=%ld score=%ld ms/frame logic=%ld draw=%ld hud=%ld blit=%ld",
                 fps10 / 10, fps10 % 10, (long)rd_stats_walls, (long)rd_stats_cels, steps_done, g.score,
                 (long)(prof[0] / 1000 / fps_frames), (long)(prof[1] / 1000 / fps_frames),
                 (long)(prof[2] / 1000 / fps_frames), (long)(prof[3] / 1000 / fps_frames));
            memset(prof, 0, sizeof(prof));
            fps_frames = 0;
            t_fps = t;
        }
    }
    if (g.hiscore > saved_hi) hiscore_save();
    AB_I("quit (%s) after %ld frames", quit_why, frames);
done:
    sfx_exit();
    AB_I("exit: sound closed");
    sys_close();
    AB_I("exit: display closed");
    timer_close();
    if (bridge) ab_cleanup();
    return rc;
}
