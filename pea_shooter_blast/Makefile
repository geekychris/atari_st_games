# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
# Pea Shooter Blast - Atari STE port
#   make CROSS=~/computers/atari-cc/opt/cross-mint/bin/m68k-atari-mintelf-
#   make run
# music.mod ("Pea Patrol", an original tune) is made by
# ../tools/modgen/pea_shooter_blast.py and copied into build/ as MUSIC.MOD
NAME     = PEASHOOT
SRCS     = main_st.c game.c draw.c levels.c st_input.c
ST_EXTRA = paula ptplayer amiga_dos ab_log st_blit
MACHINE  = ste
include ../st_port/port.mk

# the MOD, under the 8.3 name the AmigaDOS shim maps it to
MOD = $(OUT)/MUSIC.MOD
all: $(MOD)
run: $(MOD)
$(MOD): music.mod | $(OUT)
	cp $< $@
