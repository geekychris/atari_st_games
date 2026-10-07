// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * RJ'S 70TH BIRTHDAY BASH - Atari STE port.
 *
 * Port of the Amiga version (main.c in geekychris/amiga_games, kept as
 * main.c.amiga): game.c and rooms.c are unmodified; draw.c caches the
 * RJ head on the title as one operation; sound.c's ptplayer calls go to
 * the Paula emulation (STE DMA sound), its voice clips under 8.3 names.  The guest
 * list and high scores load and save through the AmigaDOS shim
 * (GUESTS.TXT, HISCORES.DAT next to the program).
 *
 * The original's two MOD files and four arcade voice clips are not
 * included (third-party tracker modules and samples of unknown
 * licence); copied next to the program as PARTY.MOD, BIRTHDAY.MOD and
 * SND_ARC1..4.RAW (make assets UPSTREAM=<amiga_games checkout>), they
 * are used as on the Amiga.
 *
 * main.c's loop body runs once per 50 Hz VBL (as on the Amiga), the
 * drawing once per frame:
 *   pages (title, help, high scores, credits...)  on the HUD layer
 *   the party   the six rooms, pre-rendered once into a 1920 pixel
 *               strip by rooms_draw_bg, are copied by the STE blitter
 *               at the camera position; guests, items, the player, the
 *               HUD and messages are drawn over them each frame
 *
 * Controls: cursor keys or joystick move, Space / Alt / fire act, Esc
 * leaves the party (credits), Q on the title quits; letters, Return,
 * Backspace and Delete for names.  With Hatari --natfeats on, events and
 * symbol addresses are logged ("RJBB ..." lines).
 */
#include <osbind.h>
#include <stdio.h>
#include <string.h>
#include <exec/memory.h>
#include <proto/dos.h>
#include "bridge_client.h"
#include "game.h"
#include "ptplayer.h"
#include "natfeats.h"
#include "paula.h"
#include "st_blit.h"

#define FRCLOCK (*(volatile long *)0x466)

/* Custom chip base for ptplayer (the Paula emulation) */
#define CUSTOM_BASE ((void *)&custom)

void input_exit(void);

/* MOD data */
static UBYTE *mod_data = NULL;
static ULONG  mod_size = 0;
static UBYTE *bday_mod_data = NULL;
static ULONG  bday_mod_size = 0;
static WORD music_playing = 0;
static WORD bday_playing = 0;

/* Game state */
static GameState gs;
static InputState inp;

/* Palette */
static UWORD palette[NUM_COLORS] = {
    0x113,  /*  0: COL_BG       - dark blue-black */
    0xFFF,  /*  1: COL_WHITE    - white */
    0x830,  /*  2: COL_BROWN    - brown (wood) */
    0x520,  /*  3: COL_DKBROWN  - dark brown */
    0xF00,  /*  4: COL_RED      - red */
    0x0A0,  /*  5: COL_GREEN    - green */
    0x23C,  /*  6: COL_BLUE     - blue */
    0x8CF,  /*  7: COL_LTBLUE   - light blue */
    0xFE0,  /*  8: COL_YELLOW   - yellow */
    0xF80,  /*  9: COL_ORANGE   - orange */
    0xF5A,  /* 10: COL_PINK     - pink/magenta */
    0x6E6,  /* 11: COL_LTGREEN  - light green */
    0xDA8,  /* 12: COL_TAN      - tan/beige */
    0x888,  /* 13: COL_GREY     - grey */
    0x800,  /* 14: COL_DKRED    - dark red (carpet) */
    0xFF8,  /* 15: COL_BTYELLOW - bright yellow */
};

/* --- File loading --- */

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

/* --- Display setup --- */

static void log_line(const char *fmt, long a, long b, long c, long d)
{
    char buf[128];
    strcpy(buf, "RJBB ");
    snprintf(buf + 5, sizeof(buf) - 6, fmt, a, b, c, d);
    strcat(buf, "\n");
    nf_print(buf);
}

/* one 50 Hz step of main.c's loop, without the drawing; 0 to quit */
static int game_step(void)
{
    /* Read input */
    input_read(&inp, NULL);

    /* Q from title screen = quit */
    if (gs.state == GS_TITLE && inp.last_char == 'q') {
        return 0;
    }

    /* ESC during gameplay = back to title via credits */
    if ((inp.bits & INP_ESC) && gs.state == GS_PLAYING) {
        gs.state = GS_CREDITS;
        gs.credits_scroll = 0;
    }

    /* Update game */
    game_update(&gs, &inp);

    /* Check if arcade cabinet A voice clip finished playing */
    arcade_voice_check_done();

        /* Switch to birthday music on win/gameover/credits */
        if ((gs.state == GS_WIN || gs.state == GS_GAMEOVER ||
             gs.state == GS_CREDITS) && !bday_playing && bday_mod_data) {
            mt_end(CUSTOM_BASE);
            mt_init(CUSTOM_BASE, bday_mod_data, NULL, 0);
            mt_MusicChannels = 2;
            mt_Enable = 1;
            bday_playing = 1;
            music_playing = 0;
        }
        /* Switch back to party music on title/playing */
        if ((gs.state == GS_TITLE || gs.state == GS_PLAYING)
            && !music_playing && mod_data) {
            mt_end(CUSTOM_BASE);
            mt_init(CUSTOM_BASE, mod_data, NULL, 0);
            mt_MusicChannels = 2;
            mt_Enable = 1;
            music_playing = 1;
            bday_playing = 0;
        }

    return 1;
}

/* The house: rooms_draw_bg depends on the camera only, so the six rooms
 * are rendered once into a strip (one room per 320 pixel chunk) and the
 * blitter copies the visible window each frame.  Without a blitter, or
 * without the memory, the rooms are drawn as on the Amiga. */
#define HOUSE_BPR (WORLD_W / 2)
static UWORD *house;

static void build_house(void)
{
    struct RastPort srp;
    UWORD *scratch = (UWORD *)Malloc(32000);
    WORD cam = gs.camera_x, r, y;

    if (!blit_available() || !scratch || !(house = (UWORD *)Malloc((long)HOUSE_BPR * 200))) {
        if (scratch) Mfree(scratch);
        house = NULL;
        return;
    }
    memset(&srp, 0, sizeof(srp));
    srp.base = scratch;
    for (r = 0; r < ROOM_COUNT; r++) {
        memset(scratch, 0, 32000);
        gs.camera_x = r * ROOM_W;
        draw_clear(&srp);
        rooms_draw_bg(&srp, &gs);
        for (y = 0; y < 200; y++)
            memcpy((UBYTE *)house + (long)y * HOUSE_BPR + r * 160, scratch + y * 80, 160);
    }
    gs.camera_x = cam;
    Mfree(scratch);
}

/* main.c's drawing for the current state */
static void game_draw(void)
{
    static int mode = -1;
    int m = gs.state == GS_PLAYING;
    struct RastPort *rp;

    if (m != mode) {
        gfx_bg_clear();
        gfx_bg_to_screens();
        mode = m;
    }
    if (!m) {
        /* the pages clear the screen themselves: on the HUD layer only
         * what changed is rendered */
        rp = gfx_hud();
        switch (gs.state) {
            case GS_TITLE:
                draw_title(rp, &gs);
                break;
            case GS_GAMEOVER:
                draw_gameover(rp, &gs);
                break;
            case GS_WIN:
                draw_win(rp, &gs);
                break;
            case GS_CREDITS:
                draw_credits(rp, &gs);
                break;
            case GS_HISCORE:
                draw_hiscore(rp, &gs);
                break;
            case GS_ENTER_NAME:
            case GS_ADD_GUEST:
                draw_enter_name(rp, &gs);
                break;
            case GS_HELP:
                draw_help(rp, &gs);
                break;
            case GS_GUEST_EDIT:
                draw_guest_edit(rp, &gs);
                break;
            case GS_JAIL:
                draw_jail(rp, &gs);
                break;
        }
        gfx_hud_commit();
        gfx_restore_back();
        return;
    }

    gfx_hud_commit();		/* (empty: drops the page's operations) */
    rp = gfx_back_nomark();
    if (house) {
        WORD cam = gs.camera_x;
        if (cam < 0) cam = 0;
        if (cam > WORLD_W - SCREEN_W) cam = WORLD_W - SCREEN_W;
        blit_area(house, HOUSE_BPR, cam, 0, rp->base, 160, 0, 0, SCREEN_W, 200, BLIT_COPY);
    } else {
        draw_clear(rp);
        rooms_draw_bg(rp, &gs);
    }
    rooms_draw_details(rp, &gs);
    draw_items(rp, &gs);
    draw_guests(rp, &gs);
    draw_boings(rp, &gs);
    draw_cops(rp, &gs);
    draw_player(rp, &gs);
    draw_puffs(rp, &gs);
    draw_hud(rp, &gs);
    draw_message(rp, &gs);
}

int main(void)
{
    long old_ssp, last_vbl, frames = 0;
    int running = 1, last_state = -1, last_room = -1;
    long last_score = -1;

    nf_init();
    old_ssp = Super(0L);

    /* Init tables */
    game_init_tables();

    /* Load guest names */
    game_load_names(&gs, "DH2:Dev/guests.txt");

    /* Load high scores */
    game_load_hiscores(&gs, "DH2:Dev/hiscores.dat");

    if (!gfx_init(palette, 16)) {
        Super((void *)old_ssp);
        Cconws("Not enough memory\r\n");
        return 1;
    }
    gfx_set_frame_vbls(1);
    build_house();

    /* Build sound effects */
    sound_init();
    arcade_voice_load();

    /* Load MOD music */
    paula_set_rate(6258);
    mod_data = load_file_to_chip("DH2:Dev/party.mod", &mod_size);
    bday_mod_data = load_file_to_chip("DH2:Dev/birthday.mod", &bday_mod_size);
    mt_install_cia(CUSTOM_BASE, NULL, 1);  /* PAL */
    if (mt_sound_ok() != 0)
        log_line("no DMA sound - continuing without sound", 0, 0, 0, 0);
    if (mod_data) {
        mt_init(CUSTOM_BASE, mod_data, NULL, 0);
        mt_MusicChannels = 2;  /* 2 music, 2 SFX */
        mt_Enable = 1;
        music_playing = 1;
    }

    /* Init game */
    gs.state = GS_TITLE;
    gs.title_blink = 0;
    input_init();
    memset(&inp, 0, sizeof(inp));

    log_line("START names=%ld party.mod=%ld birthday.mod=%ld house=%ld", (long)gs.name_count,
             (long)(mod_data != NULL), (long)(bday_mod_data != NULL), (long)(house != NULL));
    log_line("SYMBOL gs 0x%06lx", (long)&gs, 0, 0, 0);
    log_line("SYMBOL state 0x%06lx", (long)&gs.state, 0, 0, 0);
    log_line("SYMBOL camera_x 0x%06lx", (long)&gs.camera_x, 0, 0, 0);
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

        if (gs.state != last_state || gs.current_room != last_room) {
            static const char *n[] = { "TITLE", "PLAYING", "GAMEOVER", "CREDITS", "WIN",
                "HISCORE", "ENTER_NAME", "ADD_GUEST", "HELP", "GUEST_EDIT", "JAIL" };
            log_line("STATE %s room=%ld lives=%ld happiness=%ld",
                     (long)(gs.state >= 0 && gs.state <= 10 ? n[gs.state] : "?"),
                     (long)gs.current_room, (long)gs.lives, (long)gs.happiness);
            last_state = gs.state;
            last_room = gs.current_room;
        }
        if (gs.score != last_score) {
            if (gs.state == GS_PLAYING)
                log_line("SCORE %ld", (long)gs.score, 0, 0, 0);
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
    if (music_playing || bday_playing) {
        mt_end(CUSTOM_BASE);
    }
    mt_remove_cia(CUSTOM_BASE);
    sound_cleanup();
    arcade_voice_cleanup();
    input_exit();
    gfx_exit();
    Super((void *)old_ssp);
    return 0;
}
