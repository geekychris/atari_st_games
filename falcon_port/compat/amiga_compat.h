/*
 * Amiga header shim for the Falcon ports: the few exec / intuition /
 * graphics declarations Amiga game code refers to, mapped to the
 * Falcon layer (fgfx) or stubbed.
 */
#ifndef AMIGA_COMPAT_H
#define AMIGA_COMPAT_H

#include <stdlib.h>
#include <string.h>
#include "amiga_types.h"
#include "fgfx.h"

/* exec memory */
#define MEMF_ANY    0
#define MEMF_CHIP   (1 << 1)
#define MEMF_FAST   (1 << 2)
#define MEMF_CLEAR  (1 << 16)
static inline void *AllocMem(ULONG size, ULONG flags)
{
	return (flags & MEMF_CLEAR) ? calloc(1, size) : malloc(size);
}
static inline void FreeMem(void *p, ULONG size) { (void)size; free(p); }
static inline void CopyMem(const void *s, void *d, ULONG n) { memcpy(d, s, n); }

/* graphics/intuition objects only referenced through pointers */
struct ViewPort { int unused; };
struct Screen { struct ViewPort ViewPort; };
struct Window;
struct ScreenBuffer;
struct MsgPort;
struct BitMap;

static inline void WaitBlit(void) { }
static inline void WaitTOF(void) { }

#endif
