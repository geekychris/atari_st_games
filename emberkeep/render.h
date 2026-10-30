// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#ifndef EMBERKEEP_RENDER_H
#define EMBERKEEP_RENDER_H

#include <exec/types.h>

struct Screen;
struct GameState;

LONG           render_open(void);
void           render_close(void);
struct Screen *render_get_screen(void);

void render_frame(const GameState &gs);
void render_title(const GameState &gs);
void render_endscreen(const GameState &gs);

/* Page the freshly-drawn back buffer onto the display. Call after
 * any render_* to swap. */
void render_flip(void);

void render_text(LONG x, LONG y, UBYTE pen, const char *text);

#endif
