// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#include "audio.h"
extern "C" {
#include "bridge_client.h"
}

void audio_init(void)     { AB_I("audio: stub init"); }
void audio_shutdown(void) { AB_I("audio: stub shutdown"); }
void sfx_step(void)       { AB_I("sfx: step"); }
void sfx_rune(void)       { AB_I("sfx: rune"); }
void sfx_oil(void)        { AB_I("sfx: oil"); }
void sfx_death(void)      { AB_I("sfx: death"); }
void sfx_levelwin(void)   { AB_I("sfx: levelwin"); }
