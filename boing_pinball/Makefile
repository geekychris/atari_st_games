# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
# Boing Pinball - Atari Falcon030 port (the amiga3d engine, ../a3d_falcon)
#   make CROSS=~/computers/atari-cc/opt/cross-mint/bin/m68k-atari-mintelf-
#   make run / make test / make host-test
NAME    = boing_pinball
PRG     = BOINGPIN
TPRG    = PBTEST
CORE    = pb_core.c pb_tables.c
GAME    = game.c render.c tests/test_cases.c
TESTS   = tests/test_cases.c
HEADERS = pb.h pinball.h tests/vectors.h
DATA    = boing_pinball.mod
include ../a3d_falcon/a3d.mk
