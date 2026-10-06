/*
 * ProTracker MOD player with the C interface of Frank Wille's ptplayer
 * (which the Amiga games use from assembly), running on the Paula
 * emulation in paula.c.  Same function names and SfxStructure; the
 * custom base argument is accepted and ignored, and the player is
 * driven from the VBL instead of a CIA timer.
 *
 * Supported: 31 sample M.K. style modules, speed / BPM, effects 0-F and
 * the common E commands (fine slides, pattern loop, retrigger, note
 * cut/delay, pattern delay); sound effects with channel choice and
 * priority as in ptplayer (mt_playfx, mt_soundfx).
 */
#ifndef PTPLAYER_H
#define PTPLAYER_H

#include "amiga_types.h"

typedef struct {
	APTR sfx_ptr;		/* sample start */
	WORD sfx_len;		/* length in words */
	WORD sfx_per;		/* replay period */
	WORD sfx_vol;		/* volume 0..64 */
	BYTE sfx_cha;		/* channel 0..3, or -1 for auto */
	BYTE sfx_pri;		/* priority (non-zero) */
} SfxStructure;

void mt_install_cia(void *custom, void *autovec, UBYTE pal);
void mt_remove_cia(void *custom);
void mt_init(void *custom, APTR module, APTR samples, UBYTE songpos);
void mt_end(void *custom);
void mt_soundfx(void *custom, APTR sample, UWORD length, UWORD period, UWORD volume);
void mt_playfx(void *custom, SfxStructure *sfx);
void mt_musicmask(void *custom, UBYTE mask);
void mt_mastervol(void *custom, UWORD vol);
void mt_music(void *custom);		/* one player tick (normally from the VBL) */

extern UBYTE mt_Enable;			/* 0: music paused */
extern UBYTE mt_E8Trigger;		/* last E8x parameter */
extern UBYTE mt_MusicChannels;		/* channels kept for music, the rest
					 * may be used by sound effects */

/* ST: 0 if the machine has DMA sound and playback started */
int mt_sound_ok(void);

#endif
