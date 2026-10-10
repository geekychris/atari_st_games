# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
# Rolling Steel - Atari Falcon030 port
#   make CROSS=~/computers/atari-cc/opt/cross-mint/bin/m68k-atari-mintelf-
#   make run
NAME = ROLLSTL
SRCS = main_68k.c game.c phys.c course.c render.c glcels_soft.c softcel.c sound_paula.c \
       fastcel.c
# the frame is the screen, in RGB565 (glcels_soft.c); and upstream warnings
EXTRA_CFLAGS = -DSOFTCEL_RGB565 -Wno-maybe-uninitialized -Wno-unused-const-variable \
               -Wno-misleading-indentation -Wno-format-overflow
include ../falcon_3do/f3do.mk
