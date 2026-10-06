# INTERACT: GEM test target for the Hatari agent API
#
#   make CROSS=~/computers/atari-cc/opt/cross-mint/bin/m68k-atari-mintelf-
#   make run          # Hatari + agent API, build/ as C:, autostart
#   make test         # run tests/run-all.sh against the running emulator

CROSS  ?= m68k-atari-mintelf-
CC      = $(CROSS)gcc
CFLAGS  = -m68000 -Os -Wall -Wextra -Wno-unused-parameter -fomit-frame-pointer -I../gemdemo
LDFLAGS = -s
LIBS    = -lgem

OUT     = build
PRG     = $(OUT)/INTERACT.PRG
SRCS    = interact.c ../gemdemo/natfeats.c

all: $(PRG)

$(PRG): $(SRCS) | $(OUT)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRCS) $(LIBS)
	@ls -l $@

$(OUT):
	mkdir -p $(OUT)

run: $(PRG)
	tests/start.sh

test:
	tests/run-all.sh

clean:
	rm -rf $(OUT)

.PHONY: all run test clean
