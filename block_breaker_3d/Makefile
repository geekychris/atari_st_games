# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
# Block Breaker 3D - Atari Falcon030 port (the amiga3d engine, ../a3d_falcon)
#   make CROSS=~/computers/atari-cc/opt/cross-mint/bin/m68k-atari-mintelf-
#   make run / make test / make host-test
NAME    = block_breaker_3d
PRG     = BBREAK3D
TPRG    = BBTEST
CORE    = bb_core.c
GAME    = game.c render.c tests/test_cases.c
TESTS   = tests/test_cases.c
HEADERS = bb.h breaker.h tests/vectors.h
include ../a3d_falcon/a3d.mk
