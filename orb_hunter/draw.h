// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * Orb Hunter - Drawing function declarations
 */
#ifndef DRAW_H
#define DRAW_H

#include <graphics/rastport.h>
#include "game.h"

void draw_frame(struct RastPort *rp, GameState *gs);
void draw_title(struct RastPort *rp);
void draw_hud(struct RastPort *rp, GameState *gs);
void draw_item_get(struct RastPort *rp, GameState *gs);
void draw_gameover(struct RastPort *rp);
void draw_text(struct RastPort *rp, WORD x, WORD y, const char *str, WORD color);

#ifdef __MINT__
/* ST port: the screen in layers, see draw.c */
int  draw_st_room_changed(GameState *gs);
void draw_st_room(struct RastPort *rp, GameState *gs);
void draw_st_actors(struct RastPort *rp, GameState *gs);
void draw_st_advance(WORD frames);
#endif

#endif
