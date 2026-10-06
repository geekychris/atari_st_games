// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * void_trader - Atari Falcon030 port.
 *
 * Port of the Amiga AGA version (main.c in geekychris/amiga_games, kept
 * as main.c.amiga).  engine3d.c, models.c, combat.c, scanner.c, trade.c,
 * sfx.c and modplay.c are unmodified; the 3D engine's filled triangles
 * (AreaMove/AreaDraw/AreaEnd) and the 256 pens run on the Falcon port
 * layer's 16 bit true colour screen (../falcon_port/fgfx), the music and
 * sound effects on the Paula emulation (../st_port/paula, DMA sound).
 *
 * Below, the original's palette, cockpit, world, camera and per-mode
 * code are copied verbatim from main.c; only the screen, input and the
 * loop timing are the Falcon's.  The game logic, one tick per rendered
 * frame on the Amiga, runs at 25 ticks per second here, and rendering
 * happens once per loop.
 *
 * Controls: W/S pitch, A/D yaw, Q/E roll, R/F thrust, Space fire,
 * Tab dock, U undock, B/N buy/sell, Esc quits.  With Hatari --natfeats
 * on, events and symbol addresses are logged ("VTRADER ..." lines).
 */
#include <osbind.h>
#include <stdio.h>
#include <string.h>

#include "fgfx.h"
#include "st_ikbd.h"
#include "natfeats.h"
#include "bridge_client.h"
#include "engine3d.h"
#include "models.h"
#include "combat.h"
#include "scanner.h"
#include "gamemode.h"
#include "trade.h"
#include "modplay.h"
#include "sfx.h"

#define FRCLOCK (*(volatile long *)0x466)
#define TICK_HZ 25

/* Screen geometry. LORES AGA 320x256 8bpp. */
#define SCREEN_W    320
#define SCREEN_H    256
#define VIEW_H      180                 /* HUD chrome eats the rest */
#define VIEW_H_HALF (VIEW_H / 2)

#define IN_PITCH_DOWN 0x0001
#define IN_PITCH_UP   0x0002
#define IN_YAW_L      0x0004
#define IN_YAW_R      0x0008
#define IN_ROLL_L     0x0010
#define IN_ROLL_R     0x0020
#define IN_THRUST_FWD 0x0040
#define IN_THRUST_REV 0x0080
/* IN_FIRE lives in combat.h as VT_IN_FIRE = 0x0100 so combat.c
 * can read it without pulling main.c's headers. Alias here. */
#define IN_FIRE       VT_IN_FIRE
#define IN_DOCK       0x0200
#define IN_UNDOCK     0x0400
#define IN_BUY        0x0800
#define IN_SELL       0x1000
/* IN_PITCH_DOWN / IN_PITCH_UP double as menu up/down when docked. */

static UWORD input_flags;

/* ST port: the Amiga code sets input_flags from key events, so a key
 * cleared at a mode change ("input_flags = 0") stays cleared until it is
 * pressed again.  Here the keys are polled each tick: cleared ones are
 * held back until released. */
static UWORD held_back;

static UWORD poll_keys(void)
{
    volatile UBYTE *k = ikbd_keys;
    UWORD f = 0;
    if (k[SC_W])    f |= IN_PITCH_DOWN;
    if (k[SC_S])    f |= IN_PITCH_UP;
    if (k[SC_A])    f |= IN_YAW_L;
    if (k[SC_D])    f |= IN_YAW_R;
    if (k[SC_Q])    f |= IN_ROLL_L;
    if (k[0x12])    f |= IN_ROLL_R;     /* E */
    if (k[0x13])    f |= IN_THRUST_FWD;     /* R */
    if (k[0x21])    f |= IN_THRUST_REV;     /* F */
    if (k[SC_SPACE]) f |= IN_FIRE;
    if (k[0x0f])    f |= IN_DOCK;           /* Tab */
    if (k[0x16])    f |= IN_UNDOCK;         /* U */
    if (k[0x30])    f |= IN_BUY;            /* B */
    if (k[0x31])    f |= IN_SELL;           /* N */
    return f;
}

static int clampi(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}
/* ST port: the pens go to the Falcon layer's RGB565 table */
struct ViewPort;
static void put_rgb(struct ViewPort *vp, UWORD pen, int r, int g, int b)
{
    (void)vp;
    r = clampi(r, 0, 255);
    g = clampi(g, 0, 255);
    b = clampi(b, 0, 255);
    fgfx_set_rgb(pen, r, g, b);
}

static void install_palette(struct ViewPort *vp)
{
    int i;
    /* 0 = deep space black */
    put_rgb(vp, 0, 0, 0, 8);
    /* 1..4 = starfield white/yellow tints */
    for (i = 0; i < 4; i++) {
        int v = 130 + i * 32;
        if (v > 255) v = 255;
        put_rgb(vp, 1 + i, v, v, v);
    }
    /* 8..39 = ship shading ramp: dark grey -> mid grey -> highlight.
     * Pen base per ship gets bumped so we can colour-code teams. */
    for (i = 0; i < 32; i++) {
        int shade = 20 + i * 6;
        if (shade > 250) shade = 250;
        put_rgb(vp, 8 + i, shade, shade, shade + 10);
    }
    /* 40..71 = second ramp for enemy — reddish */
    for (i = 0; i < 32; i++) {
        int shade = 20 + i * 6;
        if (shade > 250) shade = 250;
        put_rgb(vp, 40 + i, shade + 20, shade / 2, shade / 3);
    }
    /* 72..103 = station ramp — greenish */
    for (i = 0; i < 32; i++) {
        int shade = 20 + i * 6;
        if (shade > 250) shade = 250;
        put_rgb(vp, 72 + i, shade / 3, shade + 20, shade / 2);
    }
    /* 120 = HUD phosphor green */
    put_rgb(vp, 120, 60, 240, 120);
    /* 121 = HUD dim */
    put_rgb(vp, 121, 30, 120, 60);
    /* 122 = cockpit chrome (dark grey) */
    put_rgb(vp, 122, 40, 40, 55);
    /* 123 = cockpit highlight */
    put_rgb(vp, 123, 90, 90, 105);
    /* 124 = warning red */
    put_rgb(vp, 124, 240, 60, 60);
    /* 125 = laser green */
    put_rgb(vp, 125, 60, 255, 90);
}

/* World state — hoisted so draw_cockpit can hand cam+entities
 * to the scanner overlay. */
static Camera cam;
static Entity world[8];
static Combat combat;
static TradeState trade;

static void draw_cockpit(struct RastPort *rp, int fps, LONG speed,
                         LONG energy)
{
    /* Bottom console — hides the lower portion of the screen. */
    SetAPen(rp, 122);
    RectFill(rp, 0, VIEW_H, SCREEN_W - 1, SCREEN_H - 1);
    /* Console highlight line */
    SetAPen(rp, 123);
    Move(rp, 0, VIEW_H); Draw(rp, SCREEN_W - 1, VIEW_H);

    /* Central crosshair reticle */
    SetAPen(rp, 120);
    int cx = SCREEN_W / 2, cy = VIEW_H_HALF;
    Move(rp, cx - 8, cy); Draw(rp, cx - 3, cy);
    Move(rp, cx + 3, cy); Draw(rp, cx + 8, cy);
    Move(rp, cx, cy - 8); Draw(rp, cx, cy - 3);
    Move(rp, cx, cy + 3); Draw(rp, cx, cy + 8);

    /* Console text */
    SetAPen(rp, 120);
    SetDrMd(rp, JAM1);
    char buf[32];
    sprintf(buf, "SPD %04ld", (long)speed);
    Move(rp, 8, VIEW_H + 12); Text(rp, (STRPTR)buf, 8);
    sprintf(buf, "ENGY %04ld", (long)energy);
    Move(rp, 8, VIEW_H + 24); Text(rp, (STRPTR)buf, 9);
    sprintf(buf, "FPS %ld", (long)fps);
    Move(rp, SCREEN_W - 56, VIEW_H + 12); Text(rp, (STRPTR)buf, strlen(buf));
    sprintf(buf, "CR %ld", (long)trade.credits);
    Move(rp, SCREEN_W - 76, VIEW_H + 24); Text(rp, (STRPTR)buf, strlen(buf));
    sprintf(buf, "KILL %ld", (long)(combat.score / 100));
    Move(rp, SCREEN_W - 76, VIEW_H + 36); Text(rp, (STRPTR)buf, strlen(buf));

    /* Scanner ellipse in the middle of the dashboard —
     * see scanner.c for the mapping. */
    vt_scanner_draw(rp, SCREEN_W / 2, VIEW_H + 42, 60, 18,
                    &cam, world, 8);

    /* Left/right cockpit pillars, top only */
    SetAPen(rp, 122);
    RectFill(rp,  0, 0,  8, VIEW_H - 1);
    RectFill(rp, SCREEN_W - 9, 0, SCREEN_W - 1, VIEW_H - 1);
    SetAPen(rp, 123);
    Move(rp,  9, 0);            Draw(rp,  9, VIEW_H - 1);
    Move(rp, SCREEN_W - 10, 0); Draw(rp, SCREEN_W - 10, VIEW_H - 1);
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

static UBYTE game_mode = GM_TITLE;
static UWORD mode_timer = 0;           /* frames since mode entered */
static UWORD enemy_respawn_timer = 0;  /* counts down from ENEMY_RESPAWN_FRAMES when 0 enemies alive */
static ULONG spawn_rng = 0xC0FFEEUL;

/* Station lives at world[2]. Rotate it each frame so it looks alive. */
static void tick_station(void)
{
    if (world[2].active) {
        world[2].roll = (world[2].roll + 1) % 360;
        world[2].yaw  = (world[2].yaw  + 1) % 360;   /* barrel roll */
    }
}

/* Distance from player to station. Returns MAX_LONG if station
 * inactive so callers can compare safely. */
static LONG station_dist(void)
{
    if (!world[2].active) return 0x7FFFFFFF;
    LONG dx = world[2].x - cam.x;
    LONG dy = world[2].y - cam.y;
    LONG dz = world[2].z - cam.z;
    LONG d2 = dx * dx + dy * dy + dz * dz;
    LONG r = 0, bit = 1L << 30, v = d2;
    if (v <= 0) return 0;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return r;
}

static void setup_world(void)
{
    memset(world, 0, sizeof(world));
    /* Player camera at origin looking down +Z. */
    cam.x = 0; cam.y = 0; cam.z = 0;
    cam.pitch = 0; cam.yaw = 0; cam.roll = 0;

    /* One friendly Cobra floating a bit away — for the demo. */
    world[0].active = 1;
    world[0].model = &model_cobra;
    world[0].x = -4000; world[0].y = 200; world[0].z = 8000;
    world[0].yaw = 30;
    world[0].team = 0;
    world[0].pen_base = 8;

    /* Two enemy Kraits — start on opposite sides so it's not
     * trivially easy to face them both at once. */
    world[1].active = 1;
    world[1].model = &model_krait;
    world[1].x = 3500; world[1].y = -300; world[1].z = 6500;
    world[1].yaw = -20;
    world[1].team = 1;
    world[1].pen_base = 40;
    world[1].hp = 3;

    world[3].active = 1;
    world[3].model = &model_krait;
    world[3].x = -4500; world[3].y = 200; world[3].z = 7500;
    world[3].yaw = 60;
    world[3].team = 1;
    world[3].pen_base = 40;
    world[3].hp = 3;

    /* A station in the distance. */
    world[2].active = 1;
    world[2].model = &model_station;
    world[2].x = 0; world[2].y = 500; world[2].z = 14000;
    world[2].team = 2;
    world[2].pen_base = 72;
}

static LONG ship_speed = 0;             /* signed: negative = reverse */

/* Cheap LCG for spawn positions. */
static ULONG srng(void)
{
    spawn_rng = spawn_rng * 1664525UL + 1013904223UL;
    return spawn_rng;
}
static LONG srand_range(LONG lo, LONG hi)
{
    return lo + (LONG)(srng() % (ULONG)(hi - lo + 1));
}

/* Count Kraits currently alive (team==1 + active + hp > 0). */
static int live_enemy_count(void)
{
    int n = 0, i;
    for (i = 0; i < 8; i++)
        if (world[i].active && world[i].team == 1 && world[i].hp > 0) n++;
    return n;
}

/* Spawn a Krait in one of the inactive slots at a random position
 * roughly around the player, far enough not to be point-blank. */
static void spawn_krait(void)
{
    int i;
    for (i = 0; i < 8; i++) {
        if (!world[i].active) {
            LONG r = srand_range(6000, 9000);
            LONG ang = srand_range(0, 359);
            world[i].active = 1;
            world[i].model = &model_krait;
            world[i].team = 1;
            world[i].pen_base = 40;
            world[i].hp = 3;
            world[i].x = cam.x + ((e3d_sin(ang) * r) >> FP);
            world[i].y = cam.y + srand_range(-500, 500);
            world[i].z = cam.z + ((e3d_cos(ang) * r) >> FP);
            world[i].yaw = ang;
            return;
        }
    }
}

/* Full-game reset — called on boot AND on title-screen SPACE
 * (which restarts from win/lose too). */
static void reset_game(void)
{
    ship_speed = 0;
    enemy_respawn_timer = 0;
    input_flags = 0;
    setup_world();
    vt_combat_init(&combat);
    vt_trade_init(&trade);
}
#define SHIP_MAX_SPEED   80
#define SHIP_ACCEL        2
#define ROTATE_RATE       3             /* degrees per tick */

static void update_camera(UWORD in)
{
    /* Rotation. Held keys rotate at ROTATE_RATE°/frame. */
    if (in & IN_PITCH_DOWN) cam.pitch = (cam.pitch + ROTATE_RATE) % 360;
    if (in & IN_PITCH_UP)   cam.pitch = (cam.pitch - ROTATE_RATE + 360) % 360;
    if (in & IN_YAW_R)      cam.yaw   = (cam.yaw   + ROTATE_RATE) % 360;
    if (in & IN_YAW_L)      cam.yaw   = (cam.yaw   - ROTATE_RATE + 360) % 360;
    if (in & IN_ROLL_R)     cam.roll  = (cam.roll  + ROTATE_RATE) % 360;
    if (in & IN_ROLL_L)     cam.roll  = (cam.roll  - ROTATE_RATE + 360) % 360;

    /* Thrust — hold R to accelerate, F to decelerate. Speed
     * persists (cruise-control) once you let go. */
    if (in & IN_THRUST_FWD) {
        ship_speed += SHIP_ACCEL;
        if (ship_speed > SHIP_MAX_SPEED) ship_speed = SHIP_MAX_SPEED;
    }
    if (in & IN_THRUST_REV) {
        ship_speed -= SHIP_ACCEL;
        if (ship_speed < 0) ship_speed = 0;
    }

    /* Translate along camera's local +Z (forward). Forward vector
     * in world space = the third row of the camera's rotation
     * matrix's transpose, i.e. rotate (0,0,1) by camera orientation. */
    LONG sp = e3d_sin(cam.pitch), cp = e3d_cos(cam.pitch);
    LONG sy = e3d_sin(cam.yaw),   cy = e3d_cos(cam.yaw);
    /* Forward = (Ry Rx) * (0,0,1). */
    LONG fx = ( sy * cp) >> FP;
    LONG fy = (-sp)     ;                  /* already ONE-scaled */
    LONG fz = ( cy * cp) >> FP;
    cam.x += (fx * ship_speed) >> FP;
    cam.y += (fy * ship_speed) >> FP;
    cam.z += (fz * ship_speed) >> FP;
}

static void log_line(const char *fmt, long a, long b, long c, long d)
{
    char buf[128];
    strcpy(buf, "VTRADER ");
    snprintf(buf + 8, sizeof(buf) - 9, fmt, a, b, c, d);
    strcat(buf, "\n");
    nf_print(buf);
}

static const char *mode_name(int m)
{
    static const char *n[] = { "TITLE", "FLIGHT", "DOCKING", "DOCKED", "UNDOCKING", "GAME_OVER", "WIN" };
    return m >= 0 && m < 7 ? n[m] : "?";
}

/* one tick of main.c's per-mode code; 0 to quit */
static int game_tick(void)
{
    UWORD keys = poll_keys();
    held_back &= keys;              /* released keys count again */
    input_flags = keys & ~held_back;

        /* --- Per-mode tick --- */
        tick_station();
        mode_timer++;

        LONG sd = station_dist();

        switch (game_mode) {
        case GM_TITLE:
            /* SPACE (fire) launches the mission. Debounce briefly
             * so a keypress from a previous run doesn't skip. */
            if ((input_flags & IN_FIRE) && mode_timer > 8) {
                reset_game();
                game_mode = GM_FLIGHT;
                mode_timer = 0;
                input_flags = 0;
                modplay_start_song(MODPLAY_SONG_GAME);
            }
            break;
        case GM_FLIGHT: {
            LONG prev_score = combat.score;
            update_camera(input_flags);
            vt_combat_tick(&combat, &cam, world, 8, input_flags);
            /* Each new kill (combat.score jumped by 100) pays a
             * 50-credit bounty. Divide-and-count is fine because
             * only one kill can happen per hit-test frame. */
            LONG new_kills = (combat.score - prev_score) / 100;
            if (new_kills > 0) trade.credits += new_kills * 50;
            /* Enemy respawn: when no live enemies, run down a
             * timer; on zero, spawn a new Krait somewhere near. */
            if (live_enemy_count() == 0) {
                if (enemy_respawn_timer > 0) enemy_respawn_timer--;
                if (enemy_respawn_timer == 0) {
                    spawn_krait();
                    enemy_respawn_timer = ENEMY_RESPAWN_FRAMES;
                }
            } else {
                enemy_respawn_timer = ENEMY_RESPAWN_FRAMES;
            }
            /* Enter docking approach on TAB within range. */
            if ((input_flags & IN_DOCK) && sd < DOCK_APPROACH_RANGE) {
                game_mode = GM_DOCKING;
                mode_timer = 0;
                input_flags = 0;
            }
            if (combat.game_over) {
                game_mode = GM_GAME_OVER;
                mode_timer = 0;
            }
            /* Win: reach WIN_CREDITS_TARGET credits (must be
             * cashed-in, so player has to dock + sell to win). */
            if (trade.credits >= WIN_CREDITS_TARGET) {
                game_mode = GM_WIN;
                mode_timer = 0;
            }
            break;
        }
        case GM_DOCKING: {
            /* Cinematic: pull the camera toward the station
             * regardless of input. Ship yaws to face the station's
             * centre so the docking always looks intentional. */
            LONG dx = world[2].x - cam.x;
            LONG dy = world[2].y - cam.y;
            LONG dz = world[2].z - cam.z;
            LONG d = sd > 0 ? sd : 1;
            cam.x += (dx * 40) / d;
            cam.y += (dy * 40) / d;
            cam.z += (dz * 40) / d;
            /* Auto-tumble the ship a bit for style. */
            cam.roll = (cam.roll + 2) % 360;
            if (sd < DOCK_LOCK_RANGE) {
                game_mode = GM_DOCKED;
                mode_timer = 0;
                vt_sfx_play(SFX_DOCK);
            }
            break;
        }
        case GM_DOCKED: {
            /* Edge-detect the menu keys — apply_key already
             * releases them, so we compare against the previous
             * frame's bitmask. */
            static UWORD prev_flags = 0;
            UWORD edge = input_flags & ~prev_flags;
            if      (edge & IN_PITCH_UP)   vt_trade_menu(&trade, VT_MENU_UP);
            else if (edge & IN_PITCH_DOWN) vt_trade_menu(&trade, VT_MENU_DOWN);
            else if (edge & IN_BUY)        { vt_trade_menu(&trade, VT_MENU_BUY);  vt_sfx_play(SFX_BUY); }
            else if (edge & IN_SELL)       { vt_trade_menu(&trade, VT_MENU_SELL); vt_sfx_play(SFX_BUY); }
            prev_flags = input_flags;

            if (input_flags & IN_UNDOCK) {
                game_mode = GM_UNDOCKING;
                mode_timer = 0;
                input_flags = 0;
                prev_flags = 0;
                /* Refuel + refill shields at the station. */
                combat.player_energy = VT_PLAYER_MAX_ENERGY;
            }
            break;
        }
        case GM_UNDOCKING: {
            /* Cinematic: pull out along the station's rear. */
            LONG dx = cam.x - world[2].x;
            LONG dy = cam.y - world[2].y;
            LONG dz = cam.z - world[2].z;
            LONG d = sd > 0 ? sd : 1;
            cam.x += (dx * 60) / d;
            cam.y += (dy * 60) / d;
            cam.z += (dz * 60) / d;
            if (sd > DOCK_APPROACH_RANGE + 500 || mode_timer > 60) {
                game_mode = GM_FLIGHT;
                mode_timer = 0;
            }
            break;
        }
        case GM_GAME_OVER:
        case GM_WIN:
            /* SPACE returns to title (which then SPACE-starts). */
            if ((input_flags & IN_FIRE) && mode_timer > 30) {
                game_mode = GM_TITLE;
                mode_timer = 0;
                input_flags = 0;
                modplay_start_song(MODPLAY_SONG_TITLE);
            }
            break;
        }

    if (!input_flags)
        held_back |= keys;          /* cleared by the code above */
    return 1;
}

static int running = 1, fps_shown;

int main(void)
{
    ULONG frame = 0;
    long old_ssp;

    nf_init();
    old_ssp = Super(0L);
    if (fgfx_init()) {
        Super((void *)old_ssp);
        Cconws("Void Trader needs a Falcon030 (16 bit true colour)\r\n");
        return 1;
    }
    install_palette(NULL);

    e3d_init(SCREEN_W, VIEW_H);
    vt_build_models();
    reset_game();

    /* Audio: modplay owns Paula (ch 0-2 music, ch 3 SFX). Both
     * are optional — if allocation fails the game stays silent. */
    int mod_ok = (modplay_init() == 0);
    log_line("modplay %s", (long)(mod_ok ? "up" : "unavailable"), 0, 0, 0);
    vt_sfx_init();
    if (mod_ok) modplay_start_song(MODPLAY_SONG_TITLE);

    ikbd_init(IKBD_JOYSTICK);
    log_line("START", 0, 0, 0, 0);
    log_line("SYMBOL cam 0x%06lx", (long)&cam, 0, 0, 0);
    log_line("SYMBOL world 0x%06lx", (long)world, 0, 0, 0);
    log_line("SYMBOL combat 0x%06lx", (long)&combat, 0, 0, 0);
    log_line("SYMBOL trade 0x%06lx", (long)&trade, 0, 0, 0);
    log_line("SYMBOL game_mode 0x%06lx", (long)&game_mode, 0, 0, 0);
    log_line("SYMBOL frame_sync 0x%06lx", (long)fgfx_swap, 0, 0, 0);

    const int vbl_hz = fgfx_height() == 240 ? 60 : 50;   /* VGA / RGB */
    long last_vbl = FRCLOCK, fps_vbl = FRCLOCK, tick_acc = 0;
    ULONG fps_frame = 0;
    int last_mode = -1;
    LONG last_credits = -1, last_score = -1;

    while (running) {
        if (ikbd_keys[SC_ESC]) break;

        /* the ticks due at TICK_HZ since the last frame (at most 4),
         * then one rendered frame */
        long now = FRCLOCK;
        tick_acc += (now - last_vbl) * TICK_HZ;
        last_vbl = now;
        int ticks = (int)(tick_acc / vbl_hz);
        tick_acc -= (long)ticks * vbl_hz;
        if (ticks > 4) { ticks = 4; tick_acc = 0; }
        while (ticks-- > 0 && running)
            running = game_tick();
        if (!running) break;

        if (game_mode != last_mode) {
            log_line("STATE %s credits=%ld kills=%ld energy=%ld", (long)mode_name(game_mode),
                     (long)trade.credits, (long)(combat.score / 100), (long)combat.player_energy);
            last_mode = game_mode;
        }
        if (trade.credits != last_credits || combat.score != last_score) {
            if (game_mode != GM_TITLE)
                log_line("SCORE credits=%ld kills=%ld", (long)trade.credits,
                         (long)(combat.score / 100), 0, 0);
            last_credits = trade.credits;
            last_score = combat.score;
        }

        /* --- Render --- */
        LONG sd = station_dist();
        /* main.c draws through its master RastPort mrp */
        struct RastPort *mrpp = fgfx_back();
#define mrp (*mrpp)
        if (game_mode == GM_TITLE) {
            /* Title screen — solid backdrop + big banner + briefing
             * + blinking start prompt. */
            SetAPen(&mrp, 0);
            RectFill(&mrp, 0, 0, SCREEN_W - 1, SCREEN_H - 1);
            SetAPen(&mrp, 120);
            SetDrMd(&mrp, JAM1);
            Move(&mrp, SCREEN_W/2 - 68, 40);
            Text(&mrp, (STRPTR)"V O I D   T R A D E R", 21);
            SetAPen(&mrp, 121);
            Move(&mrp, SCREEN_W/2 - 72, 56);
            Text(&mrp, (STRPTR)"space combat + trading", 22);
            SetAPen(&mrp, 123);
            int y = 92;
            Move(&mrp, 32, y); Text(&mrp, (STRPTR)"Pirates roam the void.", 22); y += 12;
            Move(&mrp, 32, y); Text(&mrp, (STRPTR)"Trade cargo at the station,", 27); y += 12;
            Move(&mrp, 32, y); Text(&mrp, (STRPTR)"kill pirates for bounty, and", 28); y += 12;
            char goal[48];
            sprintf(goal, "earn %ld credits to win.", (long)WIN_CREDITS_TARGET);
            Move(&mrp, 32, y); Text(&mrp, (STRPTR)goal, strlen(goal)); y += 20;
            SetAPen(&mrp, 120);
            Move(&mrp, 32, y); Text(&mrp, (STRPTR)"WASD  fly    QE   roll", 22); y += 10;
            Move(&mrp, 32, y); Text(&mrp, (STRPTR)"R/F   thrust SPACE fire", 23); y += 10;
            Move(&mrp, 32, y); Text(&mrp, (STRPTR)"TAB   dock   U    undock", 24); y += 20;
            if (((mode_timer >> 3) & 1) == 0) {
                SetAPen(&mrp, 125);
                Move(&mrp, SCREEN_W/2 - 76, SCREEN_H - 24);
                Text(&mrp, (STRPTR)"PRESS SPACE TO LAUNCH", 21);
            }
        } else if (game_mode == GM_DOCKED) {
            vt_trade_render(&mrp, &trade);
        } else {
            e3d_render_frame(&mrp, &cam, world, 8);
            vt_combat_render(&mrp, &combat, &cam);
            draw_cockpit(&mrp, fps_shown, ship_speed, combat.player_energy);
            /* Docking prompt when in range in flight. */
            if (game_mode == GM_FLIGHT && sd < DOCK_APPROACH_RANGE) {
                SetAPen(&mrp, 120);
                SetDrMd(&mrp, JAM1);
                if ((mode_timer >> 3) & 1) {
                    Move(&mrp, SCREEN_W/2 - 56, VIEW_H - 12);
                    Text(&mrp, (STRPTR)"TAB TO DOCK", 11);
                }
            }
            if (game_mode == GM_DOCKING) {
                SetAPen(&mrp, 120);
                SetDrMd(&mrp, JAM1);
                Move(&mrp, SCREEN_W/2 - 44, VIEW_H/2 - 20);
                Text(&mrp, (STRPTR)"DOCKING...", 10);
            }
            if (game_mode == GM_UNDOCKING) {
                SetAPen(&mrp, 120);
                SetDrMd(&mrp, JAM1);
                Move(&mrp, SCREEN_W/2 - 48, VIEW_H/2 - 20);
                Text(&mrp, (STRPTR)"LAUNCHING...", 12);
            }
            if (game_mode == GM_GAME_OVER) {
                SetAPen(&mrp, 124);
                SetDrMd(&mrp, JAM1);
                Move(&mrp, SCREEN_W/2 - 40, VIEW_H/2);
                Text(&mrp, (STRPTR)"GAME  OVER", 10);
                if (((mode_timer >> 3) & 1) == 0) {
                    SetAPen(&mrp, 120);
                    Move(&mrp, SCREEN_W/2 - 68, VIEW_H/2 + 16);
                    Text(&mrp, (STRPTR)"SPACE = MAIN MENU", 17);
                }
            }
            if (game_mode == GM_WIN) {
                SetAPen(&mrp, 125);
                SetDrMd(&mrp, JAM1);
                char buf[32];
                Move(&mrp, SCREEN_W/2 - 60, VIEW_H/2 - 8);
                Text(&mrp, (STRPTR)"MISSION COMPLETE", 16);
                sprintf(buf, "%ld credits banked", (long)trade.credits);
                SetAPen(&mrp, 120);
                Move(&mrp, SCREEN_W/2 - 60, VIEW_H/2 + 6);
                Text(&mrp, (STRPTR)buf, strlen(buf));
                if (((mode_timer >> 3) & 1) == 0) {
                    Move(&mrp, SCREEN_W/2 - 68, VIEW_H/2 + 22);
                    Text(&mrp, (STRPTR)"SPACE = MAIN MENU", 17);
                }
            }
        }

#undef mrp
        fgfx_swap();
        frame++;

        if ((frame % 50) == 0) {
            long vbls = FRCLOCK - fps_vbl;
            long fps10 = vbls > 0 ? (10L * vbl_hz * (long)(frame - fps_frame)) / vbls : 0;
            fps_shown = (int)(fps10 / 10);
            log_line("PERF frames=%ld vbls=%ld fps=%ld mode=%ld", (long)(frame - fps_frame),
                     vbls, (long)fps_shown, (long)game_mode);
            fps_vbl = FRCLOCK;
            fps_frame = frame;
        }
    }

    log_line("EXIT frames=%ld credits=%ld", (long)frame, (long)trade.credits, 0, 0);
    vt_sfx_shutdown();
    modplay_stop();
    modplay_cleanup();
    ikbd_exit();
    fgfx_exit();
    Super((void *)old_ssp);
    return 0;
}
