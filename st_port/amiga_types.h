/*
 * Amiga exec/types.h replacement for the Atari ST port.
 */
#ifndef AMIGA_TYPES_H
#define AMIGA_TYPES_H

typedef long            LONG;
typedef unsigned long   ULONG;
typedef short           WORD;
typedef unsigned short  UWORD;
typedef signed char     BYTE;
typedef unsigned char   UBYTE;
typedef short           BOOL;
typedef void           *APTR;

#ifndef TRUE
#define TRUE  1
#define FALSE 0
#endif

#endif
