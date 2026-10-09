/*
 * Amiga header shim for the 3DO-derived Falcon ports (planet_chomp,
 * rolling_steel, spectral_keep): the exec / intuition / input event
 * declarations their 68k main loops use.  The window's IDCMP port is
 * fed with RAWKEY messages from the IKBD handler (sys3do.c).
 */
#ifndef AMIGA3DO_COMPAT_H
#define AMIGA3DO_COMPAT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "amiga_types.h"

/* exec memory: no chip RAM, sound DMA reads the mixer's buffer */
#define __chip
#define MEMF_ANY    0
#define MEMF_PUBLIC (1L << 0)
#define MEMF_CHIP   (1L << 1)
#define MEMF_FAST   (1L << 2)
#define MEMF_CLEAR  (1L << 16)
static inline void *AllocMem(ULONG size, ULONG flags)
{
	return (flags & MEMF_CLEAR) ? calloc(1, size) : malloc(size);
}
static inline void FreeMem(void *p, ULONG size) { (void)size; free(p); }
static inline void *AllocVec(ULONG size, ULONG flags) { return AllocMem(size, flags); }
static inline void FreeVec(void *p) { free(p); }
static inline void CopyMem(const void *s, void *d, ULONG n) { memcpy(d, s, n); }

/* messages: only the IDCMP port of the game's window */
struct Message { struct Message *mn_Next; };
struct MsgPort { int mp_Unused; };
struct IntuiMessage {
	struct Message ExecMessage;
	ULONG Class;
	UWORD Code;
	UWORD Qualifier;
};
struct Window { struct MsgPort *UserPort; };
struct Screen { int unused; };
struct ViewPort { int unused; };

#define IDCMP_MOUSEBUTTONS   0x00000008L
#define IDCMP_CLOSEWINDOW    0x00000200L
#define IDCMP_RAWKEY         0x00000400L
#define IDCMP_INACTIVEWINDOW 0x00080000L
#define IECODE_UP_PREFIX     0x80
#define IEQUALIFIER_LSHIFT   0x0001
#define IEQUALIFIER_RSHIFT   0x0002
#define IEQUALIFIER_REPEAT   0x0200

struct Message *GetMsg(struct MsgPort *port);
void ReplyMsg(struct Message *msg);

#define SIGBREAKF_CTRL_C (1L << 12)
static inline ULONG SetSignal(ULONG n, ULONG m) { (void)n; (void)m; return 0; }

void WaitTOF(void);			/* the next VBL */
static inline void Delay(LONG ticks) { while (ticks-- > 0) { WaitTOF(); } }

/* AmigaDOS files (../st_port/amiga_dos.c): volume prefixes dropped,
 * names shortened to GEMDOS 8.3 */
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

/* fonts: OpenFont gives the system's 8x8 font (sys3do.c) as a strike,
 * whatever is asked for (the games ask for topaz 8) */
struct TextAttr { STRPTR ta_Name; UWORD ta_YSize; UBYTE ta_Style, ta_Flags; };
struct TextFont {
	UWORD tf_YSize, tf_XSize, tf_Baseline, tf_Modulo;
	UBYTE tf_LoChar, tf_HiChar;
	APTR tf_CharData, tf_CharLoc;
};
struct TextFont *OpenFont(struct TextAttr *ta);
static inline void CloseFont(struct TextFont *tf) { (void)tf; }

#endif
