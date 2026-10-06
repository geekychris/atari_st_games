# Shared build rules for the Amiga -> Atari ST game ports.
# A port's Makefile sets NAME (8.3 program name without .PRG), SRCS,
# optionally TOS / MACHINE / EXTRA_CFLAGS, then includes this file.
# compat/ has Amiga header shims (exec/types.h, graphics/rastport.h,
# proto/graphics.h...) so unmodified Amiga drawing code compiles.
#
#   make CROSS=~/computers/atari-cc/opt/cross-mint/bin/m68k-atari-mintelf-
#   make run         # Hatari + agent API, build/ as C:, autostart
#   make STRIP=-s    # strip symbols (smaller PRG; default keeps them for
#                    # the Hatari profiler / debugger)

CROSS   ?= m68k-atari-mintelf-
CC       = $(CROSS)gcc
CXX      = $(CROSS)g++
PORT    := $(dir $(lastword $(MAKEFILE_LIST)))
CPU     ?= -m68000
CFLAGS   = $(CPU) -O2 -fomit-frame-pointer -Wall -Wno-unused-function -I. -I$(PORT) -I$(PORT)compat $(EXTRA_CFLAGS)
STRIP   ?=
OUT      = build
PRG      = $(OUT)/$(NAME).PRG
# ST_EXTRA: optional layer modules a port uses, e.g.
#   paula ptplayer   Paula emulation on STE DMA sound + MOD player
#   amiga_dos        AmigaDOS Open/Read/Seek on GEMDOS
#   ab_log           amiga_games bridge AB_I/AB_W/AB_E -> NatFeats
ST_SRCS  = $(PORT)st_gfx.c $(PORT)st_ikbd.c $(PORT)st_ym.c $(PORT)natfeats.c \
           $(patsubst %,$(PORT)%.c,$(ST_EXTRA))
HDRS     = $(wildcard *.h) $(wildcard $(PORT)*.h) $(wildcard $(PORT)compat/*.h)
TOP     := $(abspath $(PORT)../..)
MACHINE ?= st
# the 192k EmuTOS reports TOS 1.04, which Hatari only runs as a plain ST
ifeq ($(MACHINE),st)
TOS     ?= $(TOP)/roms/emutos/emutos-192k-1.4/etos192uk.img
else
TOS     ?= $(TOP)/roms/emutos/emutos-1024k-1.4/etos1024k.img
endif

all: $(PRG)

$(PRG): $(SRCS) $(ST_SRCS) $(HDRS) | $(OUT)
	$(CC) $(CFLAGS) $(STRIP) -o $@ $(SRCS) $(ST_SRCS) $(LIBS)
	@ls -l $@

$(OUT):
	mkdir -p $(OUT)

run: $(PRG)
	AGENT_TOS=$${AGENT_TOS:-$(TOS)} $(TOP)/tools/agent/hatari-agent-run.sh \
		--machine $(MACHINE) --natfeats on \
		--harddrive $(CURDIR)/$(OUT) --auto 'C:\$(NAME).PRG' $(HATARI_OPTS)

clean:
	rm -rf $(OUT)

.PHONY: all run clean
