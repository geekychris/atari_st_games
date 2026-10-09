/* Amiga header shim for the ST ports: DMA control bits (Paula emulation) */
#include "amiga_types.h"
#ifndef DMAF_SETCLR
#define DMAF_SETCLR  0x8000
#define DMAF_AUDIO   0x000f
#define DMAF_AUD0    0x0001
#define DMAF_AUD1    0x0002
#define DMAF_AUD2    0x0004
#define DMAF_AUD3    0x0008
#define DMAF_MASTER  0x0200
#endif
