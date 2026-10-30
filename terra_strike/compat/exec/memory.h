// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/* exec/memory.h for Terra Strike on the Falcon: the memory flags sound.c uses */
#ifndef TS_FALCON_EXEC_MEMORY_H
#define TS_FALCON_EXEC_MEMORY_H
#include "amiga_types.h"
#define MEMF_ANY   0
#define MEMF_CHIP  (1L << 1)
#define MEMF_FAST  (1L << 2)
#define MEMF_CLEAR (1L << 16)
#endif
