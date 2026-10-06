/*
 * Amiga Paula (4 channel sample playback) on the Atari Falcon's DMA sound.
 *
 * Amiga code that drives Paula through custom.dmacon / custom.aud[] works
 * unchanged when compiled as C++ (custom.dmacon is an object whose
 * assignment applies Paula's set/clear semantics).  Like Paula, enabling
 * a channel latches ac_ptr/ac_len and starts at the sample's beginning;
 * at the end of the block the (possibly updated) ac_ptr/ac_len are
 * reloaded, which is how one-shot vs. looping samples work.  ac_per and
 * ac_vol take effect immediately.
 *
 * A VBL handler mixes the channels Amiga style (0+3 left, 1+2 right) into
 * a ring buffer that the Falcon plays at 9834 Hz, 8 bit stereo, and can
 * also call the player's tick routine at its design rate (50 Hz).
 * Needs supervisor mode.
 */
#ifndef FPAULA_H
#define FPAULA_H

#include "amiga_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void paula_dmacon(UWORD v);

/* starts DMA sound and the VBL handler; tick (may be NULL) is called
 * tick_hz times per second from the VBL interrupt.  0 = ok */
int  paula_init(void (*tick)(void), int tick_hz);
void paula_exit(void);

/* bracket main program calls into code the tick routine also runs */
UWORD paula_lock(void);
void paula_unlock(UWORD sr);

#ifdef __cplusplus
}

struct AudChannel {
	UWORD *ac_ptr;
	UWORD ac_len;		/* words */
	UWORD ac_per;		/* Paula clock / sample rate */
	UWORD ac_vol;		/* 0-64 */
	UWORD ac_dat;
	UWORD ac_pad[2];
};

struct PaulaDmacon {
	PaulaDmacon &operator=(UWORD v) { paula_dmacon(v); return *this; }
};

struct Custom {
	PaulaDmacon dmacon;
	AudChannel aud[4];
};

extern struct Custom custom;
#endif

#endif
