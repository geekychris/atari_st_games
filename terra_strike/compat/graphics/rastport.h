// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/* graphics/rastport.h for Terra Strike on the Falcon: render.h's Canvas
 * (a RastPort on the classic Amiga) is a Falcon screen buffer here, as
 * ../a3d_falcon draws it: a base and the offsets of the game's 256 rows
 * (-1 for the rows the 240-line screen drops) */
#ifndef TS_FALCON_RASTPORT_H
#define TS_FALCON_RASTPORT_H
#include <stdint.h>
#include "amiga_types.h"
struct RastPort { uint8_t *base; const int32_t *rowoff; int h; };
#endif
