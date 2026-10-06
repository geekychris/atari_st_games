/*
 * URANUS LANDER - Atari ST port.
 *
 * Port of the Amiga version (main.c in geekychris/amiga_games): same
 * game logic (game.c) and drawing code (draw.c) on top of an ST
 * graphics layer (st_gfx.c), IKBD input (st_input.c) and YM2149
 * sound (st_sound.c).
 *
 * Controls:
 *   Left/Right, A/D or Joy L/R   Rotate ship
 *   Space, W, Joy fire/up        Thrust
 *   M                            Music on/off
 *   Esc                          Quit
 *
 * With Hatari --natfeats on, game events are logged to the host
 * ("URANUS ..." lines, see README.md) for agent-driven testing, and
 * the addresses of the game state and tunables are reported so they
 * can be inspected or tweaked through the debugger / memory API (the
 * Amiga version exposes them through AmigaBridge instead).
 */
#include <osbind.h>
#include <stdio.h>
#include <string.h>
#include "game.h"
#include "draw.h"
#include "input.h"
#include "sound.h"
#include "st_sound.h"
#include "natfeats.h"   /* ../st_port */

#define FRCLOCK (*(volatile long *)0x466)	/* VBL counter */

/* Same palette as the Amiga version (12 bit $0RGB) */
static const UWORD palette[16] = {
	0x002, 0xEEE, 0x322, 0x644, 0x876, 0x226, 0xEC0, 0x0C0,
	0xD00, 0xF80, 0x0CE, 0x08A, 0xD0D, 0xFE0, 0x533, 0xFFF,
};

static GameState gs;

/* rendering switches for performance experiments, poked through the
 * agent API memory endpoint: 1 = no stars, 2 = no HUD, 4 = no ship &
 * particles, 8 = no sprite restore
 */
volatile short perf_flags;

static const char *state_names[] = {
	"TITLE", "PLAYING", "LANDED", "CRASHING", "GAMEOVER", "ENTER_NAME"
};

static void log_line(const char *fmt, long a, long b, long c, long d)
{
	char buf[128];
	strcpy(buf, "URANUS ");
	snprintf(buf + 7, sizeof(buf) - 8, fmt, a, b, c, d);
	strcat(buf, "\n");
	nf_print(buf);
}

/* identifies what the background buffer must show */
static long scenery_key(void)
{
	long key = 0;
	int i;
	if (gs.state == STATE_TITLE)
		return -1;
	/* rotate-xor hash: no 32 bit multiply (a library call on 68000) */
	for (i = 0; i < TERRAIN_W; i++)
		key = ((key << 5) | ((unsigned long)key >> 27)) ^ gs.terrain_y[i];
	return key ^ gs.level;
}

static void build_background(void)
{
	struct RastPort *bg = gfx_bg();
	gfx_bg_clear();
	if (gs.state == STATE_TITLE)
		draw_title_static(bg, &gs);
	else
		draw_terrain(bg, &gs);
	gfx_bg_to_screens();
}

static void draw_frame(void)
{
	struct RastPort *hud = gfx_hud(), *rp;

	/* HUD layer: same draw calls every frame, only changes get rendered */
	if (perf_flags & 2)
		;
	else if (gs.state == STATE_TITLE)
		draw_title_anim(hud, &gs);
	else
	{
		draw_hud(hud, &gs);
		if (gs.state == STATE_LANDED)
			draw_landed(hud, &gs);
		else if (gs.state == STATE_CRASHING)
			draw_crash(hud, &gs);
		else if (gs.state == STATE_GAMEOVER)
			draw_gameover(hud, &gs);
		else if (gs.state == STATE_ENTER_NAME)
			draw_enter_name(hud, &gs);
	}
	gfx_hud_commit();

	/* sprite layer */
	if (!(perf_flags & 8))
		gfx_restore_back();
	rp = gfx_back();
	if (!(perf_flags & 1))
		draw_stars(rp, &gs);
	if (gs.state == STATE_TITLE || (perf_flags & 4))
		return;
	draw_particles(rp, &gs);
	if (gs.state != STATE_CRASHING || gs.ship.alive)
		draw_ship(rp, &gs);
}

int main(void)
{
	long old_ssp;
	int running = 1, music_key_held = 0, was_thrusting = 0;
	int startup_delay = 30, last_state = -1;
	long last_key = 0, last_vbl, lag_frames = 0, frames = 0;

	nf_init();
	old_ssp = Super(0L);	/* hardware access: screen, PSG, IKBD */

	game_init_tables();
	if (!gfx_init(palette, 16))
	{
		Super((void *)old_ssp);
		Cconws("Not enough memory\r\n");
		return 1;
	}
	sound_init();
	planet_gfx_init();
	input_init();
	/* an 8 MHz ST draws this game in ~1.8 PAL frames: run a steady
	 * 25 fps with two 50 Hz game updates per frame (same game speed
	 * as the Amiga) instead of jittering between 50 and 17 fps */
	gfx_set_frame_vbls(2);

	game_init(&gs);
	gs.state = STATE_TITLE;
	input_reset();

	log_line("START", 0, 0, 0, 0);
	log_line("SYMBOL gs 0x%06lx", (long)&gs, 0, 0, 0);
	log_line("SYMBOL g_tune 0x%06lx", (long)&g_tune, 0, 0, 0);
	/* field addresses, so tools don't depend on struct layout */
	log_line("SYMBOL ship 0x%06lx", (long)&gs.ship, 0, 0, 0);
	log_line("SYMBOL pads 0x%06lx", (long)&gs.pads, 0, 0, 0);
	log_line("SYMBOL num_pads 0x%06lx", (long)&gs.num_pads, 0, 0, 0);
	log_line("SYMBOL terrain_y 0x%06lx", (long)&gs.terrain_y, 0, 0, 0);
	log_line("SYMBOL state 0x%06lx", (long)&gs.state, 0, 0, 0);
	log_line("SYMBOL score 0x%06lx", (long)&gs.score, 0, 0, 0);
	log_line("SYMBOL lives 0x%06lx", (long)&gs.lives, 0, 0, 0);
	log_line("SYMBOL level 0x%06lx", (long)&gs.level, 0, 0, 0);
	/* called once per frame between update and display: a breakpoint
	 * here gives tools a consistent, frame-synchronous view */
	log_line("SYMBOL frame_sync 0x%06lx", (long)gfx_swap, 0, 0, 0);
	log_line("SYMBOL perf_flags 0x%06lx", (long)&perf_flags, 0, 0, 0);

	last_key = scenery_key() + 1;	/* force first background build */
	last_vbl = FRCLOCK;

	while (running)
	{
		UWORD inp = input_read();
		InputState is;
		long now = FRCLOCK;
		int steps = (int)(now - last_vbl);
		last_vbl = now;
		/* keep game speed at 50 updates/s even if a frame took longer */
		if (steps < 1) steps = 1;
		if (steps > 3) steps = 3;
		if (steps > 1)
			lag_frames += steps - 1;

		if (inp & INPUT_ESC)
			running = 0;

		/* M toggles music (edge triggered) */
		if (inp & INPUT_MUSIC)
		{
			if (!music_key_held)
			{
				music_key_held = 1;
				music_enable(!music_enabled());
				log_line("MUSIC %ld", music_enabled(), 0, 0, 0);
			}
		}
		else
			music_key_held = 0;

		is.left   = (inp & INPUT_LEFT)   ? 1 : 0;
		is.right  = (inp & INPUT_RIGHT)  ? 1 : 0;
		is.thrust = (inp & INPUT_THRUST) ? 1 : 0;
		is.up     = (inp & INPUT_UP)     ? 1 : 0;
		is.down   = (inp & INPUT_DOWN)   ? 1 : 0;
		is.quit   = (inp & INPUT_ESC)    ? 1 : 0;

		while (steps-- > 0)
		{
			if (startup_delay > 0)
			{
				startup_delay--;
				memset(&is, 0, sizeof(is));
			}
			if (gs.state == STATE_TITLE)
			{
				stars_update(&gs);
				gs.frame++;
				if (is.thrust || is.up)
				{
					game_init(&gs);
					gs.state = STATE_PLAYING;
					startup_delay = 15;
				}
				continue;
			}
			stars_update(&gs);
			game_update(&gs, &is);

			if (gs.ev_thrust)
			{
				if (!was_thrusting)
					sfx_thrust_play();
				was_thrusting = 1;
			}
			else
			{
				if (was_thrusting)
					sfx_thrust_stop();
				was_thrusting = 0;
			}
			if (gs.ev_crash)
			{
				sfx_crash_play();
				log_line("CRASH level=%ld score=%ld lives=%ld", gs.level, gs.score, gs.lives, 0);
			}
			if (gs.ev_land)
			{
				sfx_land_play();
				log_line("LANDED level=%ld pad=%ld bonus=%ld score=%ld", gs.level,
				         gs.landed_pad, gs.land_bonus, gs.score);
			}
			if (gs.ev_low_fuel)
				sfx_beep_play();
		}

		if (gs.state != last_state)
		{
			log_line("STATE %s level=%ld score=%ld lives=%ld", (long)state_names[gs.state],
			         gs.level, gs.score, gs.lives);
			last_state = gs.state;
		}

		{
			long key = scenery_key();
			if (key != last_key)
			{
				build_background();
				last_key = key;
			}
		}
		draw_frame();
		sound_tick();
		gfx_swap();
		frames++;
		/* frame rate report for agents: VBLs per 100 rendered frames */
		if (frames % 100 == 0)
		{
			static long perf_vbl;
			long t = FRCLOCK;
			if (perf_vbl)
				log_line("PERF frames=100 vbls=%ld state=%ld", t - perf_vbl, gs.state, 0, 0);
			perf_vbl = t;
		}
	}

	log_line("EXIT frames=%ld late_frames=%ld", frames, lag_frames, 0, 0);
	sound_cleanup();
	input_exit();
	planet_gfx_cleanup();
	gfx_exit();
	Super((void *)old_ssp);
	return 0;
}
