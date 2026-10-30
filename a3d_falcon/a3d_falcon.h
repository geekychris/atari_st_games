// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/* a3d_falcon.h - what the Falcon platform's files share (not for games) */
#ifndef A3D_FALCON_H
#define A3D_FALCON_H

#include <stdint.h>
#include "a3d.h"

/* the sky gradient (plat_sky): the pen it replaces, over rows 0..rows-1,
 * and the gradient pen (32-47) on each row */
extern int a3f_sky_pen, a3f_sky_rows;
extern uint8_t a3f_sky_col[SCREEN_H];
void a3f_draw_pens(void);

/* an AmigaDOS name ("PROGDIR:stack_tower.mod") as the GEMDOS 8.3 name the
 * port's Makefile copies the file to, in the program's DATA folder
 * ("DATA\\STACK_TO.MOD"); out holds 18 */
void a3f_path(const char *amiga, char *out);

#endif
