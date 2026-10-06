# Uranus Lander - Atari ST port (m68k-atari-mintelf GCC)
#
#   make CROSS=~/computers/atari-cc/opt/cross-mint/bin/m68k-atari-mintelf-
#   make run      # Hatari (ST, PAL 50 Hz EmuTOS like the Amiga original) + agent API,
#                 # build/ as C:, autostart

CROSS  ?= m68k-atari-mintelf-
CC      = $(CROSS)gcc
CFLAGS  = -m68000 -O2 -fomit-frame-pointer -Wall -Wno-unused-function -I. -I../gemdemo
LDFLAGS = -s
OUT     = build
PRG     = $(OUT)/URANUS.PRG
SRCS    = main_st.c game.c draw.c st_gfx.c st_input.c st_sound.c ../gemdemo/natfeats.c
HDRS    = $(wildcard *.h)

all: $(PRG)

$(PRG): $(SRCS) $(HDRS) | $(OUT)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRCS)
	@ls -l $@

$(OUT):
	mkdir -p $(OUT)

run: $(PRG)
	AGENT_TOS=$${AGENT_TOS:-$(CURDIR)/../../roms/emutos/emutos-192k-1.4/etos192uk.img} \
	../../tools/agent/hatari-agent-run.sh --machine st --natfeats on \
		--harddrive $(CURDIR)/$(OUT) --auto 'C:\URANUS.PRG'

clean:
	rm -rf $(OUT)

.PHONY: all run clean
