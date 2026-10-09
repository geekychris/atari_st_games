/*
 * Paula, for the classic 68k builds: the real chip. The 3DO versions drove
 * an emulation of it with the same register names (custom.aud[n].ac_ptr,
 * ac_len, ac_per, ac_vol; paula_dmacon), so their sound code runs as it is.
 * Samples must be in chip RAM (sys_load(name, &len, 1)).
 *
 * paula_open() claims all four channels from audio.device. The 3DO's 50 Hz
 * audio tick is called by the main loop once per logic step (paula_tick).
 * The same file is in planet_chomp, rolling_steel and spectral_keep.
 */
#ifndef PAULA_H
#define PAULA_H

#include <exec/types.h>
#include <hardware/custom.h>
#include <hardware/dmabits.h>

/* the chip registers. Not amiga.lib's `custom`: linking amiga.lib would
 * also bring in its sprintf, whose %d reads a 16-bit WORD (the 3DO code
 * prints ints with %d) */
#define custom (*(volatile struct Custom *)0xDFF000)

#define PAULA_CLOCK_PAL 3546895L

int   paula_open(void);                 /* 0: no channels (another program has them) */
void  paula_close(void);
void  paula_dmacon(UWORD v);
void  paula_set_tick(void (*tick)(void));
void  paula_tick(void);                 /* the main loop: once per 50 Hz step */
UWORD paula_lock(void);                 /* nothing to lock: the tick runs in the main loop */
void  paula_unlock(UWORD sr);

#endif
