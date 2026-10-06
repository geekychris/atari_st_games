// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#ifndef INPUT_H
#define INPUT_H

#include "game.h"

/* ST port: IKBD based, no IntuiMessages */
void input_init(void);
void input_exit(void);
void input_read(InputState *input);

#endif
