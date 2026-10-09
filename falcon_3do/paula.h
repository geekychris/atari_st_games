/*
 * The 3DO-derived ports' Paula API (their paula.h / paula68k.c) on the
 * Paula emulation of the ST layer (../st_port/paula.c, Falcon DMA sound).
 * The sound code writes custom.aud[] and calls paula_dmacon() as on the
 * Amiga; its 50 Hz tick still runs from the main loop (paula_tick), the
 * mixing from the VBL.
 */
#ifndef PAULA_3DO_H
#define PAULA_3DO_H

#include "../st_port/paula.h"
#include "hardware/dmabits.h"

#define PAULA_CLOCK_PAL 3546895L

int   paula_open(void);                 /* 0: no DMA sound */
void  paula_close(void);
void  paula_set_tick(void (*tick)(void));
void  paula_tick(void);                 /* the main loop: once per 50 Hz step */

#endif
