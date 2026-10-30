# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
# Lander 3D - Atari Falcon030 port (the amiga3d engine, ../a3d_falcon)
#   make CROSS=~/computers/atari-cc/opt/cross-mint/bin/m68k-atari-mintelf-
#   make run / make test / make host-test
NAME    = lander_3d
PRG     = LANDER3D
TPRG    = LDTEST
CORE    = ld_core.c
GAME    = game.c render.c tests/test_cases.c
TESTS   = tests/test_cases.c
HEADERS = ld.h lander.h
DATA    = lander_3d.mod
TRACE   = tests/trace_main.c
include ../a3d_falcon/a3d.mk
