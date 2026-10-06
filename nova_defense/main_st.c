/*
 * NOVA DEFENSE - Atari ST port.
 *
 * Port of the Amiga version (main.c in geekychris/amiga_games): same
 * game logic (game.c) and drawing code (draw.c, split into HUD and
 * sprite layers) on the shared ST layer in ../st_port.
 *
 * Controls: mouse or cursor keys / A,D / joystick to move; left mouse
 * button, Space, Alt or joystick fire to shoot; Esc quits.
 *
 * With Hatari --natfeats on, events and symbol addresses are logged
 * ("NOVA ..." lines) for agent-driven testing.
 */
#include <osbind.h>
#include <stdio.h>
#include <string.h>
#include "game.h"
#include "draw.h"
#include "input.h"
#include "sound.h"
#include "natfeats.h"

#define FRCLOCK (*(volatile long *)0x466)

/* Amiga palette (LoadRGB32 values reduced to 12 bit $0RGB) */
static const UWORD palette[16] = {
	0x000, 0xFFF, 0x0D0, 0x080, 0xE22, 0xFA0, 0x0EE, 0xE2E,
	0xFF0, 0x44F, 0x88F, 0xF64, 0x4F4, 0x888, 0xF80, 0xAAA,
};

static GameState gs;
static InputState input;
volatile short perf_flags;	/* 1 = no sprites, 2 = no HUD */

static void log_line(const char *fmt, long a, long b, long c, long d)
{
	char buf[128];
	strcpy(buf, "NOVA ");
	snprintf(buf + 5, sizeof(buf) - 6, fmt, a, b, c, d);
	strcat(buf, "\n");
	nf_print(buf);
}

static const char *state_names[] = { "TITLE", "PLAYING", "DYING", "GAMEOVER", "WAVE_CLEAR" };

/* everything the HUD layer depends on */
static ULONG last_hud_key = 1;
static ULONG hud_key(void)
{
	ULONG k = gs.score * 31 + gs.hiscore;
	int s, i;
	k = (k << 7) ^ (gs.lives << 4) ^ (gs.wave << 8) ^ gs.state ^ (perf_flags << 20);
	if (gs.state == STATE_TITLE)
		k ^= (gs.title_blink >> 3) << 24;	/* blink + alien animation */
	else if (gs.state == STATE_GAMEOVER)
		k ^= (gs.state_timer & 8) << 24;
	for (s = 0; s < SHIELD_COUNT; s++)
	{
		const ULONG *p = (const ULONG *)&gs.shields[s].pixels[0][0];
		for (i = 0; i < SHIELD_H * SHIELD_W / 4; i++)
			k = (k << 1 | k >> 31) ^ p[i];
	}
	return k;
}

static void draw_frame(void)
{
	struct RastPort *hud = gfx_hud(), *rp;

	/* skip the HUD layer entirely when nothing it shows has changed */
	ULONG key = hud_key();
	if (key == last_hud_key)
		gfx_hud_keep();
	else
	{
		last_hud_key = key;
		if (!(perf_flags & 2))
		{
			if (gs.state == STATE_TITLE)
				draw_title(hud, &gs);
			else if (gs.state == STATE_GAMEOVER)
				draw_gameover(hud, &gs);
			else
				draw_game_hud(hud, &gs);
		}
		gfx_hud_commit();
	}

	gfx_restore_back();
	rp = gfx_back();
	if (!(perf_flags & 1) && gs.state != STATE_TITLE && gs.state != STATE_GAMEOVER)
		draw_game_sprites(rp, &gs);
}

int main(void)
{
	long old_ssp, last_vbl, frames = 0;
	int running = 1, startup_delay = 10, last_state = -1;
	long last_score = -1, last_wave = -1;

	nf_init();
	old_ssp = Super(0L);

	if (!gfx_init(palette, 16))
	{
		Super((void *)old_ssp);
		Cconws("Not enough memory\r\n");
		return 1;
	}
	gfx_bg_clear();
	gfx_bg_to_screens();
	/* steady 25 fps (two 50 Hz game updates per frame); a full swarm,
	 * shields and bullets don't fit one 8 MHz 68000 frame from C */
	gfx_set_frame_vbls(2);
	sound_init();
	game_init(&gs);
	input_init();
	memset(&input, 0, sizeof(input));
	game_srand(FRCLOCK * 7919);

	log_line("START", 0, 0, 0, 0);
	log_line("SYMBOL gs 0x%06lx", (long)&gs, 0, 0, 0);
	log_line("SYMBOL state 0x%06lx", (long)&gs.state, 0, 0, 0);
	log_line("SYMBOL player_x 0x%06lx", (long)&gs.player_x, 0, 0, 0);
	log_line("SYMBOL swarm 0x%06lx", (long)&gs.swarm, 0, 0, 0);
	log_line("SYMBOL score 0x%06lx", (long)&gs.score, 0, 0, 0);
	log_line("SYMBOL lives 0x%06lx", (long)&gs.lives, 0, 0, 0);
	log_line("SYMBOL wave 0x%06lx", (long)&gs.wave, 0, 0, 0);
	log_line("SYMBOL alive 0x%06lx", (long)&gs.swarm.alive, 0, 0, 0);
	log_line("SYMBOL grid_x 0x%06lx", (long)&gs.swarm.grid_x, 0, 0, 0);
	log_line("SYMBOL grid_y 0x%06lx", (long)&gs.swarm.grid_y, 0, 0, 0);
	log_line("SYMBOL swarm_dir 0x%06lx", (long)&gs.swarm.dir, 0, 0, 0);
	log_line("SYMBOL player_bullet 0x%06lx", (long)&gs.player_bullet, 0, 0, 0);
	log_line("SYMBOL alien_bullets 0x%06lx", (long)&gs.alien_bullets, 0, 0, 0);
	log_line("SYMBOL sizeof_bool %ld", (long)sizeof(BOOL), 0, 0, 0);
	log_line("SYMBOL frame_sync 0x%06lx", (long)gfx_swap, 0, 0, 0);
	log_line("SYMBOL perf_flags 0x%06lx", (long)&perf_flags, 0, 0, 0);

	last_vbl = FRCLOCK;
	while (running)
	{
		long now = FRCLOCK;
		int steps = (int)(now - last_vbl);
		last_vbl = now;
		if (steps < 1) steps = 1;
		if (steps > 3) steps = 3;

		input_read(&input);
		if (startup_delay > 0)
		{
			startup_delay--;
			input.fire_pressed = FALSE;
		}
		if (input.quit)
			break;

		/* one game update per 50 Hz VBL: same speed as the Amiga */
		while (steps-- > 0)
		{
			game_update(&gs, &input);
			input.fire_pressed = FALSE;	/* edge only once */
			input.mouse_dx = 0;

			if (gs.ev_shoot)      sound_play_shoot();
			if (gs.ev_alien_hit)  sound_play_alien_explode();
			if (gs.ev_player_hit) sound_play_player_explode();
			if (gs.ev_ufo_hit)    sound_play_alien_explode();
			if (gs.ev_march)      sound_play_march(gs.march_note);
			sound_play_ufo(gs.ufo.active);
			sound_update();
		}

		if (gs.state != last_state)
		{
			log_line("STATE %s wave=%ld score=%ld lives=%ld", (long)state_names[gs.state],
			         gs.wave, gs.score, gs.lives);
			last_state = gs.state;
		}
		if (gs.score != last_score || gs.wave != last_wave)
		{
			if (gs.state == STATE_PLAYING || gs.state == STATE_WAVE_CLEAR)
				log_line("SCORE %ld wave=%ld aliens=%ld", gs.score, gs.wave,
				         gs.swarm.alive_count, 0);
			last_score = gs.score;
			last_wave = gs.wave;
		}

		draw_frame();
		gfx_swap();
		frames++;
		if (frames % 100 == 0)
		{
			static long perf_vbl;
			long t = FRCLOCK;
			if (perf_vbl)
				log_line("PERF frames=100 vbls=%ld state=%ld", t - perf_vbl, gs.state, 0, 0);
			perf_vbl = t;
		}
	}

	log_line("EXIT frames=%ld score=%ld", frames, gs.score, 0, 0);
	sound_cleanup();
	input_exit();
	gfx_exit();
	Super((void *)old_ssp);
	return 0;
}
