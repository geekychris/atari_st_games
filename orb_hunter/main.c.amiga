// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * ORB HUNTER for Amiga
 *
 * exploration platformer with:
 * - Tile-based rooms with scrolling between areas
 * - Power-up collection (roll form, missiles, bombs, beams)
 * - Multiple enemy types and boss encounters
 * - MOD music via ptplayer
 * - Sound effects (shoot, missile, bomb, item, hit, jump)
 * - Joystick + keyboard input
 * - AmigaBridge integration for debug/tuning
 *
 * Controls:
 *   Joystick port 2 or keyboard arrows + space/alt
 *   Up = jump/aim up, Down = roll form
 *   Fire = shoot, Return = start/pause
 *   ESC to quit
 */

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/dos.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfxbase.h>
#include <graphics/view.h>
#include <exec/memory.h>
#include <hardware/custom.h>
#include <stdio.h>
#include <string.h>

#include "bridge_client.h"
#include "game.h"
#include "draw.h"
#include "input.h"
#include "ptplayer.h"

/* Libraries */
struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase       *GfxBase = NULL;

/* Screen & double buffering */
static struct Screen *screen = NULL;
static struct Window *window = NULL;
static struct ScreenBuffer *sbuf[2] = { NULL, NULL };
static struct RastPort rp_buf[2];    /* one RastPort per buffer */
static WORD cur_buf = 0;             /* which buffer we're drawing to */
static struct MsgPort *db_port[2] = { NULL, NULL };
static BOOL safe_to_write[2] = { TRUE, TRUE };

/* Custom chip base for ptplayer */
#define CUSTOM_BASE ((void *)0xdff000)

/* MOD data loaded into chip RAM */
static UBYTE *mod_data = NULL;
static ULONG  mod_size = 0;

/* Sound effect samples in chip RAM */
#define SFX_SHOOT_LEN       128
#define SFX_MISSILE_LEN     256
#define SFX_BOMB_LEN        512
#define SFX_ITEM_GET_LEN    768
#define SFX_PLAYER_HIT_LEN  256
#define SFX_ENEMY_HIT_LEN   128
#define SFX_DOOR_OPEN_LEN   256
#define SFX_JUMP_LEN        128

static BYTE *sfx_shoot_data = NULL;
static BYTE *sfx_missile_data = NULL;
static BYTE *sfx_bomb_data = NULL;
static BYTE *sfx_item_get_data = NULL;
static BYTE *sfx_player_hit_data = NULL;
static BYTE *sfx_enemy_hit_data = NULL;
static BYTE *sfx_door_open_data = NULL;
static BYTE *sfx_jump_data = NULL;

/* SFX structures for ptplayer */
static SfxStructure sfx_shoot_sfx;
static SfxStructure sfx_missile_sfx;
static SfxStructure sfx_bomb_sfx;
static SfxStructure sfx_item_get_sfx;
static SfxStructure sfx_player_hit_sfx;
static SfxStructure sfx_enemy_hit_sfx;
static SfxStructure sfx_door_open_sfx;
static SfxStructure sfx_jump_sfx;

/* Game state */
static GameState gs;

/* Amiga palette: 16 colors for 4 bitplanes (sci-fi-themed) */
static UWORD palette[16] = {
    0x001,  /*  0: near-black (BG) */
    0xFFF,  /*  1: white */
    0xE40,  /*  2: orange-red (hunter suit) */
    0xFC0,  /*  3: yellow (suit detail/visor) */
    0x141,  /*  4: dark green (cavern rock) */
    0x262,  /*  5: medium green */
    0x668,  /*  6: grey-blue (metal) */
    0x99A,  /*  7: light grey */
    0xF30,  /*  8: red-orange (lava/magma) */
    0xF80,  /*  9: bright orange */
    0xFF0,  /* 10: yellow (missiles) */
    0xA0F,  /* 11: purple (enemy) */
    0xF0A,  /* 12: magenta */
    0x06F,  /* 13: blue (doors) */
    0x0FF,  /* 14: cyan (items) */
    0x0F0,  /* 15: bright green (beam) */
};

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

    /* Shoot: quick high-pitched blip, square wave descending */
    sfx_shoot_data = (BYTE *)AllocMem(SFX_SHOOT_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_shoot_data) {
        for (i = 0; i < SFX_SHOOT_LEN; i++) {
            WORD t = (i * 127) / SFX_SHOOT_LEN;
            WORD env = 127 - t;
            WORD freq = 25 - (i * 10) / SFX_SHOOT_LEN;
            sfx_shoot_data[i] = (BYTE)((((i * freq) & 0xFF) > 128 ? 64 : -64) * env / 127);
        }
    }

    /* Missile: longer bass thump */
    sfx_missile_data = (BYTE *)AllocMem(SFX_MISSILE_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_missile_data) {
        for (i = 0; i < SFX_MISSILE_LEN; i++) {
            WORD env = 127 - (i * 127) / SFX_MISSILE_LEN;
            WORD freq = 4 + (i * 2) / SFX_MISSILE_LEN;
            WORD tone = ((i * freq) & 0xFF) > 128 ? 80 : -80;
            WORD noise = (sfx_rng() % 40) - 20;
            sfx_missile_data[i] = (BYTE)(((tone + noise) * env) / 127);
        }
    }

    /* Bomb: explosion noise with decay */
    sfx_bomb_data = (BYTE *)AllocMem(SFX_BOMB_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_bomb_data) {
        for (i = 0; i < SFX_BOMB_LEN; i++) {
            WORD env = 127 - (i * 127) / SFX_BOMB_LEN;
            WORD noise = (sfx_rng() % 256) - 128;
            WORD tone = ((i * 3) & 0xFF) > 128 ? 40 : -40;
            sfx_bomb_data[i] = (BYTE)(((noise + tone) * env) / 127);
        }
    }

    /* Item get: ascending arpeggio (3 quick tones) */
    sfx_item_get_data = (BYTE *)AllocMem(SFX_ITEM_GET_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_item_get_data) {
        for (i = 0; i < SFX_ITEM_GET_LEN; i++) {
            WORD seg = i / (SFX_ITEM_GET_LEN / 3);
            WORD freq;
            WORD env = 100;
            if (seg == 0) freq = 20;
            else if (seg == 1) freq = 25;
            else freq = 30;
            /* fade out last third */
            if (i > SFX_ITEM_GET_LEN * 2 / 3)
                env = 100 - ((i - SFX_ITEM_GET_LEN * 2 / 3) * 100) / (SFX_ITEM_GET_LEN / 3);
            sfx_item_get_data[i] = (BYTE)((((i * freq) & 0xFF) > 128 ? 60 : -60) * env / 100);
        }
    }

    /* Player hit: low noise burst */
    sfx_player_hit_data = (BYTE *)AllocMem(SFX_PLAYER_HIT_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_player_hit_data) {
        for (i = 0; i < SFX_PLAYER_HIT_LEN; i++) {
            WORD env = 127 - (i * 127) / SFX_PLAYER_HIT_LEN;
            WORD noise = (sfx_rng() % 200) - 100;
            sfx_player_hit_data[i] = (BYTE)((noise * env) / 127);
        }
    }

    /* Enemy hit: mid-pitched tick */
    sfx_enemy_hit_data = (BYTE *)AllocMem(SFX_ENEMY_HIT_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_enemy_hit_data) {
        for (i = 0; i < SFX_ENEMY_HIT_LEN; i++) {
            WORD env = 127 - (i * 127) / SFX_ENEMY_HIT_LEN;
            sfx_enemy_hit_data[i] = (BYTE)((((i * 15) & 0xFF) > 128 ? 50 : -50) * env / 127);
        }
    }

    /* Door open: descending sweep */
    sfx_door_open_data = (BYTE *)AllocMem(SFX_DOOR_OPEN_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_door_open_data) {
        for (i = 0; i < SFX_DOOR_OPEN_LEN; i++) {
            WORD env = 127 - (i * 80) / SFX_DOOR_OPEN_LEN;
            WORD freq = 30 - (i * 20) / SFX_DOOR_OPEN_LEN;
            sfx_door_open_data[i] = (BYTE)((((i * freq) & 0xFF) > 128 ? 50 : -50) * env / 127);
        }
    }

    /* Jump: quick rising tone */
    sfx_jump_data = (BYTE *)AllocMem(SFX_JUMP_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_jump_data) {
        for (i = 0; i < SFX_JUMP_LEN; i++) {
            WORD env = 127 - (i * 127) / SFX_JUMP_LEN;
            WORD freq = 10 + (i * 20) / SFX_JUMP_LEN;
            sfx_jump_data[i] = (BYTE)((((i * freq) & 0xFF) > 128 ? 50 : -50) * env / 127);
        }
    }

    /* Setup SFX structures */
    sfx_shoot_sfx.sfx_ptr = sfx_shoot_data;
    sfx_shoot_sfx.sfx_len = SFX_SHOOT_LEN / 2;
    sfx_shoot_sfx.sfx_per = 180;
    sfx_shoot_sfx.sfx_vol = 50;
    sfx_shoot_sfx.sfx_cha = -1;
    sfx_shoot_sfx.sfx_pri = 30;

    sfx_missile_sfx.sfx_ptr = sfx_missile_data;
    sfx_missile_sfx.sfx_len = SFX_MISSILE_LEN / 2;
    sfx_missile_sfx.sfx_per = 400;
    sfx_missile_sfx.sfx_vol = 60;
    sfx_missile_sfx.sfx_cha = -1;
    sfx_missile_sfx.sfx_pri = 40;

    sfx_bomb_sfx.sfx_ptr = sfx_bomb_data;
    sfx_bomb_sfx.sfx_len = SFX_BOMB_LEN / 2;
    sfx_bomb_sfx.sfx_per = 350;
    sfx_bomb_sfx.sfx_vol = 64;
    sfx_bomb_sfx.sfx_cha = -1;
    sfx_bomb_sfx.sfx_pri = 50;

    sfx_item_get_sfx.sfx_ptr = sfx_item_get_data;
    sfx_item_get_sfx.sfx_len = SFX_ITEM_GET_LEN / 2;
    sfx_item_get_sfx.sfx_per = 200;
    sfx_item_get_sfx.sfx_vol = 64;
    sfx_item_get_sfx.sfx_cha = -1;
    sfx_item_get_sfx.sfx_pri = 60;

    sfx_player_hit_sfx.sfx_ptr = sfx_player_hit_data;
    sfx_player_hit_sfx.sfx_len = SFX_PLAYER_HIT_LEN / 2;
    sfx_player_hit_sfx.sfx_per = 450;
    sfx_player_hit_sfx.sfx_vol = 64;
    sfx_player_hit_sfx.sfx_cha = -1;
    sfx_player_hit_sfx.sfx_pri = 55;

    sfx_enemy_hit_sfx.sfx_ptr = sfx_enemy_hit_data;
    sfx_enemy_hit_sfx.sfx_len = SFX_ENEMY_HIT_LEN / 2;
    sfx_enemy_hit_sfx.sfx_per = 220;
    sfx_enemy_hit_sfx.sfx_vol = 48;
    sfx_enemy_hit_sfx.sfx_cha = -1;
    sfx_enemy_hit_sfx.sfx_pri = 25;

    sfx_door_open_sfx.sfx_ptr = sfx_door_open_data;
    sfx_door_open_sfx.sfx_len = SFX_DOOR_OPEN_LEN / 2;
    sfx_door_open_sfx.sfx_per = 280;
    sfx_door_open_sfx.sfx_vol = 50;
    sfx_door_open_sfx.sfx_cha = -1;
    sfx_door_open_sfx.sfx_pri = 35;

    sfx_jump_sfx.sfx_ptr = sfx_jump_data;
    sfx_jump_sfx.sfx_len = SFX_JUMP_LEN / 2;
    sfx_jump_sfx.sfx_per = 160;
    sfx_jump_sfx.sfx_vol = 40;
    sfx_jump_sfx.sfx_cha = -1;
    sfx_jump_sfx.sfx_pri = 20;
}

/* SFX callback functions (called from game.c) */
void sfx_shoot(void)
{
    if (sfx_shoot_data)
        mt_playfx(CUSTOM_BASE, &sfx_shoot_sfx);
}

void sfx_missile(void)
{
    if (sfx_missile_data)
        mt_playfx(CUSTOM_BASE, &sfx_missile_sfx);
}

void sfx_explode(void)
{
    if (sfx_bomb_data)
        mt_playfx(CUSTOM_BASE, &sfx_bomb_sfx);
}

void sfx_powerup(void)
{
    if (sfx_item_get_data)
        mt_playfx(CUSTOM_BASE, &sfx_item_get_sfx);
}

void sfx_player_hit(void)
{
    if (sfx_player_hit_data)
        mt_playfx(CUSTOM_BASE, &sfx_player_hit_sfx);
}

void sfx_hit(void)
{
    if (sfx_enemy_hit_data)
        mt_playfx(CUSTOM_BASE, &sfx_enemy_hit_sfx);
}

void sfx_door(void)
{
    if (sfx_door_open_data)
        mt_playfx(CUSTOM_BASE, &sfx_door_open_sfx);
}

void sfx_jump(void)
{
    if (sfx_jump_data)
        mt_playfx(CUSTOM_BASE, &sfx_jump_sfx);
}

/* --- MOD loading --- */

static UBYTE *load_file_to_chip(const char *path, ULONG *out_size)
{
    BPTR fh;
    UBYTE *buf = NULL;
    LONG len;
    fh = Open((CONST_STRPTR)path, MODE_OLDFILE);
    if (!fh) return NULL;

    /* Get file size via Seek */
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

/* Validate a ProTracker MOD image in memory.
 * Returns 1 if the file is a well-formed 4-channel MOD whose declared
 * pattern + sample data fits within mod_size, 0 otherwise. */
static WORD validate_mod(const UBYTE *data, ULONG size)
{
    const UBYTE *sig;
    const UBYTE *ptable;
    ULONG song_len;
    ULONG max_pat = 0;
    ULONG num_patterns;
    ULONG pattern_bytes;
    ULONG sample_bytes = 0;
    ULONG needed;
    WORD i;

    if (!data || size < 1084) return 0;

    /* Signature at offset 1080 - accept common 4-channel variants only,
       since ptplayer here assumes standard MOD layout. */
    sig = data + 1080;
    if (!((sig[0] == 'M' && sig[1] == '.' && sig[2] == 'K' && sig[3] == '.') ||
          (sig[0] == 'M' && sig[1] == '!' && sig[2] == 'K' && sig[3] == '!') ||
          (sig[0] == 'F' && sig[1] == 'L' && sig[2] == 'T' && sig[3] == '4') ||
          (sig[0] == '4' && sig[1] == 'C' && sig[2] == 'H' && sig[3] == 'N'))) {
        return 0;
    }

    /* Song length (1..128) and pattern table */
    song_len = data[950];
    if (song_len == 0 || song_len > 128) return 0;
    ptable = data + 952;

    /* Highest pattern number referenced */
    for (i = 0; i < 128; i++) {
        if (ptable[i] > max_pat) max_pat = ptable[i];
    }
    if (max_pat >= 128) return 0;
    num_patterns = max_pat + 1;
    pattern_bytes = num_patterns * 1024UL; /* 64 rows * 4 ch * 4 bytes */

    /* Sample lengths are stored as words (in 2-byte units) at offset
       22 + 30*i for i in 0..30. */
    for (i = 0; i < 31; i++) {
        const UBYTE *s = data + 20 + i * 30;
        ULONG len_words = ((ULONG)s[22] << 8) | (ULONG)s[23];
        sample_bytes += len_words * 2UL;
    }

    needed = 1084UL + pattern_bytes + sample_bytes;
    if (needed > size) return 0;

    return 1;
}

/* --- Screen setup with double buffering --- */

static WORD setup_display(void)
{
    WORD i;

    screen = OpenScreenTags(NULL,
        SA_Width,     SCREEN_W,
        SA_Height,    SCREEN_H,
        SA_Depth,     4,
        SA_DisplayID, LORES_KEY,
        SA_Title,     (ULONG)"Orb Hunter",
        SA_ShowTitle, FALSE,
        SA_Quiet,     TRUE,
        SA_Type,      CUSTOMSCREEN,
        TAG_DONE);

    if (!screen) return 0;

    /* Set palette */
    {
        struct ViewPort *vp = &screen->ViewPort;
        for (i = 0; i < 16; i++) {
            SetRGB4(vp, i,
                (palette[i] >> 8) & 0xF,
                (palette[i] >> 4) & 0xF,
                palette[i] & 0xF);
        }
    }

    /* Allocate two screen buffers for double buffering */
    sbuf[0] = AllocScreenBuffer(screen, NULL, SB_SCREEN_BITMAP);
    sbuf[1] = AllocScreenBuffer(screen, NULL, 0);
    if (!sbuf[0] || !sbuf[1]) return 0;

    /* Create message ports for safe buffer switching */
    db_port[0] = CreateMsgPort();
    db_port[1] = CreateMsgPort();
    if (!db_port[0] || !db_port[1]) return 0;

    sbuf[0]->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort = db_port[0];
    sbuf[1]->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort = db_port[1];

    /* Init RastPorts for each buffer */
    InitRastPort(&rp_buf[0]);
    rp_buf[0].BitMap = sbuf[0]->sb_BitMap;
    InitRastPort(&rp_buf[1]);
    rp_buf[1].BitMap = sbuf[1]->sb_BitMap;

    /* Clear both buffers */
    SetRast(&rp_buf[0], 0);
    SetRast(&rp_buf[1], 0);

    /* Open a borderless window for IDCMP input */
    window = OpenWindowTags(NULL,
        WA_Left,       0,
        WA_Top,        0,
        WA_Width,      SCREEN_W,
        WA_Height,     SCREEN_H,
        WA_CustomScreen, (ULONG)screen,
        WA_Borderless, TRUE,
        WA_Backdrop,   TRUE,
        WA_Activate,   TRUE,
        WA_RMBTrap,    TRUE,
        WA_IDCMP,      IDCMP_RAWKEY | IDCMP_CLOSEWINDOW,
        TAG_DONE);

    if (!window) return 0;

    cur_buf = 1; /* We'll draw to buffer 1 first, display is showing buffer 0 */
    safe_to_write[0] = TRUE;
    safe_to_write[1] = TRUE;

    return 1;
}

static void swap_buffers(void)
{
    /* Make sure the buffer we're about to display is safe */
    if (!safe_to_write[cur_buf]) {
        while (!GetMsg(db_port[cur_buf]))
            WaitPort(db_port[cur_buf]);
        safe_to_write[cur_buf] = TRUE;
    }

    /* Wait for blitter to finish drawing */
    WaitBlit();

    /* Swap: show the buffer we just drew to */
    ChangeScreenBuffer(screen, sbuf[cur_buf]);
    safe_to_write[cur_buf] = FALSE;

    /* Switch to the other buffer for next frame's drawing */
    cur_buf ^= 1;

    /* Make sure the other buffer (which we'll draw to next) is safe */
    if (!safe_to_write[cur_buf]) {
        while (!GetMsg(db_port[cur_buf]))
            WaitPort(db_port[cur_buf]);
        safe_to_write[cur_buf] = TRUE;
    }
}

static void cleanup_display(void)
{
    WORD i;

    if (window) { CloseWindow(window); window = NULL; }

    /* Wait for any pending buffer swaps */
    if (!safe_to_write[0]) {
        while (!GetMsg(db_port[0])) WaitPort(db_port[0]);
    }
    if (!safe_to_write[1]) {
        while (!GetMsg(db_port[1])) WaitPort(db_port[1]);
    }

    for (i = 0; i < 2; i++) {
        if (sbuf[i]) { FreeScreenBuffer(screen, sbuf[i]); sbuf[i] = NULL; }
        if (db_port[i]) { DeleteMsgPort(db_port[i]); db_port[i] = NULL; }
    }

    if (screen) { CloseScreen(screen); screen = NULL; }
}

/* --- Bridge hooks --- */

/* Copy a fixed message into a bridge-supplied buffer, always terminating
 * when bufsz > 0. Handles NULL buf and non-positive bufsz safely. */
static void copy_bounded(char *buf, int bufsz, const char *msg)
{
    int i;
    if (!buf || bufsz <= 0) return;
    for (i = 0; msg[i] && i < bufsz - 1; i++) buf[i] = msg[i];
    buf[i] = '\0';
}

static int hook_reset(const char *args, char *buf, int bufsz)
{
    game_init(&gs);
    copy_bounded(buf, bufsz, "Game reset");
    return 0;
}

static int hook_give_items(const char *args, char *buf, int bufsz)
{
    gs.hunter.has_roll_form = 1;
    gs.hunter.has_bombs = 1;
    gs.hunter.has_missiles = 1;
    gs.hunter.missiles = 30;
    gs.hunter.max_missiles = 30;
    gs.hunter.has_long_beam = 1;
    copy_bounded(buf, bufsz, "All items given");
    return 0;
}

static int hook_full_health(const char *args, char *buf, int bufsz)
{
    char tmp[32];
    gs.hunter.health = gs.hunter.max_energy;
    if (!buf || bufsz <= 0) return 0;
    sprintf(tmp, "Health: %ld", (long)gs.hunter.health);
    copy_bounded(buf, bufsz, tmp);
    return 0;
}

/* --- Main --- */

int main(void)
{
    WORD running = 1;
    WORD music_playing = 0;

    /* Open libraries */
    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 39);
    if (!IntuitionBase || !GfxBase) {
        if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
        if (GfxBase) CloseLibrary((struct Library *)GfxBase);
        return 20;
    }

    /* Init bridge */
    ab_init("orb_hunter");
    AB_I("Orb Hunter starting up");

    /* Register tunables */
    ab_register_var("health",       AB_TYPE_I32, &gs.hunter.health);
    ab_register_var("max_energy",   AB_TYPE_I32, &gs.hunter.max_energy);
    ab_register_var("missiles",     AB_TYPE_I32, &gs.hunter.missiles);
    ab_register_var("max_missiles", AB_TYPE_I32, &gs.hunter.max_missiles);
    ab_register_var("room_x",       AB_TYPE_I32, &gs.room_x);
    ab_register_var("room_y",       AB_TYPE_I32, &gs.room_y);

    /* Register hooks */
    ab_register_hook("reset",       "Reset game to initial state", hook_reset);
    ab_register_hook("give_items",  "Give all power-ups",          hook_give_items);
    ab_register_hook("full_health", "Restore full health",         hook_full_health);

    /* Open screen with double buffering */
    if (!setup_display()) {
        AB_E("Failed to setup display");
        cleanup_display();
        ab_cleanup();
        CloseLibrary((struct Library *)GfxBase);
        CloseLibrary((struct Library *)IntuitionBase);
        return 20;
    }

    /* Build sound effects */
    build_sfx();

    /* Load MOD music */
    mod_data = load_file_to_chip("DH2:Dev/axelf.mod", &mod_size);
    if (mod_data) {
        AB_I("Loaded axelf.mod (%ld bytes)", (long)mod_size);
        if (validate_mod(mod_data, mod_size)) {
            /* Install CIA timer for music playback only after validation
               succeeds - otherwise mt_init on a malformed file could walk
               off the end of the buffer. */
            mt_install_cia(CUSTOM_BASE, NULL, 1); /* PAL */
            mt_init(CUSTOM_BASE, mod_data, NULL, 0);
            mt_MusicChannels = 2; /* Reserve 2 channels for music, 2 for SFX */
            mt_Enable = 1;
            music_playing = 1;
        } else {
            AB_W("axelf.mod failed validation - no music");
            FreeMem(mod_data, mod_size);
            mod_data = NULL;
            mod_size = 0;
        }
    } else {
        AB_W("Could not load axelf.mod - no music");
    }

    /* Generate world data first */
    levels_init();

    /* Init game state (but start on title screen) */
    memset(&gs, 0, sizeof(GameState));
    gs.state = STATE_TITLE;
    input_reset();

    AB_I("Entering main loop");

    /* Main loop */
    while (running) {
        struct IntuiMessage *msg;
        UWORD inp;

        /* Process window messages */
        while ((msg = (struct IntuiMessage *)GetMsg(window->UserPort))) {
            ULONG cl = msg->Class;
            UWORD code = msg->Code;
            ReplyMsg((struct Message *)msg);

            if (cl == IDCMP_CLOSEWINDOW) {
                running = 0;
            }
            else if (cl == IDCMP_RAWKEY) {
                if (code & 0x80) {
                    /* Key up */
                    input_key_up(code & 0x7F);
                } else {
                    input_key_down(code);
                }
            }
        }

        /* Check Ctrl-C */
        if (SetSignal(0L, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) {
            running = 0;
        }

        /* Read input */
        inp = input_read();

        if (inp & INPUT_ESC) {
            running = 0;
        }

        /* Get the back buffer's RastPort */
        {
            struct RastPort *rp = &rp_buf[cur_buf];

            /* State machine */
            switch (gs.state) {
                case STATE_TITLE:
                    draw_title(rp);
                    if (inp & (INPUT_FIRE | INPUT_START)) {
                        game_init(&gs);
                    }
                    break;

                case STATE_PLAYING:
                case STATE_ROOM_TRANSITION:
                    game_update(&gs,
                        (inp & INPUT_LEFT) ? 1 : 0,
                        (inp & INPUT_RIGHT) ? 1 : 0,
                        (inp & INPUT_UP) ? 1 : 0,
                        (inp & INPUT_DOWN) ? 1 : 0,
                        (inp & INPUT_UP) ? 1 : 0,
                        (inp & INPUT_FIRE) ? 1 : 0);
                    draw_frame(rp, &gs);
                    break;

                case STATE_ITEM_GET:
                    draw_frame(rp, &gs);
                    draw_item_get(rp, &gs);
                    if (inp & (INPUT_FIRE | INPUT_START)) {
                        gs.state = STATE_PLAYING;
                    }
                    break;

                case STATE_DEAD:
                    game_update(&gs,
                        (inp & INPUT_LEFT) ? 1 : 0,
                        (inp & INPUT_RIGHT) ? 1 : 0,
                        (inp & INPUT_UP) ? 1 : 0,
                        (inp & INPUT_DOWN) ? 1 : 0,
                        (inp & INPUT_UP) ? 1 : 0,
                        (inp & INPUT_FIRE) ? 1 : 0);
                    draw_frame(rp, &gs);
                    break;

                case STATE_GAMEOVER:
                    draw_gameover(rp);
                    if (inp & (INPUT_FIRE | INPUT_START)) {
                        gs.state = STATE_TITLE;
                    }
                    break;
            }
        }

        /* Poll bridge */
        ab_poll();

        /* Swap buffers and sync to VBlank */
        WaitTOF();
        swap_buffers();

        gs.frame++;
    }

    AB_I("Shutting down");

    /* Cleanup */
    if (music_playing) {
        mt_end(CUSTOM_BASE);
        mt_remove_cia(CUSTOM_BASE);
    }

    if (mod_data)           FreeMem(mod_data, mod_size);
    if (sfx_shoot_data)     FreeMem(sfx_shoot_data, SFX_SHOOT_LEN);
    if (sfx_missile_data)   FreeMem(sfx_missile_data, SFX_MISSILE_LEN);
    if (sfx_bomb_data)      FreeMem(sfx_bomb_data, SFX_BOMB_LEN);
    if (sfx_item_get_data)  FreeMem(sfx_item_get_data, SFX_ITEM_GET_LEN);
    if (sfx_player_hit_data) FreeMem(sfx_player_hit_data, SFX_PLAYER_HIT_LEN);
    if (sfx_enemy_hit_data) FreeMem(sfx_enemy_hit_data, SFX_ENEMY_HIT_LEN);
    if (sfx_door_open_data) FreeMem(sfx_door_open_data, SFX_DOOR_OPEN_LEN);
    if (sfx_jump_data)      FreeMem(sfx_jump_data, SFX_JUMP_LEN);

    input_reset();

    cleanup_display();

    ab_cleanup();
    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);

    return 0;
}
