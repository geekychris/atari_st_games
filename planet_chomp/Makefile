# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
# Planet Chomp - Atari Falcon030 port
#   make CROSS=~/computers/atari-cc/opt/cross-mint/bin/m68k-atari-mintelf-
#   make run
NAME = PCHOMP
SRCS = main_68k.c game.c maze.c sprites.c render_cel.c cels_soft.c softcel.c sfx_paula.c
# upstream warnings
EXTRA_CFLAGS = -Wno-maybe-uninitialized -Wno-unused-const-variable \
               -Wno-misleading-indentation -Wno-format-overflow
include ../falcon_3do/f3do.mk
