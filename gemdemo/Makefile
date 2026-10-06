# GEM VDI showcase for the Atari ST, built with the m68k-atari-mintelf
# GCC cross toolchain (https://tho-otto.de/crossmint.php).
#
#   make CROSS=~/computers/atari-cc/opt/cross-mint/bin/m68k-atari-mintelf-
#   make run        # boot it in Hatari through the agent API

CROSS  ?= m68k-atari-mintelf-
CC      = $(CROSS)gcc
CFLAGS  = -m68000 -Os -Wall -Wextra -Wno-unused-parameter -fomit-frame-pointer
LDFLAGS = -s
LIBS    = -lgem -lm

OUT     = build
PRG     = $(OUT)/GEMDEMO.PRG
SRCS    = gemdemo.c natfeats.c

all: $(PRG)

$(PRG): $(SRCS) natfeats.h | $(OUT)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRCS) $(LIBS)
	@ls -l $@

$(OUT):
	mkdir -p $(OUT)

# Hatari with the agent API: build dir as drive C:, autostart the demo
run: $(PRG)
	../../tools/agent/hatari-agent-run.sh --machine st --natfeats on \
		--harddrive $(CURDIR)/$(OUT) --auto 'C:\GEMDEMO.PRG'

clean:
	rm -rf $(OUT)

.PHONY: all run clean
