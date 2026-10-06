/*
 * Uranus Lander - Atari ST sound on the YM2149 PSG.
 *
 * The Amiga version plays a ProTracker MOD plus procedurally generated
 * samples through Paula.  A plain ST has no sample playback hardware,
 * so here the effects are synthesised on the PSG (tone + noise +
 * volume envelopes) and the MOD is replaced by a small chiptune loop:
 *
 *   channel A  melody      (land / low-fuel beep take it over)
 *   channel B  bass
 *   channel C  noise: thrust rumble, crash explosion
 *
 * sound_tick() is called once per frame.  Needs supervisor mode.
 */
#include "sound.h"
#include "st_sound.h"

#define YM_SELECT (*(volatile UBYTE *)0xffff8800)
#define YM_WRITE  (*(volatile UBYTE *)0xffff8802)

static UBYTE mixer_io;		/* port direction bits 6-7 of mixer reg */
static UWORD period[128];	/* MIDI note -> YM tone period */

static void ym(UBYTE reg, UBYTE val)
{
	YM_SELECT = reg;
	YM_WRITE = val;
}

static UBYTE ym_read(UBYTE reg)
{
	YM_SELECT = reg;
	return YM_SELECT;
}

/* --- effects state --- */
static WORD thrust_on;
static WORD crash_t;		/* frames left */
static WORD land_t;
static WORD beep_t;
static WORD music_on = 1;
static UWORD rng = 1234;

/* --- music: 32 steps, 7 frames each; MIDI notes, 0 = rest --- */
#define STEP_FRAMES 7
static const UBYTE bass[32] = {
	45, 0, 45, 0, 45, 0, 52, 0,  41, 0, 41, 0, 41, 0, 48, 0,
	43, 0, 43, 0, 43, 0, 50, 0,  40, 0, 40, 0, 44, 0, 47, 0,
};
static const UBYTE lead[32] = {
	69, 72, 76, 0, 81, 0, 79, 76,  77, 0, 72, 0, 69, 0, 72, 0,
	71, 74, 79, 0, 83, 0, 81, 79,  76, 0, 0, 0, 68, 71, 76, 0,
};
static WORD music_step, music_frame;

static void tone(int ch, UWORD per)
{
	ym(ch * 2, per & 0xff);
	ym(ch * 2 + 1, (per >> 8) & 0x0f);
}

void sound_init(void)
{
	/* equal temperament from A4 = 440 Hz; YM clock 2 MHz / 16 */
	float f = 440.0f;
	for (int n = 69; n < 128; n++, f *= 1.0594631f)
		period[n] = (UWORD)(125000.0f / f + 0.5f);
	f = 440.0f;
	for (int n = 69; n >= 0; n--, f /= 1.0594631f)
		period[n] = f > 31.0f ? (UWORD)(125000.0f / f + 0.5f) : 4095;

	mixer_io = ym_read(7) & 0xc0;
	for (int r = 8; r <= 10; r++)
		ym(r, 0);
	ym(7, mixer_io | 0x3f);
}

void sound_cleanup(void)
{
	for (int r = 8; r <= 10; r++)
		ym(r, 0);
	ym(7, mixer_io | 0x3f);
}

void sfx_thrust_play(void) { thrust_on = 1; }
void sfx_thrust_stop(void) { thrust_on = 0; }
void sfx_crash_play(void)  { crash_t = 90; thrust_on = 0; }
void sfx_land_play(void)   { land_t = 32; }
void sfx_beep_play(void)   { if (!land_t) beep_t = 6; }

void music_enable(int on)
{
	music_on = on;
	music_step = music_frame = 0;
}

int music_enabled(void) { return music_on; }

void sound_tick(void)
{
	UBYTE mix = 0x3f;		/* all off; clear bits to enable */
	UBYTE vol_a = 0, vol_b = 0, vol_c = 0;

	/* music (A lead, B bass) */
	if (music_on)
	{
		int s = music_step;
		int decay = music_frame;
		if (lead[s])
		{
			tone(0, period[lead[s]]);
			vol_a = 11 - (decay > 6 ? 6 : decay);
			mix &= ~0x01;
		}
		if (bass[s])
		{
			tone(1, period[bass[s]]);
			vol_b = 13 - (decay > 4 ? 4 : decay);
			mix &= ~0x02;
		}
		if (++music_frame >= STEP_FRAMES)
		{
			music_frame = 0;
			music_step = (music_step + 1) & 31;
		}
	}

	/* channel A effects override the melody */
	if (land_t)
	{
		static const UBYTE notes[4] = { 72, 76, 79, 84 };	/* C E G C */
		tone(0, period[notes[(32 - land_t) / 8]]);
		vol_a = 14;
		mix &= ~0x01;
		land_t--;
	}
	else if (beep_t)
	{
		tone(0, period[93]);	/* A6 */
		vol_a = beep_t > 2 ? 13 : 0;
		mix &= ~0x01;
		beep_t--;
	}

	/* channel C: noise */
	if (crash_t)
	{
		ym(6, 31 - (crash_t / 4 > 27 ? 27 : crash_t / 4));	/* deeper as it fades */
		vol_c = crash_t > 60 ? 15 : crash_t / 4;
		mix &= ~0x20;
		crash_t--;
	}
	else if (thrust_on)
	{
		rng = rng * 25173 + 13849;
		ym(6, 24 + ((rng >> 8) & 7));	/* low, rumbling, slightly varying */
		vol_c = 11 + ((rng >> 12) & 3);
		mix &= ~0x20;
	}

	ym(8, vol_a);
	ym(9, vol_b);
	ym(10, vol_c);
	ym(7, mixer_io | mix);
}
