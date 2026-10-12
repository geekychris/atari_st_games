# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
# RJ's 70th Birthday Bash - Atari STE port
#   make CROSS=~/computers/atari-cc/opt/cross-mint/bin/m68k-atari-mintelf-
#   make run
# The music (party.mod, birthday.mod) and the arcade voice clips
# (snd_arcade1..4.raw) are made by tools/modgen in geekychris/3do-dev:
# original tunes, "Happy Birthday to You" (public domain) and synthetic
# speech
NAME     = RJBDAY
SRCS     = main_st.c game.c draw.c rooms.c sound.c st_input.c
ST_EXTRA = paula ptplayer amiga_dos ab_log st_blit
MACHINE  = ste
# the house strip (192 KB) and both MODs need more than 1 MB
MEMSIZE  = 2
# game.h uses struct Window without including intuition.h (the Amiga
# sources always had it first); upstream: one unused variable
EXTRA_CFLAGS = -include intuition/intuition.h -Wno-unused-variable
include ../st_port/port.mk

# the guest list, as game.c loads it (DH2:Dev/guests.txt -> GUESTS.TXT)
all: $(OUT)/GUESTS.TXT
run: $(OUT)/GUESTS.TXT
$(OUT)/GUESTS.TXT: guests.txt | $(OUT)
	cp $< $@

# the music and voice clips, under the 8.3 names the AmigaDOS shim maps
# them to (sound.c asks for snd_arc1..4.raw: snd_arcade1..4 would all
# shorten to SND_ARCA.RAW)
ASSETS = $(OUT)/PARTY.MOD $(OUT)/BIRTHDAY.MOD $(OUT)/SND_ARC1.RAW $(OUT)/SND_ARC2.RAW \
         $(OUT)/SND_ARC3.RAW $(OUT)/SND_ARC4.RAW
all: $(ASSETS)
run: $(ASSETS)
$(OUT)/PARTY.MOD: party.mod | $(OUT)
	cp $< $@
$(OUT)/BIRTHDAY.MOD: birthday.mod | $(OUT)
	cp $< $@
$(OUT)/SND_ARC%.RAW: snd_arcade%.raw | $(OUT)
	cp $< $@
