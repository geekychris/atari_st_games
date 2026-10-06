// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * PEA SHOOTER BLAST - Atari STE port.
 *
 * Port of the Amiga version (main.c in geekychris/amiga_games, kept as
 * main.c.amiga): game.c and levels.c are unmodified, draw.c has ST port
 * blocks (the level pre-rendered into a strip that the STE blitter
 * scrolls, cached strings).  The sound effects are the original's,
 * played through the C ptplayer on the Paula emulation (STE DMA sound).
 * The original's MOD music is not included; a ProTracker module put next
 * to the program as MUSIC.MOD is played as on the Amiga.
 *
 * The main loop is main.c's, with the logic run once per 50 Hz VBL (as
 * on the Amiga) and the drawing once per frame.
 *
 * Controls: A/D or cursor keys or joystick move, W / up jump, Space / Alt
 * / fire shoot, Esc quits.  With Hatari --natfeats on, events and symbol
 * addresses are logged ("PEASHOOT ..." lines).
 */
#include <osbind.h>
#include <stdio.h>
#include <string.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include "bridge_client.h"
#include "game.h"
#include "draw.h"
#include "input.h"
#include "ptplayer.h"
#include "natfeats.h"
#include "paula.h"

#define FRCLOCK (*(volatile long *)0x466)

/* Custom chip base for ptplayer (the Paula emulation) */
#define CUSTOM_BASE ((void *)&custom)

/* MOD data */
static UBYTE *mod_data = NULL;
static ULONG  mod_size = 0;

/* SFX samples */
#define SFX_SHOOT_LEN     128
#define SFX_HIT_LEN       96
#define SFX_EXPLODE_LEN   512
#define SFX_POWERUP_LEN   256
#define SFX_JUMP_LEN      128
#define SFX_PLAYER_HIT_LEN 256

static BYTE *sfx_shoot_data = NULL;
static BYTE *sfx_hit_data = NULL;
static BYTE *sfx_explode_data = NULL;
static BYTE *sfx_powerup_data = NULL;
static BYTE *sfx_jump_data = NULL;
static BYTE *sfx_player_hit_data = NULL;

static SfxStructure sfx_shoot_sfx;
static SfxStructure sfx_hit_sfx;
static SfxStructure sfx_explode_sfx;
static SfxStructure sfx_powerup_sfx;
static SfxStructure sfx_jump_sfx;
static SfxStructure sfx_player_hit_sfx;

/* Game state */
static GameState gs;

/* Color palette: 16 colors, underground/mechanical theme */
static UWORD palette[16] = {
    0x112,  /*  0: very dark blue (BG) */
    0xFFF,  /*  1: white */
    0x0A0,  /*  2: green (tank body) */
    0x0E0,  /*  3: bright green (tank turret, pipes) */
    0x0F0,  /*  4: lime green (powerup health, HUD bar) */
    0x666,  /*  5: grey (rock) */
    0x842,  /*  6: brown (ground/platform) */
    0xAAA,  /*  7: light grey (metal) */
    0xF00,  /*  8: red (enemies, damage) */
    0xF80,  /*  9: orange (brick, enemy alt) */
    0xFF0,  /* 10: yellow (bullets, powerup weapon) */
    0xF0F,  /* 11: magenta (boss) */
    0x531,  /* 12: dark brown (dirt) */
    0x444,  /* 13: dark grey (treads) */
    0xFA0,  /* 14: yellow-orange (ladder) */
    0x8FF,  /* 15: bright cyan (gate) */
};

/* --- SFX generation --- */

static ULONG sfx_rng_state = 98765;
static WORD sfx_rng(void)
{
    sfx_rng_state = sfx_rng_state * 1103515245UL + 12345UL;
    return (WORD)((sfx_rng_state >> 16) & 0x7FFF);
}

static void build_sfx(void)
{
    WORD i;

    /* Shoot: quick blip */
    sfx_shoot_data = (BYTE *)AllocMem(SFX_SHOOT_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_shoot_data) {
        for (i = 0; i < SFX_SHOOT_LEN; i++) {
            WORD t = (i * 127) / SFX_SHOOT_LEN;
            WORD env = 127 - t;
            sfx_shoot_data[i] = (BYTE)((((i * 25) & 0xFF) > 128 ? 64 : -64) * env / 127);
        }
    }

    /* Hit: short metallic ping */
    sfx_hit_data = (BYTE *)AllocMem(SFX_HIT_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_hit_data) {
        for (i = 0; i < SFX_HIT_LEN; i++) {
            WORD env = 127 - (i * 127) / SFX_HIT_LEN;
            WORD wave = ((i * 35) & 0xFF) > 128 ? 50 : -50;
            sfx_hit_data[i] = (BYTE)((wave + (sfx_rng() % 30 - 15)) * env / 127);
        }
    }

    /* Explode: noise with decay */
    sfx_explode_data = (BYTE *)AllocMem(SFX_EXPLODE_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_explode_data) {
        for (i = 0; i < SFX_EXPLODE_LEN; i++) {
            WORD env = 127 - (i * 127) / SFX_EXPLODE_LEN;
            sfx_explode_data[i] = (BYTE)((sfx_rng() % 256 - 128) * env / 127);
        }
    }

    /* Powerup: ascending tone */
    sfx_powerup_data = (BYTE *)AllocMem(SFX_POWERUP_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_powerup_data) {
        for (i = 0; i < SFX_POWERUP_LEN; i++) {
            WORD freq = 15 + (i * 30) / SFX_POWERUP_LEN;
            WORD env = 100 - (i * 60) / SFX_POWERUP_LEN;
            if (env < 20) env = 20;
            sfx_powerup_data[i] = (BYTE)((((i * freq) & 0xFF) > 128 ? 64 : -64) * env / 127);
        }
    }

    /* Jump: quick rising tone */
    sfx_jump_data = (BYTE *)AllocMem(SFX_JUMP_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_jump_data) {
        for (i = 0; i < SFX_JUMP_LEN; i++) {
            WORD freq = 30 - (i * 20) / SFX_JUMP_LEN;
            WORD env = 80 - (i * 80) / SFX_JUMP_LEN;
            if (env < 0) env = 0;
            sfx_jump_data[i] = (BYTE)((((i * freq) & 0xFF) > 128 ? 50 : -50) * env / 127);
        }
    }

    /* Player hit: descending buzz */
    sfx_player_hit_data = (BYTE *)AllocMem(SFX_PLAYER_HIT_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_player_hit_data) {
        for (i = 0; i < SFX_PLAYER_HIT_LEN; i++) {
            WORD freq = 10 + (i * 25) / SFX_PLAYER_HIT_LEN;
            WORD env = 127 - (i * 127) / SFX_PLAYER_HIT_LEN;
            WORD tone = ((i * freq) & 0xFF) > 128 ? 50 : -50;
            WORD noise = (sfx_rng() % 60) - 30;
            sfx_player_hit_data[i] = (BYTE)(((tone + noise) * env) / 127);
        }
    }

    /* Setup structures */
    sfx_shoot_sfx.sfx_ptr = sfx_shoot_data;
    sfx_shoot_sfx.sfx_len = SFX_SHOOT_LEN / 2;
    sfx_shoot_sfx.sfx_per = 180;
    sfx_shoot_sfx.sfx_vol = 50;
    sfx_shoot_sfx.sfx_cha = -1;
    sfx_shoot_sfx.sfx_pri = 30;

    sfx_hit_sfx.sfx_ptr = sfx_hit_data;
    sfx_hit_sfx.sfx_len = SFX_HIT_LEN / 2;
    sfx_hit_sfx.sfx_per = 200;
    sfx_hit_sfx.sfx_vol = 40;
    sfx_hit_sfx.sfx_cha = -1;
    sfx_hit_sfx.sfx_pri = 25;

    sfx_explode_sfx.sfx_ptr = sfx_explode_data;
    sfx_explode_sfx.sfx_len = SFX_EXPLODE_LEN / 2;
    sfx_explode_sfx.sfx_per = 300;
    sfx_explode_sfx.sfx_vol = 64;
    sfx_explode_sfx.sfx_cha = -1;
    sfx_explode_sfx.sfx_pri = 50;

    sfx_powerup_sfx.sfx_ptr = sfx_powerup_data;
    sfx_powerup_sfx.sfx_len = SFX_POWERUP_LEN / 2;
    sfx_powerup_sfx.sfx_per = 250;
    sfx_powerup_sfx.sfx_vol = 50;
    sfx_powerup_sfx.sfx_cha = -1;
    sfx_powerup_sfx.sfx_pri = 40;

    sfx_jump_sfx.sfx_ptr = sfx_jump_data;
    sfx_jump_sfx.sfx_len = SFX_JUMP_LEN / 2;
    sfx_jump_sfx.sfx_per = 200;
    sfx_jump_sfx.sfx_vol = 35;
    sfx_jump_sfx.sfx_cha = -1;
    sfx_jump_sfx.sfx_pri = 20;

    sfx_player_hit_sfx.sfx_ptr = sfx_player_hit_data;
    sfx_player_hit_sfx.sfx_len = SFX_PLAYER_HIT_LEN / 2;
    sfx_player_hit_sfx.sfx_per = 300;
    sfx_player_hit_sfx.sfx_vol = 60;
    sfx_player_hit_sfx.sfx_cha = -1;
    sfx_player_hit_sfx.sfx_pri = 55;
}

/* SFX callbacks */
void sfx_shoot(void)
{
    if (sfx_shoot_data) mt_playfx(CUSTOM_BASE, &sfx_shoot_sfx);
}
void sfx_hit(void)
{
    if (sfx_hit_data) mt_playfx(CUSTOM_BASE, &sfx_hit_sfx);
}
void sfx_explode(void)
{
    if (sfx_explode_data) mt_playfx(CUSTOM_BASE, &sfx_explode_sfx);
}
void sfx_powerup(void)
{
    if (sfx_powerup_data) mt_playfx(CUSTOM_BASE, &sfx_powerup_sfx);
}
void sfx_jump(void)
{
    if (sfx_jump_data) mt_playfx(CUSTOM_BASE, &sfx_jump_sfx);
}
void sfx_player_hit(void)
{
    if (sfx_player_hit_data) mt_playfx(CUSTOM_BASE, &sfx_player_hit_sfx);
}

/* --- MOD loading --- */

static UBYTE *load_file_to_chip(const char *path, ULONG *out_size)
{
    BPTR fh;
    UBYTE *buf = NULL;
    LONG len;
    fh = Open((CONST_STRPTR)path, MODE_OLDFILE);
    if (!fh) return NULL;

    Seek(fh, 0, OFFSET_END);
    len = Seek(fh, 0, OFFSET_BEGINNING);
    if (len <= 0) { Close(fh); return NULL; }

    buf = (UBYTE *)AllocMem(len, MEMF_CHIP);
    if (!buf) { Close(fh); return NULL; }

    if (Read(fh, buf, len) != len) {
        FreeMem(buf, len);
        Close(fh);
        return NULL;
    }

    Close(fh);
    *out_size = (ULONG)len;
    return buf;
}





/* Level used by tile drawing (global for draw_tile_column) */
WORD g_current_level = 0;

void input_init(void);
void input_exit(void);

static void log_line(const char *fmt, long a, long b, long c, long d)
{
    char buf[128];
    strcpy(buf, "PEASHOOT ");
    snprintf(buf + 9, sizeof(buf) - 10, fmt, a, b, c, d);
    strcat(buf, "\n");
    nf_print(buf);
}

static const char *state_name(int s)
{
    static const char *n[] = { "TITLE", "PLAYING", "DEAD", "GAMEOVER", "LEVELCLEAR" };
    return s >= 0 && s < 5 ? n[s] : "?";
}

/* one 50 Hz step of main.c's loop, without the drawing */
static int game_step(void)
{
    UWORD inp = input_read();

    if (inp & INPUT_ESC)
        return 0;
    if (gs.state == STATE_TITLE) {
        gs.frame++;
        if (inp & INPUT_FIRE) {
            game_init(&gs);
            draw_tiles_invalidate();
        }
    } else {
        WORD level = gs.level;
        game_update(&gs, (inp & INPUT_LEFT) ? 1 : 0,
                         (inp & INPUT_RIGHT) ? 1 : 0,
                         (inp & INPUT_JUMP) ? 1 : 0,
                         (inp & INPUT_DOWN) ? 1 : 0,
                         (inp & INPUT_FIRE) ? 1 : 0);
        if (gs.level != level)
            draw_tiles_invalidate();
    }
    return 1;
}

/* main.c's drawing: the title on the HUD layer; in the game, a full
 * redraw of the back buffer (the blitter copies the tiles) */
static void game_draw(void)
{
    static int mode = -1;
    int m = gs.state == STATE_TITLE;

    if (m != mode) {
        gfx_bg_clear();
        gfx_bg_to_screens();
        mode = m;
    }
    if (m) {
        draw_title(gfx_hud(), gs.frame);
        gfx_hud_commit();
        gfx_restore_back();
        return;
    }
    gfx_hud_commit();       /* (empty: drops the title's operations) */
    {
        struct RastPort *rp = gfx_back_nomark();

        /* Set global level for tile drawing */
        g_current_level = gs.level;

        draw_all_tiles(rp, &gs.scroll);

        /* Draw entities */
        draw_powerups(rp, gs.powerups, &gs.scroll);
        draw_enemies(rp, gs.enemies, &gs.scroll);
        draw_enemy_bullets(rp, gs.enemy_bullets, &gs.scroll);
        draw_bullets(rp, gs.bullets, &gs.scroll);
        draw_tank(rp, &gs.tank, &gs.scroll);
        draw_particles(rp, gs.particles, &gs.scroll);
        draw_explosions(rp, gs.explosions, &gs.scroll);
        draw_hud_composite(rp, &gs);

        if (gs.state == STATE_GAMEOVER) {
            draw_gameover(rp, gs.score);
        }
        else if (gs.state == STATE_LEVELCLEAR) {
            draw_levelclear(rp, gs.level);
        }
    }
}

int main(void)
{
    long old_ssp, last_vbl, frames = 0;
    int running = 1, last_state = -1, last_level = -1;
    long last_score = -1;
    WORD music_playing = 0;

    nf_init();
    old_ssp = Super(0L);

    if (!gfx_init(palette, 16)) {
        Super((void *)old_ssp);
        Cconws("Not enough memory\r\n");
        return 1;
    }
    gfx_set_frame_vbls(1);

    /* Build SFX */
    build_sfx();

    /* ptplayer on the Paula emulation, for the effects (and the music) */
    paula_set_rate(6258);
    mt_install_cia(CUSTOM_BASE, NULL, 1);
    if (mt_sound_ok() != 0)
        log_line("no DMA sound - continuing without sound", 0, 0, 0, 0);

    /* Load MOD music. Before handing the buffer to ptplayer, validate
     * the 15/31-sample MOD header against mod_size so a truncated or
     * bogus file can't drive mt_init off the end of the buffer. */
    mod_data = load_file_to_chip("PROGDIR:music.mod", &mod_size);
    if (mod_data) {
        WORD mod_ok = 0;
        /* Minimum: 1084-byte header (20 name + 31*30 sample hdrs +
         * 1 songlen + 1 restart + 128 orders + 4 sig). */
        if (mod_size >= 1084) {
            ULONG total_samples = 0;
            WORD max_pattern = 0;
            WORD i;
            /* Highest pattern number in the order table gives us the
             * pattern-data size (each pattern is 1024 bytes: 64 rows
             * x 4 channels x 4 bytes). Scan all 128 order slots so
             * we cover unused-but-populated entries too. */
            for (i = 0; i < 128; i++) {
                UBYTE p = mod_data[952 + i];
                if (p > max_pattern) max_pattern = p;
            }
            /* Sum sample lengths (word count at offset +22 of each
             * 30-byte sample header, starting at offset 20). */
            for (i = 0; i < 31; i++) {
                ULONG off = 20 + (ULONG)i * 30 + 22;
                ULONG slen_w = ((ULONG)mod_data[off] << 8) | mod_data[off + 1];
                total_samples += slen_w * 2;
            }
            {
                ULONG needed = 1084UL +
                               ((ULONG)max_pattern + 1) * 1024UL +
                               total_samples;
                if (needed <= mod_size) {
                    AB_I("Loaded music.mod (%ld bytes)", (long)mod_size);
                    mt_init(CUSTOM_BASE, mod_data, NULL, 0);
                    mt_MusicChannels = 2;
                    mt_Enable = 1;
                    music_playing = 1;
                    mod_ok = 1;
                } else {
                    AB_W("MOD size %ld < required %ld - refusing to play",
                         (long)mod_size, (long)needed);
                }
            }
        } else {
            AB_W("MOD too small (%ld bytes) - refusing to play", (long)mod_size);
        }
        if (!mod_ok) {
            /* Match the existing cleanup path (see shutdown at end of
             * main()): free chip buffer and drop the pointer. */
            FreeMem(mod_data, mod_size);
            mod_data = NULL;
            mod_size = 0;
        }
    } else {
        AB_I("no music.mod - sound effects only");
    }

    /* Init game */
    game_init(&gs);
    gs.state = STATE_TITLE;
    input_init();

    log_line("START music=%ld", (long)music_playing, 0, 0, 0);
    log_line("SYMBOL gs 0x%06lx", (long)&gs, 0, 0, 0);
    log_line("SYMBOL tank 0x%06lx", (long)&gs.tank, 0, 0, 0);
    log_line("SYMBOL scroll 0x%06lx", (long)&gs.scroll, 0, 0, 0);
    log_line("SYMBOL state 0x%06lx", (long)&gs.state, 0, 0, 0);
    log_line("SYMBOL level 0x%06lx", (long)&gs.level, 0, 0, 0);
    log_line("SYMBOL frame_sync 0x%06lx", (long)gfx_swap, 0, 0, 0);

    last_vbl = FRCLOCK;
    while (running) {
        long now = FRCLOCK;
        int steps = (int)(now - last_vbl);
        last_vbl = now;
        if (steps < 1) steps = 1;
        if (steps > 4) steps = 4;

        while (steps-- > 0 && running)
            running = game_step();

        if (gs.state != last_state || gs.level != last_level) {
            log_line("STATE %s level=%ld score=%ld lives=%ld", (long)state_name(gs.state),
                     (long)gs.level + 1, (long)gs.score, (long)gs.lives);
            last_state = gs.state;
            last_level = gs.level;
        }
        if ((long)gs.score != last_score) {
            if (gs.state != STATE_TITLE)
                log_line("SCORE %ld lives=%ld health=%ld", (long)gs.score, (long)gs.lives,
                         (long)gs.tank.health, 0);
            last_score = gs.score;
        }

        game_draw();
        gfx_swap();
        frames++;
        if (frames % 100 == 0) {
            static long perf_vbl;
            long t = FRCLOCK;
            if (perf_vbl)
                log_line("PERF frames=100 vbls=%ld state=%ld", t - perf_vbl, gs.state, 0, 0);
            perf_vbl = t;
        }
    }

    log_line("EXIT frames=%ld score=%ld", frames, (long)gs.score, 0, 0);
    if (music_playing)
        mt_end(CUSTOM_BASE);
    mt_remove_cia(CUSTOM_BASE);
    input_exit();
    gfx_exit();
    Super((void *)old_ssp);
    return 0;
}
