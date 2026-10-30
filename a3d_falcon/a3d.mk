# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
#
# a3d.mk - building an amiga3d engine game for the Atari Falcon030 (the
# counterpart of amiga_games' examples/amiga3d/a3d.mk). A game's Makefile
# sets:
#   NAME        the game (its directory and bridge client name)
#   PRG         the program's 8.3 name, without .PRG (the game)
#   TPRG        the test program's (the host tests' cases on the Falcon)
#   CORE        its pure sources, GAME its other sources, TESTS its test
#               cases, HEADERS what everything depends on (as upstream)
#   TRACE       optional: tests/trace_main.c, a trace to diff with the host's
#               (make trace: build/TRACE.PRG in Hatari against the host build)
#   DATA        optional: files the game loads (music, voice), copied into
#               build/DATA/ under their 8.3 names (as main_falcon.c opens them)
# then: include ../a3d_falcon/a3d.mk
#
#   make            build/PRG.PRG (and build/DATA/), and the tests' build/obj/test/TPRG.PRG
#                   (kept out of build/'s top, which the launcher and the packager copy)
#   make run        the game in Hatari (agent API), build/ as C:, autostart
#   make test       the tests in Hatari: they must all pass (the trace hash)
#   make host-test  the tests, built and run natively

CROSS   ?= m68k-atari-mintelf-
CC       = $(CROSS)gcc
A3F     := $(dir $(lastword $(MAKEFILE_LIST)))
STPORT  := $(A3F)../st_port/
# 68020-68060 code, so one program runs on a stock Falcon and on a 68060
# accelerator (CT60): -m68030 would use the 64-bit multiply and divide forms
# the 68060 lacks (they trap), and in Block Breaker 3D it was no faster on
# the 030 either. Libraries come from the 68000 multilib: mintlib's
# m68020-60 one needs an FPU, which most Falcons lack (-msoft-float keeps
# the games' code FPU free)
CPU     ?= -m68020-60 -msoft-float
LDCPU   ?= -m68000
# -fno-defer-pop: main() runs between Super(0L) and Super(old_ssp), which
# need the stack at the same depth; -fno-omit-frame-pointer: without one
# this GCC (14.3, m68k) can push 64-bit arguments at the wrong offset (see
# ../falcon_3do/f3do.mk)
CFLAGS   = $(CPU) -O2 -fno-omit-frame-pointer -fno-defer-pop -Wall -DA3_FALCON \
           -I. -I$(A3F) -I$(A3F)compat -I$(STPORT) $(EXTRA_CFLAGS)
OUT      = build
TOUT     = $(OUT)/obj/test
ENGINE   = $(A3F)a3d_math.c $(A3F)a3d_fix.c $(A3F)a3d_draw.c $(A3F)a3d_rows.S $(A3F)a3d_sound.c
PLATFORM = $(A3F)main_falcon.c $(A3F)falcon_sys.c $(A3F)bridge.c $(STPORT)paula.c $(STPORT)ptplayer.c \
           $(STPORT)st_ikbd.c $(STPORT)natfeats.c
OBJS     = $(patsubst %,$(OUT)/obj/%.o,$(notdir $(basename $(GAME) $(CORE) $(ENGINE) $(PLATFORM))))
TOBJS    = $(patsubst %,$(OUT)/obj/%.o,$(notdir $(basename $(CORE) $(TESTS) $(A3F)a3d_math.c $(A3F)a3d_fix.c \
           $(A3F)test_falcon.c $(A3F)bridge.c $(STPORT)natfeats.c)))
TROBJS   = $(patsubst %,$(OUT)/obj/%.o,$(notdir $(basename $(CORE) $(A3F)a3d_math.c $(A3F)a3d_fix.c \
           $(A3F)trace_falcon.c $(STPORT)natfeats.c))) $(OUT)/obj/trace_main.o
A3D_HEADERS = $(wildcard $(A3F)*.h) $(wildcard $(A3F)compat/*.h) $(wildcard $(STPORT)*.h)
HATARI_DIR ?= $(abspath $(A3F)../../hatari_augmented)
AGENT    = python3 $(A3F)tools/a3d_agent.py

vpath %.c . tests $(A3F) $(STPORT)
vpath %.S $(A3F)

# DATA: name.ext -> NAME8.EXT in build/ (a3f_path in main_falcon.c)
to83 = $(shell echo $(basename $(notdir $(1))) | cut -c1-8 | tr a-z A-Z).$(shell echo $(subst .,,$(suffix $(1))) | cut -c1-3 | tr a-z A-Z)
DATA83 = $(foreach f,$(DATA),$(call to83,$(f)))

all: $(OUT)/$(PRG).PRG $(TOUT)/$(TPRG).PRG $(addprefix $(OUT)/DATA/,$(DATA83))

# The 68000 libgcc builds 64-bit multiply and divide out of 32-bit library
# calls; the 68020-60 multilib's use muls.l / divu.l (32-bit forms, which a
# 68060 has too). Only these are taken from it (as ../falcon_3do/f3do.mk).
LIBGCC020 = $(shell $(CC) -m68020-60 -print-libgcc-file-name)
DI3       = _divdi3.o _udivdi3.o _moddi3.o _umoddi3.o _muldi3.o

$(OUT)/obj/di3020.a: | $(OUT)/obj
	cd $(OUT)/obj && $(CROSS)ar x $(LIBGCC020) $(DI3) && \
		$(CROSS)ar rcs di3020.a $(DI3) && rm -f $(DI3)

$(OUT)/$(PRG).PRG: $(OBJS) $(OUT)/obj/di3020.a
	$(CC) $(LDCPU) -o $@ $(OBJS) $(OUT)/obj/di3020.a
	@ls -l $@

$(TOUT)/$(TPRG).PRG: $(TOBJS) $(OUT)/obj/di3020.a | $(TOUT)
	$(CC) $(LDCPU) -o $@ $(TOBJS) $(OUT)/obj/di3020.a

$(TOUT)/TRACE.PRG: $(TROBJS) $(OUT)/obj/di3020.a | $(TOUT)
	$(CC) $(LDCPU) -o $@ $(TROBJS) $(OUT)/obj/di3020.a

$(OUT)/obj/trace_main.o: $(TRACE) $(HEADERS) $(A3D_HEADERS) | $(OUT)/obj
	$(CC) $(CFLAGS) -Dmain=trace_main -c -o $@ $<

$(OUT)/obj/%.o: %.c $(HEADERS) $(A3D_HEADERS) | $(OUT)/obj
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUT)/obj/%.o: %.S | $(OUT)/obj
	$(CC) $(CPU) -c -o $@ $<

$(OUT)/obj $(TOUT) $(OUT)/DATA:
	mkdir -p $@

define data_rule
$(OUT)/DATA/$(call to83,$(1)): $(1) | $(OUT)/DATA
	cp $$< $$@
endef
$(foreach f,$(DATA),$(eval $(call data_rule,$(f))))

run: all
	$(AGENT) run $(OUT) $(PRG) $(HATARI_OPTS)

test: all
	$(AGENT) test $(TOUT) $(TPRG) $(HATARI_OPTS)

# the trace on the Falcon (build/obj/test/TRACE.TXT) and on the host (tests/host_trace.txt): the same
trace: $(TOUT)/TRACE.PRG
	rm -f $(TOUT)/TRACE.TXT
	$(AGENT) trace $(TOUT) $(HATARI_OPTS)
	$(HOSTCC) -O2 -I. -I$(A3F) -o tests/host_trace $(TRACE) $(CORE) $(A3F)a3d_math.c $(A3F)a3d_fix.c
	./tests/host_trace > tests/host_trace.txt
	tr -d '\r' < $(TOUT)/TRACE.TXT | cmp - tests/host_trace.txt && \
		echo "trace: the Falcon's $$(wc -l < tests/host_trace.txt) lines match the host's"

HOSTCC ?= cc
host-test:
	$(HOSTCC) -O2 -Wall -I. -I$(A3F) -o tests/host_tests $(A3F)test_host.c $(TESTS) $(CORE) $(A3F)a3d_math.c $(A3F)a3d_fix.c
	./tests/host_tests

clean:
	rm -rf $(OUT) tests/host_tests tests/host_trace tests/host_trace.txt

.PHONY: all run test trace host-test clean
