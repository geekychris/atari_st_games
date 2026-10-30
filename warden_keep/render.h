// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#ifndef WARDEN_KEEP_RENDER_H
#define WARDEN_KEEP_RENDER_H

#include <exec/types.h>

struct Screen;
struct GameState;

LONG           render_open(void);
void           render_close(void);
struct Screen *render_get_screen(void);

void render_frame(const GameState &gs);
void render_title(const GameState &gs);
void render_endscreen(const GameState &gs);
void render_flip(void);

void render_text(LONG x, LONG y, UBYTE pen, const char *text);

#endif
