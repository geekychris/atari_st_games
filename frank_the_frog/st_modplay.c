/*
 * Frank the Frog - music on the Atari ST YM2149.
 *
 * The Amiga modplay.c generates a 4 channel heavy metal track (guitar,
 * palm mute, bass, drums) and plays it through Paula samples.  This file
 * keeps its pattern generator and song order VERBATIM (the section
 * between the "pattern building" markers is copied from modplay.c) and
 * replays it on the PSG:
 *
 *   tracker ch 0 lead guitar  -> YM A square tone
 *   tracker ch 1 harmony      -> dropped (the PSG has three voices)
 *   tracker ch 2 bass         -> YM B square tone
 *   tracker ch 3 drums        -> YM C: kick = falling tone, snare/hihat = noise
 *
 * Sound effects (sound.c) take over YM C for a few frames, like the
 * Amiga version steals Paula channel 3.  Note index n plays MIDI note
 * 36 + n (E_1 bass = E2, E_2 guitar = E3); volumes 0-64 map to 0-15.
 */
#include "amiga_types.h"
#include "modplay.h"
#include "st_ym.h"
#include "st_modplay.h"

/* ---- Configuration ---- */
#define NUM_SAMPLES   6
#define NUM_PATTERNS  4
#define ROWS_PER_PAT  64
#define NUM_CHANNELS  4
#define SONG_LENGTH   8
#define SPEED_DEFAULT 3  /* ticks per row - fast for metal */


/* ---- Note indices into period table ---- */
#define N__ 0xFF
#define C_1 0
#define D_1 2
#define E_1 4
#define F_1 5
#define Fs1 6
#define G_1 7
#define A_1 9
#define B_1 11
#define C_2 12
#define Cs2 13
#define D_2 14
#define E_2 16
#define F_2 17
#define Fs2 18
#define G_2 19
#define A_2 21
#define B_2 23
#define C_3 24


enum { S_GUITAR=1, S_PMUTE, S_BASS, S_KICK, S_SNARE, S_HIHAT };

/* ---- Pattern note (compact) ---- */
typedef struct {
    UBYTE smp;    /* sample 1-6, 0 = no change */
    UBYTE note;   /* period table index, N__ = none */
    UBYTE vol;    /* 0xFF = no change, else 0-64 */
} PNote;


static struct {
    int playing, speed, tick, row, position;
    PNote pat[NUM_PATTERNS][ROWS_PER_PAT][NUM_CHANNELS];
    UBYTE order[SONG_LENGTH];
} mp;

/* per YM channel voice state */
static struct { int smp, note, vol, age; } v[3];
static int sfx_frames, sfx_kind, sfx_t;

/* ---- Pattern building ---- */

static void sn(int p, int r, int c, UBYTE smp, UBYTE note, UBYTE vol)
{
    if (p < NUM_PATTERNS && r < ROWS_PER_PAT && c < NUM_CHANNELS) {
        mp.pat[p][r][c].smp = smp;
        mp.pat[p][r][c].note = note;
        mp.pat[p][r][c].vol = vol;
    }
}

/*
 * Metal riff patterns at speed 3 (~16.7 rows/sec, 60ms per row).
 * Gallop rhythm: HIT . hit hit . hit hit . (8 rows = ~480ms)
 */

static void build_pattern0(void)
{
    /* Main E5 power chord gallop */
    int r;
    for (r = 0; r < 64; r += 8) {
        /* Lead guitar: distorted E power chord */
        sn(0, r+0, 0, S_GUITAR, E_2, 64);
        sn(0, r+2, 0, S_PMUTE,  E_2, 48);
        sn(0, r+3, 0, S_PMUTE,  E_2, 48);
        sn(0, r+5, 0, S_PMUTE,  E_2, 48);
        sn(0, r+6, 0, S_PMUTE,  E_2, 48);

        /* Harmony: fifth (B) for power chord thickness */
        sn(0, r+0, 1, S_GUITAR, B_2, 50);
        sn(0, r+2, 1, S_PMUTE,  B_2, 40);
        sn(0, r+3, 1, S_PMUTE,  B_2, 40);
        sn(0, r+5, 1, S_PMUTE,  B_2, 40);
        sn(0, r+6, 1, S_PMUTE,  B_2, 40);

        /* Bass follows root */
        sn(0, r+0, 2, S_BASS, E_1, 60);

        /* Drums: kick-hihat-snare-hihat */
        sn(0, r+0, 3, S_KICK,  C_2, 64);
        sn(0, r+2, 3, S_HIHAT, C_3, 40);
        sn(0, r+4, 3, S_SNARE, C_2, 56);
        sn(0, r+6, 3, S_HIHAT, C_3, 40);
    }
}

static void build_pattern1(void)
{
    /* G5 - A5 variation */
    int r;
    for (r = 0; r < 32; r += 8) {
        /* G power chord */
        sn(1, r+0, 0, S_GUITAR, G_2, 64);
        sn(1, r+2, 0, S_PMUTE,  G_2, 48);
        sn(1, r+3, 0, S_PMUTE,  G_2, 48);
        sn(1, r+5, 0, S_PMUTE,  G_2, 48);
        sn(1, r+6, 0, S_PMUTE,  G_2, 48);

        sn(1, r+0, 1, S_GUITAR, D_2, 50);
        sn(1, r+2, 1, S_PMUTE,  D_2, 40);
        sn(1, r+3, 1, S_PMUTE,  D_2, 40);
        sn(1, r+5, 1, S_PMUTE,  D_2, 40);
        sn(1, r+6, 1, S_PMUTE,  D_2, 40);

        sn(1, r+0, 2, S_BASS, G_1, 60);

        sn(1, r+0, 3, S_KICK,  C_2, 64);
        sn(1, r+2, 3, S_HIHAT, C_3, 40);
        sn(1, r+4, 3, S_SNARE, C_2, 56);
        sn(1, r+6, 3, S_HIHAT, C_3, 40);
    }
    for (r = 32; r < 64; r += 8) {
        /* A power chord */
        sn(1, r+0, 0, S_GUITAR, A_2, 64);
        sn(1, r+2, 0, S_PMUTE,  A_2, 48);
        sn(1, r+3, 0, S_PMUTE,  A_2, 48);
        sn(1, r+5, 0, S_PMUTE,  A_2, 48);
        sn(1, r+6, 0, S_PMUTE,  A_2, 48);

        sn(1, r+0, 1, S_GUITAR, E_2, 50);
        sn(1, r+2, 1, S_PMUTE,  E_2, 40);
        sn(1, r+3, 1, S_PMUTE,  E_2, 40);
        sn(1, r+5, 1, S_PMUTE,  E_2, 40);
        sn(1, r+6, 1, S_PMUTE,  E_2, 40);

        sn(1, r+0, 2, S_BASS, A_1, 60);

        sn(1, r+0, 3, S_KICK,  C_2, 64);
        sn(1, r+2, 3, S_HIHAT, C_3, 40);
        sn(1, r+4, 3, S_SNARE, C_2, 56);
        sn(1, r+6, 3, S_HIHAT, C_3, 40);
    }
}

static void build_pattern2(void)
{
    /* Breakdown: slower half-time feel with double kicks */
    int r;
    for (r = 0; r < 64; r += 16) {
        /* Heavy downtuned E hits */
        sn(2, r+0,  0, S_GUITAR, E_1, 64);
        sn(2, r+8,  0, S_GUITAR, E_1, 64);
        sn(2, r+12, 0, S_GUITAR, Fs1, 64);

        sn(2, r+0,  1, S_GUITAR, B_1, 50);
        sn(2, r+8,  1, S_GUITAR, B_1, 50);
        sn(2, r+12, 1, S_GUITAR, Cs2, 50);

        sn(2, r+0, 2, S_BASS, E_1, 64);
        sn(2, r+8, 2, S_BASS, E_1, 64);

        /* Double kick */
        sn(2, r+0,  3, S_KICK, C_2, 64);
        sn(2, r+2,  3, S_KICK, C_2, 58);
        sn(2, r+4,  3, S_SNARE, C_2, 60);
        sn(2, r+6,  3, S_KICK, C_2, 58);
        sn(2, r+8,  3, S_KICK, C_2, 64);
        sn(2, r+10, 3, S_KICK, C_2, 58);
        sn(2, r+12, 3, S_SNARE, C_2, 60);
        sn(2, r+14, 3, S_HIHAT, C_3, 45);
    }
}

static void build_pattern3(void)
{
    /* Fast E-F-E-F chromatic riff */
    int r;
    for (r = 0; r < 64; r += 4) {
        UBYTE note = ((r / 4) & 1) ? F_2 : E_2;
        UBYTE bnote = ((r / 4) & 1) ? C_3 : B_2;
        UBYTE bass = ((r / 4) & 1) ? F_1 : E_1;

        sn(3, r+0, 0, S_PMUTE, note, 56);
        sn(3, r+1, 0, S_PMUTE, note, 48);
        sn(3, r+2, 0, S_PMUTE, note, 56);

        sn(3, r+0, 1, S_PMUTE, bnote, 45);
        sn(3, r+2, 1, S_PMUTE, bnote, 45);

        if ((r & 7) == 0) sn(3, r, 2, S_BASS, bass, 58);

        sn(3, r+0, 3, S_KICK,  C_2, 64);
        sn(3, r+2, 3, S_HIHAT, C_3, 38);
    }
}

static void build_song(void)
{
    /* Clear all patterns */
    int p, r, c;
    for (p = 0; p < NUM_PATTERNS; p++)
        for (r = 0; r < ROWS_PER_PAT; r++)
            for (c = 0; c < NUM_CHANNELS; c++) {
                mp.pat[p][r][c].smp = 0;
                mp.pat[p][r][c].note = N__;
                mp.pat[p][r][c].vol = 0xFF;
            }

    build_pattern0();  /* E5 gallop */
    build_pattern1();  /* G5/A5 variation */
    build_pattern2();  /* Breakdown */
    build_pattern3();  /* Chromatic riff */

    /* Song order: verse-verse-chorus-verse-verse-chorus-breakdown-riff */
    mp.order[0] = 0;
    mp.order[1] = 0;
    mp.order[2] = 1;
    mp.order[3] = 0;
    mp.order[4] = 0;
    mp.order[5] = 1;
    mp.order[6] = 2;
    mp.order[7] = 3;
}


/* ---- YM replay ---- */

static int ym_vol(int vol64, int smp, int age)
{
    int v15 = (vol64 * 15 + 32) / 64;
    switch (smp) {
    case S_PMUTE:  v15 -= age * 2; break;          /* short palm-muted chug */
    case S_KICK:   v15 -= age * 3; break;
    case S_SNARE:  v15 -= age * 2; break;
    case S_HIHAT:  v15 -= age * 5; break;
    default:       v15 -= age / 4; break;          /* looping guitar/bass */
    }
    return v15 < 0 ? 0 : v15;
}

static void process_row(void)
{
    int pat = mp.order[mp.position];
    static const int map[NUM_CHANNELS] = { 0, -1, 1, 2 };
    for (int c = 0; c < NUM_CHANNELS; c++) {
        PNote *n = &mp.pat[pat][mp.row][c];
        int y = map[c];
        if (y < 0)
            continue;
        if (n->smp)
            v[y].smp = n->smp;
        if (n->vol != 0xFF)
            v[y].vol = n->vol;
        if (n->note != N__ && n->note < 36) {
            v[y].note = n->note;
            v[y].age = 0;
        }
    }
}

static void sfx_tick(int *tone, int *noise)
{
    int t = sfx_t++;
    switch (sfx_kind) {
    case SFX_HOP:      ym_tone(2, 180 - t * 25); ym_volume(2, 12 - t * 2); *tone |= 4; break;
    case SFX_SPLAT:    ym_noise(20 + t); ym_volume(2, 15 - t); *noise |= 4; break;
    case SFX_SPLASH:   ym_noise(4 + t / 2); ym_volume(2, 14 - t / 2); *noise |= 4; break;
    case SFX_HOME:     ym_note(2, 72 + (t / 3) * 4); ym_volume(2, 13); *tone |= 4; break;
    case SFX_LEVELUP:  ym_note(2, 72 + (t / 4) * 5); ym_volume(2, 14); *tone |= 4; break;
    case SFX_GAMEOVER: ym_note(2, 64 - (t / 5) * 2); ym_volume(2, 14 - t / 4); *tone |= 4; break;
    }
}

void modplay_tick(void)
{
    int tone = 0, noise = 0;

    if (mp.playing && mp.tick == 0)
        process_row();

    if (mp.playing) {
        for (int y = 0; y < 3; y++) {
            int smp = v[y].smp, vol = ym_vol(v[y].vol, smp, v[y].age);
            if (y == 2 && sfx_frames)
                continue;
            if (y < 2) {
                ym_note(y, 36 + v[y].note);
                tone |= 1 << y;
            } else if (smp == S_KICK) {
                ym_tone(2, 900 + v[y].age * 400);     /* falling thump */
                tone |= 4;
            } else {
                ym_noise(smp == S_HIHAT ? 1 : 8);
                noise |= 4;
            }
            ym_volume(y, vol);
            v[y].age++;
        }
        if (++mp.tick >= mp.speed) {
            mp.tick = 0;
            if (++mp.row >= ROWS_PER_PAT) {
                mp.row = 0;
                if (++mp.position >= SONG_LENGTH)
                    mp.position = 0;
            }
        }
    }
    if (sfx_frames) {
        sfx_tick(&tone, &noise);
        sfx_frames--;
        if (!sfx_frames)
            ym_volume(2, 0);
    }
    ym_mix(tone, noise);
}

int modplay_init(void)
{
    ym_init();
    mp.speed = SPEED_DEFAULT;
    for (int y = 0; y < 3; y++)
        v[y].vol = 0;
    build_song();
    return 0;
}

void modplay_start(void)
{
    mp.playing = 1;
    mp.tick = mp.row = mp.position = 0;
}

void modplay_stop(void)
{
    mp.playing = 0;
    for (int y = 0; y < 3; y++)
        ym_volume(y, 0);
}

void modplay_cleanup(void)
{
    modplay_stop();
    ym_exit();
}

void modplay_toggle(void)
{
    if (mp.playing)
        modplay_stop();
    else
        modplay_start();
}

void st_sfx(int kind, int frames)
{
    sfx_kind = kind;
    sfx_frames = frames;
    sfx_t = 0;
}

/* Amiga API compatibility: sample based SFX aren't used on the ST */
void modplay_sfx(BYTE *data, UWORD len_words, UWORD period, UWORD volume)
{
    (void)data; (void)len_words; (void)period; (void)volume;
}
