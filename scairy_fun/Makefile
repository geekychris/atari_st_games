# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
# ScAIry Fun - Atari Falcon030 port (the amiga3d engine, ../a3d_falcon)
#   make CROSS=~/computers/atari-cc/opt/cross-mint/bin/m68k-atari-mintelf-
#   make run / make test / make host-test
NAME    = scairy_fun
PRG     = SCAIRY
TPRG    = SCTEST
CORE    = track.c world.c
GAME    = game.c render.c models.c tests/test_cases.c
TESTS   = tests/test_cases.c
HEADERS = sc.h render.h game.h sound.h models.h
DATA    = scairy.mod scairy.voc
TRACE   = tests/trace_main.c
include ../a3d_falcon/a3d.mk
