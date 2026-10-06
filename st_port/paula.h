/*
 * Amiga Paula (4 channel sample playback) on Atari DMA sound: STE/TT
 * (8 bit stereo DMA) and Falcon030.
 *
 * `custom` mirrors the Amiga custom chip registers that matter for sound
 * at their real offsets (dmacon at $96, aud[0..3] at $a0..$d0), so code
 * that pokes `custom.aud[n]` or computes register addresses from the
 * custom base works against it.  Writes to dmacon must reach the
 * emulation: from C call paula_dmacon(); from C++ `custom.dmacon = v`
 * works unchanged (an object whose assignment calls paula_dmacon).
 *
 * Like Paula, enabling a channel latches ac_ptr/ac_len and starts at the
 * sample's beginning; at the end of the block the (possibly updated)
 * ac_ptr/ac_len are reloaded, which is how one-shot and looping samples
 * work.  ac_per and ac_vol take effect immediately.
 *
 * A VBL handler mixes the channels Amiga style (0+3 left, 1+2 right)
 * into a ring buffer the DMA sound plays, and can call a player tick
 * routine at its own rate (e.g. 50 Hz, or ProTracker BPM * 2 / 5).
 * Needs supervisor mode.
 */
#ifndef PAULA_H
#define PAULA_H

#include "amiga_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void paula_dmacon(UWORD v);

/* Output rate wish in Hz, before paula_init (default 12517 on the STE,
 * 9834 on the Falcon); the nearest rate the machine has is used.  On a
 * 8 MHz STE each playing channel costs ~4% CPU per 6 kHz. */
void paula_set_rate(int hz);

/* Starts DMA sound and the VBL handler.  tick (may be NULL) is called
 * from the VBL interrupt tick_hz times a second.  Returns 0 if ok, 1 if
 * the machine has no DMA sound (plain ST) or memory is short. */
int  paula_init(void (*tick)(void), int tick_hz);
void paula_exit(void);

/* change the tick rate: num/den calls per second */
void paula_set_tick_rate(int num, int den);

/* bracket main program code that touches state the tick also uses */
UWORD paula_lock(void);
void paula_unlock(UWORD sr);

struct AudChannel {
	UWORD *ac_ptr;		/* sample start */
	UWORD ac_len;		/* length in words */
	UWORD ac_per;		/* period: Paula clock / sample rate */
	UWORD ac_vol;		/* 0-64 */
	UWORD ac_dat;
	UWORD ac_pad[2];
};

#ifdef __cplusplus
}
struct PaulaDmacon {
	UWORD v;
	PaulaDmacon &operator=(UWORD x) { paula_dmacon(x); return *this; }
};
#define PAULA_DMACON_T PaulaDmacon
extern "C" {
#else
#define PAULA_DMACON_T UWORD
#endif

struct Custom {
	UWORD pad0[0x96 / 2];
	PAULA_DMACON_T dmacon;		/* $96 */
	UWORD pad1[(0xa0 - 0x98) / 2];
	struct AudChannel aud[4];	/* $a0, $b0, $c0, $d0 */
};

extern struct Custom custom;

#ifdef __cplusplus
}
#endif

#endif
