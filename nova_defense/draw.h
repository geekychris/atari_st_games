// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#ifndef DRAW_H
#define DRAW_H

#include "st_gfx.h"         /* ST port: was <graphics/rastport.h> */
#include "game.h"

void draw_game_hud(struct RastPort *rp, GameState *gs);      /* ST port */
void draw_game_sprites(struct RastPort *rp, GameState *gs);  /* ST port */
void draw_title(struct RastPort *rp, GameState *gs);
void draw_gameover(struct RastPort *rp, GameState *gs);
void draw_text_scaled(struct RastPort *rp, WORD x, WORD y, UBYTE color,
                      const char *text, WORD scale);

#endif
