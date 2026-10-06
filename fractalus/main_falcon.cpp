/*
 * FRACTALUS - Atari Falcon030 port.
 *
 * Port of the Amiga AGA version (main.cpp in geekychris/amiga_games):
 * terrain, game, pilots, combat and the raycasting renderer are the
 * original C++; render.cpp has __MINT__ branches for the Falcon's 16 bit
 * chunky true colour screen (falcon_port/fgfx).  This file is main.cpp
 * with the AmigaOS parts (libraries, IDCMP, singleton port, DateStamp)
 * replaced by IKBD input, the VBL counter and NatFeats logging; the
 * game loop body (attract / title / restart logic) is copied verbatim.
 *
 * Controls (as on the Amiga): A/D or cursor left/right turn, W/up thrust,
 * S/down brake, Q/Z pitch, Space fire/start, Return restart, L land,
 * Esc quit.  Joystick: directions turn/thrust/brake, fire fires.
 */
#include <osbind.h>
#include <stdio.h>
#include <string.h>

extern "C" {
#include "bridge_client.h"
#include "st_ikbd.h"
}
#include "render.h"
#include "terrain.h"
#include "game.h"
#include "pilots.h"
#include "combat.h"
#include "sfx.h"
#include "modplay.h"

#define FRCLOCK (*(volatile long *)0x466)

static Terrain   g_terrain;
static Renderer  g_renderer;
static Game      g_game;
static PilotList g_pilots;
static Combat    g_combat;
static Sfx       g_sfx;

static GameState g_state;
static ULONG     g_frame_count = 0;
static LONG      g_force_restart = 0;

/* level-triggered input from the IKBD key state (the Amiga version
 * tracks the same state from RAWKEY up/down messages) */
static UWORD read_input(void)
{
    volatile UBYTE *k = ikbd_keys;
    UBYTE j = ikbd_joy1();
    UWORD f = 0;
    if (k[SC_A] || k[SC_LEFT]  || (j & 4))  f |= INPUT_LEFT;
    if (k[SC_D] || k[SC_RIGHT] || (j & 8))  f |= INPUT_RIGHT;
    if (k[SC_W] || k[SC_UP]    || (j & 1))  f |= INPUT_THRUST;
    if (k[SC_S] || k[SC_DOWN]  || (j & 2))  f |= INPUT_BRAKE;
    if (k[SC_Q])                            f |= INPUT_UP;
    if (k[SC_Z])                            f |= INPUT_DOWN;
    if (k[SC_SPACE] || (j & 0x80))          f |= INPUT_FIRE;
    if (k[SC_RETURN])                       f |= INPUT_RESTART;
    if (k[SC_L])                            f |= INPUT_LAND;
    return f;
}

extern LONG g_bench_mask;
extern LONG g_debug_all_jaggis;

int main(void)
{
    Terrain   &terrain  = g_terrain;
    Renderer  &renderer = g_renderer;
    Game      &game     = g_game;
    PilotList &pilots   = g_pilots;
    Combat    &combat   = g_combat;
    Sfx       &sfx      = g_sfx;
    const int bridge_ok = 1;          /* AB_* logging goes to NatFeats */
    UWORD input_flags = 0;
    long old_ssp;

    nf_init();
    old_ssp = Super(0L);              /* IKBD, VBL counter */

    AB_I("fractalus (Falcon port) starting");
    /* addresses for agents (agent API /mem, GDB) */
    ab_log("S", "g_state 0x%06lx", (long)&g_state);
    ab_log("S", "pilots 0x%06lx", (long)&g_pilots);
    ab_log("S", "pilots_rescued 0x%06lx", (long)&g_state.pilots_rescued);
    ab_log("S", "rescue_state 0x%06lx", (long)&g_state.rescue_state);
    ab_log("S", "mode 0x%06lx", (long)&g_state.mode);
    ab_log("S", "bench_mask 0x%06lx", (long)&g_bench_mask);
    ab_log("S", "frame_sync 0x%06lx", (long)fgfx_swap);

    render_init_math();
    int mod_ok = (modplay_init() == 0);
    if (mod_ok) modplay_start_song(MODPLAY_SONG_TITLE);
    sfx.init();
    game.bind_sfx(&sfx);
    combat.bind_sfx(&sfx);

    auto reset_world = [&](ULONG seed) {
        g_state.seed = seed;
        terrain.generate(seed);
        game.init(&g_state, &terrain, &pilots, &combat);
        pilots.spawn(FX16_TOINT(g_state.ship.x),
                     FX16_TOINT(g_state.ship.z),
                     seed ^ 0xA5A5A5A5UL, terrain);
        combat.init(seed ^ 0x33445566UL,
                    FX16_TOINT(g_state.ship.x),
                    g_state.ship.y,
                    FX16_TOINT(g_state.ship.z));
        if (bridge_ok) {
            LONG jaggis = 0;
            for (LONG pi = 0; pi < pilots.count(); pi++)
                if (pilots[pi].is_jaggi) jaggis++;
            AB_I("mission ready: %ld/12 jaggis hiding (debug=%ld)",
                 (long)jaggis, (long)g_debug_all_jaggis);
        }
    };
    reset_world(0xC0FFEE01UL);

    int err = renderer.open_display();
    if (err) {
        Super((void *)old_ssp);
        printf("display open failed: %d (needs a Falcon)\n", err);
        return 20;
    }
    ikbd_init(IKBD_JOYSTICK);
    AB_I("goal: rescue %ld of %ld pilots to win",
         (long)MISSION_WIN_PILOTS, (long)pilots.count());

    long last_vbl = FRCLOCK, fps_vbl = FRCLOCK, tick_acc = 0;
    const int vbl_hz = fgfx_height() == 240 ? 60 : 50;   /* VGA / RGB */
    ULONG fps_frame = 0;
    while (g_state.running) {
        input_flags = read_input();
        if (ikbd_keys[SC_ESC]) { g_state.running = 0; break; }

        /* The game advances one tick per loop on the Amiga, tuned for
         * ~30 fps.  Rendering here takes longer than that, so run the
         * ticks that are due at 30 Hz (the VBL is 60 Hz on VGA, 50 Hz on
         * RGB; up to 4 per frame) before drawing once. */
        long now = FRCLOCK;
        tick_acc += (now - last_vbl) * 30;
        last_vbl = now;
        int ticks = (int)(tick_acc / vbl_hz);
        tick_acc -= (long)ticks * vbl_hz;
        if (ticks < 1) ticks = 1;
        if (ticks > 4) { ticks = 4; tick_acc = 0; }

        for (int t = 0; t < ticks; t++) {
        /* ---- loop body copied from the Amiga main.cpp ---- */
            UBYTE prev_mode = g_state.mode;
            game.tick(input_flags);
            /* Music silences when a mission ends — score screen shouldn't
             * fight either song. Title music resumes on end-screen -> title
             * transition (handled at reset_world time). */
            if (prev_mode == GM_PLAYING && g_state.mode != GM_PLAYING) {
                modplay_stop();
            }

            /* Edge-detect keypresses (this-frame ^ prev). Used below so
             * a SPACE that broke us out of ATTRACT doesn't ALSO immediately
             * fire title_go on the same held press — the user has to
             * release and press again to launch a mission. Same guard also
             * covers title_go / end_go against spurious level-triggered
             * restarts when a key was already down at mode transition. */
            static UWORD prev_input_flags = 0;
            UWORD input_edge = input_flags & ~prev_input_flags;

            /* ATTRACT (idle demo) transitions:
             *   TITLE, no input for ATTRACT_IDLE_FRAMES  -> GM_ATTRACT
             *   ATTRACT, any real keypress (edge)        -> GM_TITLE
             *
             * On the ATTRACT break we also zero input_flags so title_go
             * (still evaluated below) can't fire on the same frame — the
             * player just wanted out of the demo, not to launch a mission. */
            static UWORD attract_idle = 0;
            const UWORD ATTRACT_IDLE_FRAMES = 150;   /* ~5 sec at 30 fps */
            if (g_state.mode == GM_TITLE) {
                if (input_flags == 0) {
                    if (attract_idle < 65535) attract_idle++;
                    if (attract_idle >= ATTRACT_IDLE_FRAMES) {
                        ULONG demo_seed =
                            g_state.seed * 1103515245UL + 12345UL;
                        if (bridge_ok) AB_I("attract: idle -> demo");
                        reset_world(demo_seed);
                        g_state.mode = GM_ATTRACT;
                        attract_idle = 0;
                    }
                } else {
                    attract_idle = 0;
                }
            } else if (g_state.mode == GM_ATTRACT) {
                if (input_edge != 0) {
                    if (bridge_ok) AB_I("attract: input -> title");
                    g_state.mode = GM_TITLE;
                    g_state.state_timer = 0;
                    attract_idle = 0;
                    input_flags = 0;
                    input_edge = 0;
                }
            }

            /* End-screen restart: after the mission has ended, if SPACE is
             * pressed AND the end screen has been showing for >30 ticks
             * (so a mid-airlock fire doesn't accidentally restart), regen
             * the world from a fresh seed. */
            /* Start / restart triggers — all use input_edge so a key that
             * was already held during a mode change doesn't count:
             *   TITLE      : SPACE (INPUT_FIRE) — the "go" key
             *   WIN/LOSE   : RETURN (INPUT_RESTART)
             *   any mode   : force_restart bridge var
             * Debounced: state_timer > 30 avoids accidental double-fires. */
            UWORD title_go =
                (g_state.mode == GM_TITLE
                 && (input_edge & INPUT_FIRE)
                 && g_state.state_timer > 15);
            UWORD end_go =
                ((g_state.mode == GM_WIN || g_state.mode == GM_LOSE)
                 && (input_edge & INPUT_RESTART)
                 && g_state.state_timer > 30);
            if (title_go || end_go || g_force_restart) {
                ULONG next_seed = g_state.seed * 1103515245UL + 12345UL;
                if (bridge_ok) AB_I("restart: new mission, seed=%ld",
                                    (long)next_seed);
                reset_world(next_seed);
                g_state.mode = GM_PLAYING;   /* leave title / end screen */
                input_flags = 0;
                g_force_restart = 0;
                modplay_start_song(MODPLAY_SONG_GAME);   /* metal for combat */
            }

            prev_input_flags = input_flags;

        /* ---- end of copied loop body ---- */
        modplay_tick();
        sfx.tick();
        }

        renderer.render(g_state, terrain, pilots, combat);
        g_frame_count++;

        if ((g_frame_count % 50) == 0) {
            long vbls = FRCLOCK - fps_vbl;
            long fps10 = vbls > 0 ? (10L * vbl_hz * (long)(g_frame_count - fps_frame)) / vbls : 0;
            AB_I("hb: frame=%ld fps=%ld.%ld mode=%ld resc=%ld sh=%ld fuel=%ld saved=%ld score=%ld",
                 (long)g_frame_count, fps10 / 10, fps10 % 10, (long)g_state.mode,
                 (long)g_state.rescue_state, (long)g_state.shield,
                 (long)g_state.fuel, (long)g_state.pilots_rescued,
                 (long)g_state.score);
            fps_vbl = FRCLOCK;
            fps_frame = g_frame_count;
        }
    }

    AB_I("fractalus shutting down (%ld frames)", (long)g_frame_count);
    ikbd_exit();
    sfx.shutdown();
    modplay_stop();
    modplay_cleanup();
    renderer.close_display();
    Super((void *)old_ssp);
    return 0;
}
