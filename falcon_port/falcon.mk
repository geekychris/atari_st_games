# Shared build rules for Amiga (AGA) -> Atari Falcon030 game ports.
# A port's Makefile sets NAME, SRCS (C and C++), optional EXTRA_CFLAGS,
# then includes this file.  Reuses the ST port's IKBD input, NatFeats
# and amiga_types.h (../st_port); falcon_port/ has the 16 bit true colour
# graphics layer (fgfx), Paula emulation on DMA sound (../st_port/paula) and its
# own Amiga header shims in compat/.
#
#   make CROSS=~/computers/atari-cc/opt/cross-mint/bin/m68k-atari-mintelf-
#   make run         # Falcon in Hatari + agent API, build/ as C:, autostart

CROSS   ?= m68k-atari-mintelf-
CC       = $(CROSS)gcc
CXX      = $(CROSS)g++
FPORT   := $(dir $(lastword $(MAKEFILE_LIST)))
STPORT  := $(FPORT)../st_port/
# Game code is compiled for the 030; libraries come from the plain 68000
# multilib, since mintlib's m68020-60 one requires an FPU (68881/2) and
# most Falcons have none.  -msoft-float keeps any float code FPU free.
CPU     ?= -m68030 -msoft-float
LDCPU   ?= -m68000
COMMON   = $(CPU) -O2 -fomit-frame-pointer -Wall -Wno-unused-function \
           -I. -I$(FPORT) -I$(FPORT)compat -I$(STPORT) $(EXTRA_CFLAGS)
CFLAGS   = $(COMMON)
CXXFLAGS = $(COMMON) -fno-exceptions -fno-rtti
STRIP   ?=
OUT      = build
PRG      = $(OUT)/$(NAME).PRG
F_SRCS   = $(FPORT)fgfx.c $(STPORT)paula.c $(FPORT)abstub.c $(STPORT)st_ikbd.c $(STPORT)natfeats.c
ALL_SRCS = $(SRCS) $(F_SRCS)
OBJS     = $(patsubst %,$(OUT)/obj/%.o,$(notdir $(basename $(ALL_SRCS))))
HDRS     = $(wildcard *.h) $(wildcard $(FPORT)*.h) $(wildcard $(FPORT)compat/*.h) $(wildcard $(STPORT)*.h)
TOP     := $(abspath $(FPORT)../..)
TOS     ?= $(TOP)/roms/emutos/emutos-1024k-1.4/etos1024k.img
# 14 MB TT RAM-less Falcon, VGA monitor (320x240 true colour)
HATARI_FALCON ?= --machine falcon --dsp none --memsize 14 --monitor vga

vpath %.c   . $(FPORT) $(STPORT)
vpath %.cpp . $(FPORT)

all: $(PRG)

$(PRG): $(OBJS)
	$(CXX) $(LDCPU) $(STRIP) -o $@ $(OBJS) $(LIBS)
	@ls -l $@

$(OUT)/obj/%.o: %.c $(HDRS) | $(OUT)/obj
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUT)/obj/%.o: %.cpp $(HDRS) | $(OUT)/obj
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(OUT)/obj:
	mkdir -p $@

run: $(PRG)
	AGENT_TOS=$${AGENT_TOS:-$(TOS)} $(TOP)/tools/agent/hatari-agent-run.sh \
		$(HATARI_FALCON) --natfeats on \
		--harddrive $(CURDIR)/$(OUT) --auto 'C:\$(NAME).PRG' $(HATARI_OPTS)

clean:
	rm -rf $(OUT)

.PHONY: all run clean
