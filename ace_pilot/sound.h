// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * sound.h - Sound effects for Ace Pilot
 */
#ifndef SOUND_H
#define SOUND_H

#include <exec/types.h>
#include "ptplayer.h"

#ifdef __MINT__
/* ST port: ptplayer on the Paula emulation (../st_port/paula.h) */
#include "paula.h"
#define CUSTOM_BASE ((void *)&custom)
#else
#define CUSTOM_BASE ((void *)0xdff000)
#endif

void sound_init(void);
void sound_cleanup(void);

void sfx_gunfire(void);
void sfx_explosion(void);
void sfx_hit(void);
void sfx_die(void);

#endif
