# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
# Orb Hunter - Atari STE port
#   make CROSS=~/computers/atari-cc/opt/cross-mint/bin/m68k-atari-mintelf-
#   make run
# No music is included; copy a ProTracker module of your own to
# build/MUSIC.MOD to have music.
NAME     = ORBHUNTR
SRCS     = main_st.c game.c draw.c levels.c st_input.c
ST_EXTRA = paula ptplayer amiga_dos ab_log
MACHINE  = ste
include ../st_port/port.mk
