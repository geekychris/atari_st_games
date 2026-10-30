// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#ifndef MINER_METEOR_RENDER_H
#define MINER_METEOR_RENDER_H

#include <exec/types.h>

struct Screen;
struct RastPort;
struct GameState;

/* Open a 320x256 8bpp screen + double-buffered draw target.
 * Returns 0 on success, non-zero on error. */
LONG render_open(void);
void render_close(void);

/* Expose the screen so main.cpp can OpenWindow on top of it for
 * IDCMP_RAWKEY delivery. */
struct Screen *render_get_screen(void);

/* Draw a single frame into the back buffer. Call render_flip() after
 * any of render_frame / render_title / render_endscreen to page the
 * newly-drawn buffer onto the display. */
void render_frame(const GameState &gs);
void render_flip(void);

/* Draw title screen overlay. */
void render_title(const GameState &gs);

/* Draw end-of-game overlay. Uses mode from gs. */
void render_endscreen(const GameState &gs);

/* Simple SetAPen + Text wrapper used by title / end screens. */
void render_text(LONG x, LONG y, UBYTE pen, const char *text);

#endif
