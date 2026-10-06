// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * SKY KNIGHTS - Draw function declarations
 */
#ifndef DRAW_H
#define DRAW_H

#include <graphics/rastport.h>
#include "game.h"

void draw_title(struct RastPort *rp, GameState *gs);
void draw_playing(struct RastPort *rp, GameState *gs);
void draw_wave_intro(struct RastPort *rp, GameState *gs);
void draw_gameover(struct RastPort *rp, GameState *gs);
void draw_text(struct RastPort *rp, WORD x, WORD y, const char *str, WORD color);

#ifdef __MINT__
/* ST port: the screen in layers, see draw.c */
void draw_st_scenery(struct RastPort *rp, GameState *gs, int playing);
void draw_st_wave_text(struct RastPort *rp, GameState *gs);
void draw_st_hud(struct RastPort *rp, GameState *gs);
void draw_st_actors(struct RastPort *rp, GameState *gs);
#endif

#endif
