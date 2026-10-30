// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/* proto/exec.h for Terra Strike on the Falcon: AllocMem / FreeMem from the C
 * library (the Paula emulation mixes from any memory, so no chip RAM) */
#ifndef TS_FALCON_PROTO_EXEC_H
#define TS_FALCON_PROTO_EXEC_H
#include <stdlib.h>
#include "exec/memory.h"
static inline void *AllocMem(ULONG size, ULONG flags) { (void)flags; return calloc(1, size); }
static inline void FreeMem(void *p, ULONG size) { (void)size; free(p); }
#endif
