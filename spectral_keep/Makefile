# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
# Spectral Keep - Atari Falcon030 port
#   make CROSS=~/computers/atari-cc/opt/cross-mint/bin/m68k-atari-mintelf-
#   make run
NAME = SPECKEEP
SRCS = main_68k.c room.c world.c levels.c vox.c font.c textures.c scene.c game.c sound_paula.c
# scene.c composes the Falcon's 16 bit pixels straight into the screen
EXTRA_CFLAGS = -DSCENE_RGB565
include ../falcon_3do/f3do.mk
