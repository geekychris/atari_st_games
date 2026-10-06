/*
 * Amiga exec/dos declarations for the ST ports: memory allocation on
 * malloc, AmigaDOS files on GEMDOS (amiga_dos.c), opaque Intuition types.
 */
#ifndef AMIGA_COMPAT_H
#define AMIGA_COMPAT_H

#include <stdlib.h>
#include <string.h>
#include "amiga_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* exec memory: no chip RAM on the ST, sound DMA reads the mix buffer */
#define MEMF_ANY    0
#define MEMF_PUBLIC (1 << 0)
#define MEMF_CHIP   (1 << 1)
#define MEMF_FAST   (1 << 2)
#define MEMF_CLEAR  (1 << 16)
static inline void *AllocMem(ULONG size, ULONG flags)
{
	return (flags & MEMF_CLEAR) ? calloc(1, size) : malloc(size);
}
static inline void FreeMem(void *p, ULONG size) { (void)size; free(p); }
static inline void *AllocVec(ULONG size, ULONG flags) { return AllocMem(size, flags); }
static inline void FreeVec(void *p) { free(p); }
static inline void CopyMem(const void *s, void *d, ULONG n) { memcpy(d, s, n); }
static inline void Forbid(void) { }
static inline void Permit(void) { }

/* AmigaDOS files (amiga_dos.c).  "PROGDIR:" and other volume prefixes
 * are dropped and names are shortened to GEMDOS 8.3. */
typedef long BPTR;
#define MODE_OLDFILE   1005
#define MODE_NEWFILE   1006
#define MODE_READWRITE 1004
#define OFFSET_BEGINNING (-1)
#define OFFSET_CURRENT   0
#define OFFSET_END       1
BPTR Open(CONST_STRPTR name, LONG mode);
LONG Close(BPTR fh);
LONG Read(BPTR fh, APTR buf, LONG len);
LONG Write(BPTR fh, const void *buf, LONG len);
LONG Seek(BPTR fh, LONG pos, LONG mode);
void Delay(LONG ticks);			/* 1/50 s */

/* Intuition / graphics objects only referenced through pointers */
struct Screen;
struct Window;
struct ScreenBuffer;
struct MsgPort;
struct ViewPort;
struct BitMap;
struct GfxBase;
struct IntuitionBase;

#ifdef __cplusplus
}
#endif

#endif
