/*
 * ORBITAL PATROL - Atari STE port.
 *
 * Port of the Amiga version (main.c in geekychris/amiga_games, kept as
 * main.c.amiga): game.c, draw.c, sound.c and score.c are unmodified and
 * run on the shared ST layer in ../st_port.  sound.c drives the C
 * ptplayer (ProTracker replay + sound effects) on the Paula emulation,
 * so the original MOD music plays on the STE's DMA sound; score.c reads
 * and writes its table through the AmigaDOS shim (ORBITAL_.SCO).
 *
 * This file replaces the AmigaOS parts of main.c (screen, IDCMP, input
 * device, bridge).  Its state machine is copied, split in two: the
 * logic runs once per 50 Hz VBL (the Amiga ran one update per frame at
 * 50 Hz) and the drawing once per displayed frame.  The game redraws the
 * whole screen every frame (draw_all clears first), so drawing goes
 * straight into the back buffer.
 *
 * Controls (as on the Amiga): cursor keys / WASD / joystick to fly,
 * Space / Alt / Shift / fire to shoot, Z smart bomb, H or X hyperspace,
 * Return / Space start, Esc quits.  With Hatari --natfeats on, events and
 * symbol addresses are logged ("OPATROL ..." lines).
 */
#include <osbind.h>
#include <stdio.h>
#include <string.h>
#include "game.h"
#include "draw.h"
#include "input.h"
#include "sound.h"
#include "score.h"
#include "natfeats.h"
#include "paula.h"

#define FRCLOCK (*(volatile long *)0x466)

static GameState gs;
static InputData input_data;
static ScoreTable score_table;
static char entry_name[HISCORE_NAMELEN];
static WORD entry_cursor = 0;
static WORD entry_rank = -1;
static WORD startup_delay = 20;

static UWORD palette[16] = {
    0x000,  /*  0: Black (background) */
    0xFFF,  /*  1: White (text, lasers, humans) */
    0x336,  /*  2: Dim star / scanner border */
    0x220,  /*  3: Dark terrain / scanner bg */
    0x460,  /*  4: Terrain green */
    0x5A0,  /*  5: Terrain highlight */
    0xEE0,  /*  6: Ship body yellow / explosion yellow */
    0xF80,  /*  7: Thrust / drone / explosion orange */
    0x0F0,  /*  8: Diver green */
    0xF0F,  /*  9: Stalker magenta / hive */
    0xF00,  /* 10: Dropper red / explosion red / mine */
    0x0CF,  /* 11: HUD cyan / chaser */
    0x000,  /* 12: unused */
    0x000,  /* 13: unused */
    0x000,  /* 14: unused */
    0x000,  /* 15: unused */
};

static void log_line(const char *fmt, long a, long b, long c, long d)
{
	char buf[128];
	strcpy(buf, "OPATROL ");
	snprintf(buf + 8, sizeof(buf) - 9, fmt, a, b, c, d);
	strcat(buf, "\n");
	nf_print(buf);
}

static const char *state_name(int s)
{
	static const char *n[] = { "TITLE", "PLAYING", "DYING", "RESPAWNING",
		"LEVEL_START", "LEVEL_CLEAR", "GAMEOVER", "HISCORE_ENTRY", "HISCORE_VIEW" };
	return s >= 0 && s < 9 ? n[s] : "?";
}

/* one 50 Hz step of main.c's state machine, without the drawing */
static void game_tick(WORD input)
{
	switch (gs.state) {
	case STATE_TITLE:
		if (gs.frame > 30 && (input & (INPUT_START | INPUT_FIRE))) {
			game_init(&gs, 1);
			gs.hiscore = score_table.count > 0 ? score_table.entries[0].score : 0;
			gs.state = STATE_LEVEL_START;
			gs.state_timer = 45;
			game_spawn_wave(&gs);
		}
		break;

	case STATE_LEVEL_START:
		game_update(&gs, 0); /* no input during level start */
		gs.state_timer--;
		if (gs.state_timer <= 0)
			gs.state = STATE_PLAYING;
		break;

	case STATE_PLAYING:
		game_update(&gs, input);
		break;

	case STATE_DYING:
		game_update(&gs, 0);
		gs.state_timer--;
		if (gs.state_timer <= 0) {
			if (gs.lives > 0) {
				gs.state = STATE_RESPAWNING;
				gs.state_timer = 60;
				gs.ship.alive = 1;
				gs.ship.wy = TO_FP(PLAY_TOP + (PLAY_BOT - PLAY_TOP) / 2);
				gs.ship.vx = 0;
				gs.ship.vy = 0;
				gs.ship.invuln_timer = 120;
			} else {
				gs.state = STATE_GAMEOVER;
				gs.state_timer = 180;
			}
		}
		break;

	case STATE_RESPAWNING:
		game_update(&gs, input);
		gs.state_timer--;
		if (gs.state_timer <= 0)
			gs.state = STATE_PLAYING;
		break;

	case STATE_LEVEL_CLEAR:
		gs.state_timer--;
		if (gs.state_timer <= 0) {
			gs.score += gs.humans_alive * SCORE_WAVE_BONUS;
			if (gs.score > gs.hiscore)
				gs.hiscore = gs.score;
			gs.level++;
			gs.state = STATE_LEVEL_START;
			gs.state_timer = 45;
			game_spawn_wave(&gs);
		}
		break;

	case STATE_GAMEOVER:
		gs.state_timer--;
		if (gs.state_timer <= 0 || (input & (INPUT_START | INPUT_FIRE))) {
			entry_rank = score_qualifies(&score_table, gs.score);
			if (entry_rank >= 0) {
				gs.state = STATE_HISCORE_ENTRY;
				gs.state_timer = 0;
				entry_cursor = 0;
				memset(entry_name, 0, HISCORE_NAMELEN);
				entry_name[0] = 'A';
			} else {
				gs.state = STATE_HISCORE_VIEW;
				gs.state_timer = 300;
			}
		}
		break;

	case STATE_HISCORE_ENTRY:
		/* Simple letter picker: left/right to change letter, fire to confirm */
		if (input & INPUT_UP) {
			if (entry_name[entry_cursor] < 'Z')
				entry_name[entry_cursor]++;
			else
				entry_name[entry_cursor] = 'A';
		}
		if (input & INPUT_DOWN) {
			if (entry_name[entry_cursor] > 'A')
				entry_name[entry_cursor]--;
			else
				entry_name[entry_cursor] = 'Z';
		}
		if (input & INPUT_RIGHT) {
			if (entry_cursor < HISCORE_NAMELEN - 2) {
				entry_cursor++;
				if (entry_name[entry_cursor] == 0)
					entry_name[entry_cursor] = 'A';
			}
		}
		if (input & INPUT_LEFT) {
			if (entry_cursor > 0) entry_cursor--;
		}
		if ((input & INPUT_FIRE) && gs.state_timer == 0) {
			gs.state_timer = 10; /* debounce */
		}
		if (gs.state_timer > 0) {
			gs.state_timer--;
			if (gs.state_timer == 0 && !(input & INPUT_FIRE)) {
				/* Confirm on fire release after debounce */
				score_insert(&score_table, entry_rank, entry_name, gs.score);
				score_save(&score_table);
				gs.state = STATE_HISCORE_VIEW;
				gs.state_timer = 300;
			}
		}
		break;

	case STATE_HISCORE_VIEW:
		gs.state_timer--;
		if (gs.state_timer <= 0 || (input & (INPUT_START | INPUT_FIRE))) {
			gs.state = STATE_TITLE;
			gs.frame = 0;
			startup_delay = 20;
		}
		break;
	}

	/* Sound events */
	sound_update(&gs);

	/* Frame counter */
	gs.frame++;
}

/* main.c's drawing for the current state */
static void game_draw(struct RastPort *rp)
{
	switch (gs.state) {
	case STATE_TITLE:
		draw_title(rp, &gs);
		break;
	case STATE_LEVEL_START:
		draw_all(rp, &gs);
		draw_level_message(rp, gs.level, "GET READY");
		break;
	case STATE_PLAYING:
	case STATE_DYING:
	case STATE_RESPAWNING:
		draw_all(rp, &gs);
		break;
	case STATE_LEVEL_CLEAR:
		draw_all(rp, &gs);
		{
			WORD bonus = gs.humans_alive * SCORE_WAVE_BONUS;
			char buf[40];
			sprintf(buf, "BONUS %ld", (long)bonus);
			draw_level_message(rp, gs.level, buf);
		}
		break;
	case STATE_GAMEOVER:
		draw_all(rp, &gs);
		draw_gameover(rp, &gs);
		break;
	case STATE_HISCORE_ENTRY:
		draw_all(rp, &gs);
		draw_hiscore_entry(rp, &gs, entry_name, entry_cursor);
		break;
	case STATE_HISCORE_VIEW:
		SetRast(rp, COL_BLACK);
		draw_hiscore_table(rp, &score_table);
		break;
	}
}

int main(void)
{
	long old_ssp, last_vbl, frames = 0;
	int running = 1, last_state = -1;
	long last_score = -1, last_level = -1;
	WORD input;

	nf_init();
	old_ssp = Super(0L);

	if (!gfx_init(palette, 16))
	{
		Super((void *)old_ssp);
		Cconws("Not enough memory\r\n");
		return 1;
	}
	gfx_set_frame_vbls(1);

	/* Sound: four channels of MOD music are mixed at 6258 Hz; 12517 Hz
	 * would take a third of the 8 MHz 68000 */
	paula_set_rate(6258);
	sound_init();
	if (sound_load_mod("DH2:Dev/orbital_patrol.mod")) {
		log_line("MOD loaded", 0, 0, 0, 0);
		sound_start_music();
	} else
		log_line("No MOD file found - continuing without music", 0, 0, 0, 0);

	/* High scores */
	score_init(&score_table);
	score_load(&score_table);

	input_init(&input_data);

	game_init(&gs, 1);
	gs.state = STATE_TITLE;
	gs.hiscore = score_table.count > 0 ? score_table.entries[0].score : 0;

	log_line("START", 0, 0, 0, 0);
	log_line("SYMBOL gs 0x%06lx", (long)&gs, 0, 0, 0);
	log_line("SYMBOL ship 0x%06lx", (long)&gs.ship, 0, 0, 0);
	log_line("SYMBOL state 0x%06lx", (long)&gs.state, 0, 0, 0);
	log_line("SYMBOL score 0x%06lx", (long)&gs.score, 0, 0, 0);
	log_line("SYMBOL lives 0x%06lx", (long)&gs.lives, 0, 0, 0);
	log_line("SYMBOL enemies 0x%06lx", (long)&gs.enemies, 0, 0, 0);
	log_line("SYMBOL humans 0x%06lx", (long)&gs.humans, 0, 0, 0);
	log_line("SYMBOL frame_sync 0x%06lx", (long)gfx_swap, 0, 0, 0);

	last_vbl = FRCLOCK;
	while (running)
	{
		long now = FRCLOCK;
		int steps = (int)(now - last_vbl);
		last_vbl = now;
		/* frames take several VBLs on an 8 MHz STE: catch up to 6 game
		 * steps so the game runs at the Amiga's speed */
		if (steps < 1) steps = 1;
		if (steps > 6) steps = 6;

		/* one state machine step per 50 Hz VBL, as on the Amiga */
		while (steps-- > 0)
		{
			input = input_read(&input_data);
			if (input & INPUT_QUIT)
				running = 0;
			if (startup_delay > 0) {
				startup_delay--;
				input = 0;
			}
			game_tick(input);
		}

		if (gs.state != last_state)
		{
			log_line("STATE %s level=%ld score=%ld lives=%ld", (long)state_name(gs.state),
			         gs.level, gs.score, gs.lives);
			last_state = gs.state;
		}
		if (gs.score != last_score || gs.level != last_level)
		{
			if (gs.state != STATE_TITLE)
				log_line("SCORE %ld level=%ld humans=%ld enemies=%ld", gs.score, gs.level,
				         gs.humans_alive, gs.enemies_alive);
			last_score = gs.score;
			last_level = gs.level;
		}

		game_draw(gfx_back());
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
	input_cleanup();
	sound_stop_music();
	sound_cleanup();
	gfx_exit();
	Super((void *)old_ssp);
	return 0;
}
