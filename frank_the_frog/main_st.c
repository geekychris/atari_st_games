/*
 * FRANK THE FROG - Atari ST port.
 *
 * Port of the Amiga version (main.c in geekychris/amiga_games): the
 * playfield, lanes, frog and score modules are compiled UNMODIFIED via
 * the Amiga header shims in ../st_port/compat; this file keeps the
 * original game state machine and title/overlay drawing (copied from
 * main.c), with ST input, layered rendering and YM sound.
 *
 * Controls: cursor keys or joystick to hop, Space/Return/fire to start,
 * M toggles music, Esc quits.  With Hatari --natfeats on, events are
 * logged as "FROG ..." lines for agent-driven testing.
 */
#include <osbind.h>
#include <stdio.h>
#include <string.h>

#include "game.h"
#include "playfield.h"
#include "frank.h"
#include "lanes.h"
#include "score.h"
#include "sound.h"
#include "modplay.h"
#include "st_modplay.h"
#include "st_gfx.h"
#include "st_ikbd.h"
#include "natfeats.h"

#define FRCLOCK (*(volatile long *)0x466)

/* Amiga palette from gfx.c (12 bit $0RGB) */
static const UWORD palette[16] = {
	0x000, 0x070, 0x333, 0x00B, 0x0E0, 0x060, 0x830, 0xD00,
	0xED0, 0x08D, 0x606, 0x050, 0x8E4, 0xFFF, 0x003, 0xCC0,
};

static int game_state = STATE_TITLE;
static int transition_frames = 0;
volatile short perf_flags;

static void log_line(const char *fmt, long a, long b, long c, long d)
{
	char buf[128];
	strcpy(buf, "FROG ");
	snprintf(buf + 5, sizeof(buf) - 6, fmt, a, b, c, d);
	strcat(buf, "\n");
	nf_print(buf);
}

/* ---- from the Amiga main.c ---- */
static void draw_title(struct RastPort *rp)
{
    SetAPen(rp, (long)COL_BG);
    RectFill(rp, 0, 0, SCREEN_W - 1, SCREEN_H - 1);

    /* Big frog face */
    SetAPen(rp, (long)COL_FROG);
    RectFill(rp, 130, 40, 190, 90);
    /* Eyes */
    SetAPen(rp, (long)COL_WHITE);
    RectFill(rp, 135, 42, 148, 55);
    RectFill(rp, 170, 42, 183, 55);
    /* Pupils */
    SetAPen(rp, (long)COL_BG);
    RectFill(rp, 140, 46, 144, 51);
    RectFill(rp, 175, 46, 179, 51);
    /* Mouth */
    SetAPen(rp, (long)COL_FROG_DARK);
    Move(rp, 145, 75);
    Draw(rp, 155, 80);
    Draw(rp, 165, 75);
    /* Nostrils */
    WritePixel(rp, 150, 65);
    WritePixel(rp, 168, 65);

    /* Title text */
    SetAPen(rp, (long)COL_FROG);
    SetBPen(rp, (long)COL_BG);
    Move(rp, (SCREEN_W - 14 * 8) / 2, 110);
    Text(rp, (CONST_STRPTR)"FRANK THE FROG", 14L);

    SetAPen(rp, (long)COL_WHITE);
    Move(rp, (SCREEN_W - 19 * 8) / 2, 150);
    Text(rp, (CONST_STRPTR)"Press FIRE to start", 19L);

    /* Controls help */
    SetAPen(rp, (long)COL_ROAD_LINE);
    Move(rp, (SCREEN_W - 24 * 8) / 2, 180);
    Text(rp, (CONST_STRPTR)"Arrows/Joy2 to move ", 20L);
    Move(rp, (SCREEN_W - 24 * 8) / 2, 195);
    Text(rp, (CONST_STRPTR)"Space/Fire  to start", 20L);
    Move(rp, (SCREEN_W - 24 * 8) / 2, 210);
    Text(rp, (CONST_STRPTR)"ESC to quit         ", 20L);
}

static void draw_gameover(struct RastPort *rp)
{
    char buf[32];

    /* Semi-transparent overlay effect */
    SetAPen(rp, (long)COL_BG);
    RectFill(rp, 60, 80, 260, 170);
    SetAPen(rp, (long)COL_WHITE);
    Move(rp, 60, 80); Draw(rp, 260, 80);
    Draw(rp, 260, 170); Draw(rp, 60, 170); Draw(rp, 60, 80);

    SetAPen(rp, (long)COL_CAR_RED);
    SetBPen(rp, (long)COL_BG);
    Move(rp, (SCREEN_W - 9 * 8) / 2, 105);
    Text(rp, (CONST_STRPTR)"GAME OVER", 9L);

    sprintf(buf, "SCORE: %06ld", (long)score_get());
    SetAPen(rp, (long)COL_WHITE);
    Move(rp, (SCREEN_W - (int)strlen(buf) * 8) / 2, 130);
    Text(rp, (CONST_STRPTR)buf, (long)strlen(buf));

    SetAPen(rp, (long)COL_ROAD_LINE);
    Move(rp, (SCREEN_W - 20 * 8) / 2, 155);
    Text(rp, (CONST_STRPTR)"Press FIRE to retry ", 20L);
}

static void draw_level_complete(struct RastPort *rp)
{
    char buf[32];

    SetAPen(rp, (long)COL_BG);
    RectFill(rp, 80, 90, 240, 150);
    SetAPen(rp, (long)COL_FROG);
    Move(rp, 80, 90); Draw(rp, 240, 90);
    Draw(rp, 240, 150); Draw(rp, 80, 150); Draw(rp, 80, 90);

    SetAPen(rp, (long)COL_FROG);
    SetBPen(rp, (long)COL_BG);
    Move(rp, (SCREEN_W - 14 * 8) / 2, 115);
    Text(rp, (CONST_STRPTR)"LEVEL COMPLETE", 14L);

    sprintf(buf, "+1000 BONUS!");
    SetAPen(rp, (long)COL_CAR_YELLOW);
    Move(rp, (SCREEN_W - 12 * 8) / 2, 140);
    Text(rp, (CONST_STRPTR)buf, (long)strlen(buf));
}

/* ---- end of copied code ---- */

/* edge triggered moves, like the Amiga version's joystick/IDCMP
 * handling; presses are latched by the IKBD handler so short taps are
 * never lost however long a frame takes */
static void read_input(int *dx, int *dy, int *fire, int *quit, int *music)
{
	volatile UBYTE *k = ikbd_keys;
	UBYTE j = ikbd_joy1(), hits = ikbd_joy1_hits();

	*dx = *dy = 0;
	if (ikbd_key_hit(SC_LEFT) || (hits & 4))  *dx = -1;
	if (ikbd_key_hit(SC_RIGHT) || (hits & 8)) *dx = 1;
	if (ikbd_key_hit(SC_UP) || (hits & 1))    *dy = -1;
	if (ikbd_key_hit(SC_DOWN) || (hits & 2))  *dy = 1;
	*fire = k[SC_SPACE] || k[SC_RETURN] || (j & 0x80) ||
	        ikbd_key_hit(SC_SPACE) || ikbd_key_hit(SC_RETURN) || (hits & 0x80);
	*quit = k[SC_ESC];
	*music = ikbd_key_hit(SC_M);
}

static int scenery_mode = -1;

static void draw_frame(void)
{
	struct RastPort *hud = gfx_hud(), *rp;
	int mode = (game_state == STATE_TITLE) ? 0 : 1;

	/* scenery: title page, or the static playfield */
	if (mode != scenery_mode)
	{
		gfx_bg_clear();
		if (mode == 0)
			draw_title(gfx_bg());
		else
			playfield_draw(gfx_bg());
		gfx_bg_to_screens();
		scenery_mode = mode;
	}

	/* HUD layer: score + overlays, only when something it shows changed */
	{
		static ULONG last_key = 1;
		ULONG key = ((ULONG)score_get() << 8) ^ (score_lives() << 4) ^
		            (score_level() << 20) ^ (game_state << 28) ^ mode ^ perf_flags;
		if (key == last_key)
			gfx_hud_keep();
		else
		{
			last_key = key;
			if (mode && !(perf_flags & 2))
			{
				score_draw(hud);
				if (game_state == STATE_LEVEL)
					draw_level_complete(hud);
				else if (game_state == STATE_GAMEOVER)
					draw_gameover(hud);
			}
			gfx_hud_commit();
		}
	}

	/* sprites: the 10 traffic/river lanes are redrawn completely every
	 * frame (band copy from the background, no dirty rectangles), the
	 * frog and home frogs are normal sprites */
	gfx_restore_back();
	rp = gfx_back();
	if (mode && !(perf_flags & 1))
	{
		/* river: solid water; road: solid grey plus the dashed divider
		 * rows from the background (fills are ~2x faster than copies) */
		gfx_fill_band(ROW_RIVER_5 * TILE_H, ROW_RIVER_1 * TILE_H + TILE_H - 1, COL_WATER);
		gfx_fill_band(ROW_ROAD_5 * TILE_H, ROW_ROAD_1 * TILE_H + TILE_H - 1, COL_ROAD);
		{
			int row;
			for (row = ROW_ROAD_5; row < ROW_ROAD_1; row++)
				gfx_copy_band(row * TILE_H + TILE_H / 2, row * TILE_H + TILE_H / 2 + 1);
		}
		lanes_draw(gfx_back_nomark());
		lanes_draw_homes(rp);		/* ST port: home frogs as marked sprites */
		if (game_state == STATE_PLAYING || game_state == STATE_DYING)
			frank_draw(rp);
	}
}

/* one 50 Hz game step: the Amiga main loop body, minus drawing */
static void game_step(int joy_dx, int joy_dy, int joy_fire, int *prev_fire)
{
	int death_done, ride_dx;

	switch (game_state) {
	case STATE_TITLE:
		if (joy_fire && !*prev_fire) {
			game_state = STATE_PLAYING;
			score_init();
			frank_init();
			lanes_init(1);
			lanes_reset_home();
			log_line("START_GAME", 0, 0, 0, 0);
		}
		break;

	case STATE_PLAYING:
		if (joy_dx || joy_dy) {
			int old_highest = frank_highest_row();
			frank_move(joy_dx, joy_dy);
			sound_hop();
			if (frank_highest_row() < old_highest)
				score_add(10);
		}
		lanes_tick();
		if (frank_row() >= ROW_RIVER_5 && frank_row() <= ROW_RIVER_1) {
			if (!lanes_check_river(frank_x(), frank_y(), &ride_dx)) {
				log_line("SPLASH row=%ld col=%ld", frank_row(), frank_col(), 0, 0);
				frank_start_death();
				game_state = STATE_DYING;
				sound_splash();
			} else if (ride_dx != 0) {
				int new_x = frank_x() + ride_dx;
				if (new_x < -4 || new_x + TILE_W > SCREEN_W + 4) {
					log_line("CARRIED_OFF", 0, 0, 0, 0);
					frank_start_death();
					game_state = STATE_DYING;
					sound_splash();
				} else {
					frank_set_x(new_x);
				}
			}
		}
		if (game_state == STATE_PLAYING &&
		    frank_row() >= ROW_ROAD_5 && frank_row() <= ROW_ROAD_1) {
			if (lanes_check_car(frank_x(), frank_y())) {
				log_line("SPLAT row=%ld col=%ld", frank_row(), frank_col(), 0, 0);
				frank_start_death();
				game_state = STATE_DYING;
				sound_splat();
			}
		}
		if (game_state == STATE_PLAYING && frank_row() == ROW_HOME) {
			int slot = lanes_check_home(frank_x());
			if (slot >= 0) {
				lanes_fill_home(slot);
				score_add(50);
				sound_home();
				log_line("HOME slot=%ld score=%ld", slot, score_get(), 0, 0);
				if (lanes_all_home()) {
					game_state = STATE_LEVEL;
					transition_frames = 60;
					sound_levelup();
					log_line("LEVEL_COMPLETE level=%ld", score_level(), 0, 0, 0);
				} else {
					frank_init();
				}
			} else {
				frank_start_death();
				game_state = STATE_DYING;
				sound_splash();
			}
		}
		break;

	case STATE_DYING:
		death_done = !frank_die_tick();
		lanes_tick();
		if (death_done) {
			score_lose_life();
			if (score_lives() <= 0) {
				game_state = STATE_GAMEOVER;
				transition_frames = 30;
				sound_gameover();
				log_line("GAME_OVER score=%ld", score_get(), 0, 0, 0);
			} else {
				frank_init();
				game_state = STATE_PLAYING;
			}
		}
		break;

	case STATE_LEVEL:
		transition_frames--;
		if (transition_frames <= 0) {
			score_next_level();
			lanes_reset_home();
			lanes_init(score_level());
			frank_init();
			game_state = STATE_PLAYING;
			log_line("LEVEL level=%ld", score_level(), 0, 0, 0);
		}
		break;

	case STATE_GAMEOVER:
		lanes_tick();
		if (transition_frames > 0)
			transition_frames--;
		else if (joy_fire && !*prev_fire)
			game_state = STATE_TITLE;
		break;
	}
	*prev_fire = joy_fire;
}

int main(void)
{
	long old_ssp, last_vbl, frames = 0;
	int prev_fire = 0, last_state = -1;

	nf_init();
	old_ssp = Super(0L);
	if (!gfx_init(palette, 16))
	{
		Super((void *)old_ssp);
		Cconws("Not enough memory\r\n");
		return 1;
	}
	/* steady 16.7 fps with three 50 Hz game updates per displayed frame:
	 * redrawing ~26 lane objects each frame is bound by 8 MHz bus RMW
	 * cycles on the plain ST (a 16 MHz Mega STE doesn't help; the STE
	 * blitter would) */
	gfx_set_frame_vbls(3);
	modplay_init();
	sound_init();
	modplay_start();
	ikbd_init(IKBD_JOYSTICK);

	score_init();
	frank_init();
	lanes_init(1);

	log_line("START", 0, 0, 0, 0);
	log_line("SYMBOL frame_sync 0x%06lx", (long)gfx_swap, 0, 0, 0);
	log_line("SYMBOL game_state 0x%06lx", (long)&game_state, 0, 0, 0);
	log_line("SYMBOL perf_flags 0x%06lx", (long)&perf_flags, 0, 0, 0);

	last_vbl = FRCLOCK;
	for (;;)
	{
		int dx, dy, fire, quit, music;
		long now = FRCLOCK;
		int steps = (int)(now - last_vbl);
		last_vbl = now;
		/* one game update per 50 Hz VBL (three per displayed frame) */
		if (steps < 1) steps = 1;
		if (steps > 3) steps = 3;

		read_input(&dx, &dy, &fire, &quit, &music);
		if (quit)
			break;
		if (music)
			modplay_toggle();

		while (steps-- > 0)
		{
			game_step(dx, dy, fire, &prev_fire);
			dx = dy = 0;		/* a hop happens once */
			modplay_tick();
		}
		if (game_state != last_state)
		{
			static const char *names[] = { "TITLE", "PLAYING", "DYING", "LEVEL", "GAMEOVER" };
			log_line("STATE %s score=%ld lives=%ld level=%ld", (long)names[game_state],
			         score_get(), score_lives(), score_level());
			last_state = game_state;
		}

		draw_frame();
		gfx_swap();
		if (++frames % 100 == 0)
		{
			static long perf_vbl;
			long t = FRCLOCK;
			if (perf_vbl)
				log_line("PERF frames=100 vbls=%ld state=%ld", t - perf_vbl, game_state, 0, 0);
			perf_vbl = t;
		}
	}

	log_line("EXIT frames=%ld", frames, 0, 0, 0);
	modplay_cleanup();
	ikbd_exit();
	gfx_exit();
	Super((void *)old_ssp);
	return 0;
}
