/*
 * ROCK BLASTER - Atari STE port.
 *
 * Port of the Amiga version (main.c in geekychris/amiga_games, kept as
 * main.c.amiga): game.c and draw.c are unmodified and run on the shared
 * ST layer in ../st_port.  This file replaces the AmigaOS parts of
 * main.c (screens, IDCMP, bridge); the palette and the procedural sound
 * effects below are copied from it verbatim and play through the C
 * ptplayer on the Paula emulation (STE DMA sound; silent on a plain ST).
 *
 * The Amiga game loads axelf.mod for music, but that file is not a valid
 * ProTracker module (no M.K. tag), so the original rejects it and plays
 * sound effects only; so does this port.
 *
 * Layers: title page as scenery, score/lives/game over as HUD, rocks,
 * ship, bullets and particles as sprites redrawn every frame.
 *
 * Controls: cursor keys / A,Z,D,C / joystick to rotate and thrust (up,
 * W), Space / Alt / fire to shoot, Esc quits.  With Hatari --natfeats
 * on, events and symbol addresses are logged ("ROCK ..." lines).
 */
#include <osbind.h>
#include <stdio.h>
#include <string.h>
#include <exec/memory.h>
#include "game.h"
#include "draw.h"
#include "input.h"
#include "ptplayer.h"
#include "natfeats.h"

#define FRCLOCK (*(volatile long *)0x466)
#define CUSTOM_BASE ((void *)0xdff000)	/* ignored by the C ptplayer */

void input_init(void);
void input_exit(void);

/* Game state */
static GameState gs;

/* Amiga palette: 16 colors for 4 bitplanes */
static UWORD palette[16] = {
    0x001,  /*  0: dark blue (background) */
    0xFFF,  /*  1: white (ship, text) */
    0x08F,  /*  2: blue */
    0x0F8,  /*  3: cyan */
    0x0F0,  /*  4: green */
    0x888,  /*  5: dim grey (small rocks) */
    0xAAA,  /*  6: medium grey (medium rocks) */
    0xCCC,  /*  7: light grey (large rocks) */
    0xF00,  /*  8: red */
    0xF80,  /*  9: orange (flame, explosions) */
    0xFF0,  /* 10: yellow */
    0xF0F,  /* 11: magenta */
    0x80F,  /* 12: purple */
    0x444,  /* 13: dark grey */
    0xFA0,  /* 14: bright orange */
    0xFE0,  /* 15: bright yellow (explosion) */
};

/* Sound effect samples in chip RAM */
#define SFX_SHOOT_LEN    128
#define SFX_EXPLODE_LEN  512
#define SFX_EXPLODE_SM_LEN 256
#define SFX_THRUST_LEN   64
#define SFX_DIE_LEN      1024

static BYTE *sfx_shoot_data = NULL;
static BYTE *sfx_explode_data = NULL;
static BYTE *sfx_explode_sm_data = NULL;
static BYTE *sfx_die_data = NULL;

/* SFX structures for ptplayer */
static SfxStructure sfx_shoot_sfx;
static SfxStructure sfx_explode_sfx;
static SfxStructure sfx_explode_sm_sfx;
static SfxStructure sfx_die_sfx;

/* --- Sound effect generation --- */

static ULONG sfx_rng_state = 98765;
static WORD sfx_rng(void)
{
    sfx_rng_state = sfx_rng_state * 1103515245UL + 12345UL;
    return (WORD)((sfx_rng_state >> 16) & 0x7FFF);
}

static void build_sfx(void)
{
    WORD i;

    /* Shoot: short sharp blip, descending pitch */
    sfx_shoot_data = (BYTE *)AllocMem(SFX_SHOOT_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_shoot_data) {
        for (i = 0; i < SFX_SHOOT_LEN; i++) {
            WORD t = (i * 127) / SFX_SHOOT_LEN;
            WORD env = 127 - t;
            sfx_shoot_data[i] = (BYTE)((((i * 20) & 0xFF) > 128 ? 64 : -64) * env / 127);
        }
    }

    /* Explode large: white noise with decay */
    sfx_explode_data = (BYTE *)AllocMem(SFX_EXPLODE_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_explode_data) {
        for (i = 0; i < SFX_EXPLODE_LEN; i++) {
            WORD env = 127 - (i * 127) / SFX_EXPLODE_LEN;
            sfx_explode_data[i] = (BYTE)((sfx_rng() % 256 - 128) * env / 127);
        }
    }

    /* Explode small: shorter noise burst */
    sfx_explode_sm_data = (BYTE *)AllocMem(SFX_EXPLODE_SM_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_explode_sm_data) {
        for (i = 0; i < SFX_EXPLODE_SM_LEN; i++) {
            WORD env = 127 - (i * 127) / SFX_EXPLODE_SM_LEN;
            sfx_explode_sm_data[i] = (BYTE)((sfx_rng() % 256 - 128) * env / 127);
        }
    }

    /* Die: descending tone + noise */
    sfx_die_data = (BYTE *)AllocMem(SFX_DIE_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_die_data) {
        for (i = 0; i < SFX_DIE_LEN; i++) {
            WORD env = 127 - (i * 127) / SFX_DIE_LEN;
            WORD freq = 8 + (i * 40) / SFX_DIE_LEN;
            WORD tone = ((i * freq) & 0xFF) > 128 ? 60 : -60;
            WORD noise = (sfx_rng() % 80) - 40;
            sfx_die_data[i] = (BYTE)(((tone + noise) * env) / 127);
        }
    }

    /* Setup SFX structures */
    sfx_shoot_sfx.sfx_ptr = sfx_shoot_data;
    sfx_shoot_sfx.sfx_len = SFX_SHOOT_LEN / 2;
    sfx_shoot_sfx.sfx_per = 200;
    sfx_shoot_sfx.sfx_vol = 50;
    sfx_shoot_sfx.sfx_cha = -1;
    sfx_shoot_sfx.sfx_pri = 30;

    sfx_explode_sfx.sfx_ptr = sfx_explode_data;
    sfx_explode_sfx.sfx_len = SFX_EXPLODE_LEN / 2;
    sfx_explode_sfx.sfx_per = 300;
    sfx_explode_sfx.sfx_vol = 64;
    sfx_explode_sfx.sfx_cha = -1;
    sfx_explode_sfx.sfx_pri = 50;

    sfx_explode_sm_sfx.sfx_ptr = sfx_explode_sm_data;
    sfx_explode_sm_sfx.sfx_len = SFX_EXPLODE_SM_LEN / 2;
    sfx_explode_sm_sfx.sfx_per = 250;
    sfx_explode_sm_sfx.sfx_vol = 48;
    sfx_explode_sm_sfx.sfx_cha = -1;
    sfx_explode_sm_sfx.sfx_pri = 40;

    sfx_die_sfx.sfx_ptr = sfx_die_data;
    sfx_die_sfx.sfx_len = SFX_DIE_LEN / 2;
    sfx_die_sfx.sfx_per = 350;
    sfx_die_sfx.sfx_vol = 64;
    sfx_die_sfx.sfx_cha = -1;
    sfx_die_sfx.sfx_pri = 60;
}

/* SFX callback functions (called from game.c) */
void sfx_shoot(void)
{
    if (sfx_shoot_data)
        mt_playfx(CUSTOM_BASE, &sfx_shoot_sfx);
}

void sfx_explode_large(void)
{
    if (sfx_explode_data)
        mt_playfx(CUSTOM_BASE, &sfx_explode_sfx);
}

void sfx_explode_small(void)
{
    if (sfx_explode_sm_data)
        mt_playfx(CUSTOM_BASE, &sfx_explode_sm_sfx);
}

void sfx_thrust_tick(void) { /* not used as continuous SFX */ }

void sfx_die(void)
{
    if (sfx_die_data)
        mt_playfx(CUSTOM_BASE, &sfx_die_sfx);
}


static void log_line(const char *fmt, long a, long b, long c, long d)
{
	char buf[128];
	strcpy(buf, "ROCK ");
	snprintf(buf + 5, sizeof(buf) - 6, fmt, a, b, c, d);
	strcat(buf, "\n");
	nf_print(buf);
}

static const char *state_names[] = { "TITLE", "PLAYING", "DEAD", "GAMEOVER" };

static ULONG last_hud_key = 1;

static void draw_frame(void)
{
	struct RastPort *rp;
	ULONG key = (ULONG)gs.score * 977 + gs.lives * 31 + gs.level * 7 +
	            (gs.state == STATE_GAMEOVER ? 0x40000000UL : 0);

	if (key == last_hud_key)
		gfx_hud_keep();
	else
	{
		struct RastPort *hud = gfx_hud();
		last_hud_key = key;
		draw_hud(hud, &gs);
		if (gs.state == STATE_GAMEOVER)
			draw_gameover(hud, gs.score);
		gfx_hud_commit();
	}

	gfx_restore_back();
	rp = gfx_back();
	draw_rocks(rp, gs.rocks);
	draw_bullets(rp, gs.bullets);
	draw_particles(rp, gs.particles);
	draw_ship(rp, &gs.ship, gs.frame);
}

static void show_title(void)
{
	gfx_bg_clear();
	draw_title(gfx_bg());
	gfx_bg_to_screens();
	last_hud_key = 1;
}

static void show_playfield(void)
{
	gfx_bg_clear();
	gfx_bg_to_screens();
	last_hud_key = 1;
}

int main(void)
{
	long old_ssp, last_vbl, frames = 0;
	int running = 1, last_state = -1;
	long last_score = -1, last_level = -1;
	UWORD inp = 0;

	nf_init();
	old_ssp = Super(0L);

	if (!gfx_init(palette, 16))
	{
		Super((void *)old_ssp);
		Cconws("Not enough memory\r\n");
		return 1;
	}
	gfx_set_frame_vbls(1);
	gfx_or_mode(1);		/* vector objects over the black playfield */
	game_init_tables();
	build_sfx();
	mt_install_cia(CUSTOM_BASE, NULL, 1);	/* sound effects (STE) */
	mt_MusicChannels = 2;			/* as with music on the Amiga */
	input_init();

	game_init(&gs);
	gs.state = STATE_TITLE;
	show_title();

	log_line("START sound=%s", (long)(mt_sound_ok() == 0 ? "dma" : "none"), 0, 0, 0);
	log_line("SYMBOL gs 0x%06lx", (long)&gs, 0, 0, 0);
	log_line("SYMBOL ship 0x%06lx", (long)&gs.ship, 0, 0, 0);
	log_line("SYMBOL rocks 0x%06lx", (long)&gs.rocks, 0, 0, 0);
	log_line("SYMBOL state 0x%06lx", (long)&gs.state, 0, 0, 0);
	log_line("SYMBOL score 0x%06lx", (long)&gs.score, 0, 0, 0);
	log_line("SYMBOL tune 0x%06lx", (long)&g_tune, 0, 0, 0);
	log_line("SYMBOL frame_sync 0x%06lx", (long)gfx_swap, 0, 0, 0);

	last_vbl = FRCLOCK;
	while (running)
	{
		long now = FRCLOCK;
		int steps = (int)(now - last_vbl);
		last_vbl = now;
		if (steps < 1) steps = 1;
		if (steps > 3) steps = 3;

		inp = input_read();
		if (inp & INPUT_ESC)
			break;

		if (gs.state == STATE_TITLE)
		{
			if (inp & INPUT_FIRE)
			{
				game_init(&gs);
				show_playfield();
			}
		}
		else
		{
			/* one game update per 50 Hz VBL, as on the Amiga */
			while (steps-- > 0)
				game_update(&gs, (inp & INPUT_LEFT) ? 1 : 0,
				            (inp & INPUT_RIGHT) ? 1 : 0,
				            (inp & INPUT_UP) ? 1 : 0,
				            (inp & INPUT_FIRE) ? 1 : 0);
		}

		if (gs.state != last_state)
		{
			log_line("STATE %s level=%ld score=%ld lives=%ld", (long)state_names[gs.state],
			         gs.level, gs.score, gs.lives);
			last_state = gs.state;
		}
		if (gs.score != last_score || gs.level != last_level)
		{
			if (gs.state != STATE_TITLE)
				log_line("SCORE %ld level=%ld rocks=%ld", gs.score, gs.level, gs.rock_count, 0);
			last_score = gs.score;
			last_level = gs.level;
		}

		if (gs.state != STATE_TITLE)
			draw_frame();
		else
		{
			gfx_hud_keep();
			gfx_restore_back();
		}
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
	mt_remove_cia(CUSTOM_BASE);
	input_exit();
	gfx_exit();
	Super((void *)old_ssp);
	return 0;
}
