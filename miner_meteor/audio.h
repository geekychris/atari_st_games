// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#ifndef MINER_METEOR_AUDIO_H
#define MINER_METEOR_AUDIO_H

/*
 * Audio interface — deliberately kept behind a stub for this first
 * cut. Real Paula 4-channel MOD tune + chip SFX (jump / land / key /
 * death) is a follow-up:
 *
 *   - Classic 68k: link modplay + sfx a la void_trader, wire up
 *     Amiga.lib CIA interrupt-based mixer.
 *   - PPC OS4:     AHI-based mixer, since sam460ex has no Paula.
 *
 * See CLAUDE.md's "PPC OS4" notes for the reason we don't touch chip
 * registers directly.
 */

void audio_init(void);
void audio_shutdown(void);

/* Non-blocking SFX triggers. In the stub they log to the bridge only. */
void sfx_jump(void);
void sfx_land(void);
void sfx_key(void);
void sfx_death(void);
void sfx_levelwin(void);

#endif
