/*
 * YM2149 PSG helpers for game ports (supervisor mode).
 *
 * Channels: tone periods via ym_tone()/ym_note(), shared noise generator,
 * per channel volume 0-15.  ym_mix() sets which channels use tone/noise.
 */
#ifndef ST_YM_H
#define ST_YM_H

#include "amiga_types.h"

void ym_init(void);		/* silence, remember port direction bits */
void ym_exit(void);		/* silence */
void ym_write(UBYTE reg, UBYTE val);
void ym_tone(int ch, UWORD period);	/* ch 0-2 = A-C */
void ym_note(int ch, int midi_note);	/* equal temperament, A4 = 69 */
void ym_noise(UBYTE period);		/* 0-31 */
void ym_volume(int ch, int vol);	/* 0-15 */
/* tone_mask / noise_mask: bit 0 = A, 1 = B, 2 = C enabled */
void ym_mix(int tone_mask, int noise_mask);
UWORD ym_period(int midi_note);

#endif
