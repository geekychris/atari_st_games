// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#include "audio.h"
extern "C" {
#include "bridge_client.h"
}

/*
 * Stub audio. Every trigger goes to the bridge log so the host can
 * see the game's audio timeline in the devbench UI. Real sample
 * playback is a follow-up (see audio.h).
 */

void audio_init(void)     { AB_I("audio: stub init"); }
void audio_shutdown(void) { AB_I("audio: stub shutdown"); }

void sfx_jump(void)       { AB_I("sfx: jump"); }
void sfx_land(void)       { AB_I("sfx: land"); }
void sfx_key(void)        { AB_I("sfx: key"); }
void sfx_death(void)      { AB_I("sfx: death"); }
void sfx_levelwin(void)   { AB_I("sfx: levelwin"); }
