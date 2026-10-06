/*
 * Nova Defense - Atari ST sound on the YM2149 (replaces the Amiga
 * sound.c, which plays Paula waveforms).  Same events and the same
 * pitches: Paula period P with the original 32-sample waveforms is
 * 3546895 / (32 P) Hz, i.e. a YM tone period of P * 1.128.
 *
 *   channel A  shoot / UFO warble
 *   channel B  march (4 note bass)       } melody shares B between
 *   channel C  explosions (noise)        } march notes
 */
#include "sound.h"
#include "st_ym.h"

#define PAULA_TO_YM(p) ((UWORD)(((ULONG)(p) * 1128) / 1000))

static const UWORD march_periods[4] = { 846, 950, 1066, 1130 };
static const UWORD melody_periods[16] = {
	504, 423, 336, 423, 504, 566, 504, 423,
	377, 336, 283, 336, 377, 423, 504, 566
};
#define MELODY_SPEED 12

static WORD shoot_t, alien_t, player_t, march_t, melody_t, melody_pos;
static BOOL ufo_on;
static WORD ufo_phase;
static UWORD march_period;

int sound_init(void)
{
	ym_init();
	return 0;
}

void sound_cleanup(void)
{
	ym_exit();
}

void sound_play_shoot(void)          { shoot_t = 8; }
void sound_play_alien_explode(void)  { if (player_t == 0) alien_t = 14; }
void sound_play_player_explode(void) { player_t = 60; }
void sound_play_ufo(BOOL on)         { ufo_on = on; }

void sound_play_march(WORD note)
{
	march_period = PAULA_TO_YM(march_periods[note & 3]);
	march_t = 6;
}

void sound_update(void)
{
	int tone = 0, noise = 0;

	/* A: shoot (falling sweep) or UFO warble */
	if (shoot_t)
	{
		ym_tone(0, 60 + (8 - shoot_t) * 25);
		ym_volume(0, 10 + shoot_t / 2);
		tone |= 1;
		shoot_t--;
	}
	else if (ufo_on)
	{
		ufo_phase = (ufo_phase + 1) & 15;
		ym_tone(0, 90 + (ufo_phase < 8 ? ufo_phase : 16 - ufo_phase) * 6);
		ym_volume(0, 9);
		tone |= 1;
	}
	else
		ym_volume(0, 0);

	/* B: march step, else quiet background melody */
	if (march_t)
	{
		ym_tone(1, march_period);
		ym_volume(1, 9 + march_t / 2);
		tone |= 2;
		march_t--;
	}
	else
	{
		if (++melody_t >= MELODY_SPEED)
		{
			melody_t = 0;
			melody_pos = (melody_pos + 1) & 15;
		}
		ym_tone(1, PAULA_TO_YM(melody_periods[melody_pos]));
		ym_volume(1, melody_t < 6 ? 5 : 3);
		tone |= 2;
	}

	/* C: explosions */
	if (player_t)
	{
		ym_noise(10 + (60 - player_t) / 4);
		ym_volume(2, player_t > 30 ? 15 : player_t / 2);
		noise |= 4;
		player_t--;
	}
	else if (alien_t)
	{
		ym_noise(6 + (14 - alien_t) / 2);
		ym_volume(2, alien_t);
		noise |= 4;
		alien_t--;
	}
	else
		ym_volume(2, 0);

	ym_mix(tone, noise);
}
