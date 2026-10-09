# Shared build rules for the 3DO-derived Amiga ports on the Atari Falcon030
# (planet_chomp, rolling_steel, spectral_keep).  A port's Makefile sets
# NAME and SRCS (the game's files and its 68k main loop), optional
# EXTRA_CFLAGS, then includes this file.  The machine layer is sys3do.c
# here; the screen code, Paula emulation and IKBD input come from
# ../falcon_port and ../st_port.
#
#   make CROSS=~/computers/atari-cc/opt/cross-mint/bin/m68k-atari-mintelf-
#   make run         # Falcon in Hatari + agent API, build/ as C:, autostart
CROSS   ?= m68k-atari-mintelf-
CC       = $(CROSS)gcc
F3DO    := $(dir $(lastword $(MAKEFILE_LIST)))
FPORT   := $(F3DO)../falcon_port/
STPORT  := $(F3DO)../st_port/
# 68020-68060 code: runs on a plain Falcon (030) and on 060 accelerators
# (CT60), which lack the 64-bit multiply and divide GCC uses for -m68030
CPU     ?= -m68020-60 -msoft-float
LDCPU   ?= -m68000
# -Dmain=game_main: sys3do.c's main() enters supervisor mode around it;
# -fno-defer-pop: see ../st_port/port.mk.  -fno-omit-frame-pointer: without
# a frame pointer this GCC (14.3, m68k) pushes some 64-bit arguments, as
# in softcel.c's ((long long)tw << 32) / w, as "subq.l #8,%sp; move.l
# N(%sp),(%sp)" with N not adjusted for the subq: the division reads the
# wrong local (Planet Chomp's sky covered half the screen)
CFLAGS   = $(CPU) -O2 -fno-omit-frame-pointer -fno-defer-pop -Wall -Wno-unused-function \
           -Wno-pointer-sign -Dmain=game_main -DF3DO_NAME=\"$(NAME)\" \
           -I. -I$(F3DO) -I$(F3DO)compat -I$(FPORT) -I$(FPORT)compat -I$(STPORT) $(EXTRA_CFLAGS)
OUT      = build
PRG      = $(OUT)/$(NAME).PRG
L_SRCS   = $(F3DO)sys3do.c $(FPORT)fgfx.c $(FPORT)abstub.c $(STPORT)paula.c \
           $(STPORT)st_ikbd.c $(STPORT)natfeats.c $(STPORT)amiga_dos.c
OBJS     = $(patsubst %,$(OUT)/obj/%.o,$(notdir $(basename $(SRCS) $(L_SRCS))))
HDRS     = $(wildcard *.h) $(wildcard $(F3DO)*.h) $(wildcard $(F3DO)compat/*.h) $(wildcard $(STPORT)*.h)
TOP     := $(abspath $(F3DO)../..)
TOS     ?= $(TOP)/roms/emutos/emutos-1024k-1.4/etos1024k.img
HATARI_FALCON ?= --machine falcon --dsp none --memsize 14 --monitor vga

vpath %.c . $(F3DO) $(FPORT) $(STPORT)

all: $(PRG) $(OUT)/DATA

$(PRG): $(OBJS)
	$(CC) $(LDCPU) -o $@ $(OBJS) $(LIBS)
	@ls -l $@

$(OUT)/obj/%.o: %.c $(HDRS) | $(OUT)/obj
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUT)/obj:
	mkdir -p $@

# the game's data/ beside the program, as GEMDOS 8.3 names (sys_load
# shortens the names it is asked for the same way)
$(OUT)/DATA: $(wildcard data/*) | $(OUT)/obj
	rm -rf $@
	mkdir -p $@
	$(if $(wildcard data/*),for f in data/*; do n=`basename $$f`; \
		b=`echo $${n%.*} | cut -c1-8`; e=`echo $${n##*.} | cut -c1-3`; \
		cp $$f $@/`echo $$b.$$e | tr a-z A-Z`; done)

run: all
	AGENT_TOS=$${AGENT_TOS:-$(TOS)} $(TOP)/tools/agent/hatari-agent-run.sh \
		$(HATARI_FALCON) --natfeats on \
		--harddrive $(CURDIR)/$(OUT) --auto 'C:\$(NAME).PRG' $(HATARI_OPTS)

clean:
	rm -rf $(OUT)

.PHONY: all run clean
