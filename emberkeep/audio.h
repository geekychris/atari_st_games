// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#ifndef EMBERKEEP_AUDIO_H
#define EMBERKEEP_AUDIO_H

/* Stub audio interface — real Paula MOD + chip SFX is a follow-up.
 * See miner_meteor/audio.h for the pattern. */

void audio_init(void);
void audio_shutdown(void);

void sfx_step(void);
void sfx_rune(void);
void sfx_oil(void);
void sfx_death(void);
void sfx_levelwin(void);

#endif
