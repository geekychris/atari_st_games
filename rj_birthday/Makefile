# RJ's 70th Birthday Bash - Atari STE port
#   make CROSS=~/computers/atari-cc/opt/cross-mint/bin/m68k-atari-mintelf-
#   make run
# The original's music (party.mod, birthday.mod) and arcade voice clips
# are not included; for personal use, copy them from an amiga_games
# checkout:  make assets UPSTREAM=path/to/amiga_games
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

assets: | $(OUT)
	@test -n "$(UPSTREAM)" || { echo "usage: make assets UPSTREAM=path/to/amiga_games"; exit 1; }
	cp $(UPSTREAM)/rj_birthday/party.mod $(OUT)/PARTY.MOD
	cp $(UPSTREAM)/rj_birthday/birthday.mod $(OUT)/BIRTHDAY.MOD
	for i in 1 2 3 4; do cp $(UPSTREAM)/rj_birthday/snd_arcade$$i.raw $(OUT)/SND_ARC$$i.RAW; done
.PHONY: assets
