# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
# Stack Tower - Atari Falcon030 port (the amiga3d engine, ../a3d_falcon)
#   make CROSS=~/computers/atari-cc/opt/cross-mint/bin/m68k-atari-mintelf-
#   make run / make test / make host-test
NAME    = stack_tower
PRG     = STACKTWR
TPRG    = STTEST
CORE    = st_core.c
GAME    = game.c render.c tests/test_cases.c
TESTS   = tests/test_cases.c
HEADERS = st.h stack.h tests/vectors.h
DATA    = stack_tower.mod
include ../a3d_falcon/a3d.mk
