# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
# Terra Strike - Atari Falcon030 port (its own engine; the screen, fill,
# input, sound and bridge from ../a3d_falcon)
#   make CROSS=~/computers/atari-cc/docker-bin/m68k-atari-mintelf-
#   make run         Hatari (agent API), build/ as C:, autostart
#   make test        the unit tests on the Falcon in Hatari
#   make trace       a scripted game on the Falcon, diffed with the host's
#   make host-test   the unit tests, natively
CROSS   ?= m68k-atari-mintelf-
CC       = $(CROSS)gcc
A3F     := ../a3d_falcon/
STPORT  := ../st_port/
# 68020-68060 code (one program for a stock Falcon and a 68060), 68000
# libraries without an FPU; see ../a3d_falcon/a3d.mk
CPU     ?= -m68020-60 -msoft-float
LDCPU   ?= -m68000
CFLAGS   = $(CPU) -O2 -fno-omit-frame-pointer -fno-defer-pop -Wall -DTS_FALCON \
           -I. -Icompat -I$(A3F) -I$(A3F)compat -I$(STPORT)
OUT      = build
TOUT     = $(OUT)/obj/test
PRG      = TERRA
CORE     = ts_math.c terrain.c world.c
GAME     = game.c render.c models.c sound.c title_image.c tests/test_cases.c
PLATFORM = main_falcon.c $(A3F)falcon_sys.c $(A3F)a3d_rows.S $(A3F)bridge.c \
           $(STPORT)paula.c $(STPORT)ptplayer.c $(STPORT)st_ikbd.c $(STPORT)natfeats.c
objs     = $(patsubst %,$(OUT)/obj/%.o,$(notdir $(basename $(1))))
OBJS     = $(call objs,$(GAME) $(CORE) $(PLATFORM))
TOBJS    = $(call objs,falcon_test.c tests/test_cases.c $(CORE) $(A3F)bridge.c $(STPORT)natfeats.c)
TROBJS   = $(call objs,$(CORE) $(A3F)trace_falcon.c $(STPORT)natfeats.c) $(OUT)/obj/trace_main.o
HEADERS  = $(wildcard *.h) $(wildcard compat/*/*.h) $(wildcard $(A3F)*.h) $(wildcard $(A3F)compat/*.h) $(wildcard $(STPORT)*.h)
AGENT    = python3 $(A3F)tools/a3d_agent.py

vpath %.c . tests $(A3F) $(STPORT)
vpath %.S $(A3F)

all: $(OUT)/$(PRG).PRG $(OUT)/DATA/TERRA_ST.MOD $(TOUT)/TSTEST.PRG

# 64-bit multiply and divide from the 68020-60 libgcc (see ../a3d_falcon/a3d.mk)
LIBGCC020 = $(shell $(CC) -m68020-60 -print-libgcc-file-name)
DI3       = _divdi3.o _udivdi3.o _moddi3.o _umoddi3.o _muldi3.o

$(OUT)/obj/di3020.a: | $(OUT)/obj
	cd $(OUT)/obj && $(CROSS)ar x $(LIBGCC020) $(DI3) && \
		$(CROSS)ar rcs di3020.a $(DI3) && rm -f $(DI3)

$(OUT)/$(PRG).PRG: $(OBJS) $(OUT)/obj/di3020.a
	$(CC) $(LDCPU) -o $@ $(OBJS) $(OUT)/obj/di3020.a
	@ls -l $@

$(TOUT)/TSTEST.PRG: $(TOBJS) $(OUT)/obj/di3020.a | $(TOUT)
	$(CC) $(LDCPU) -o $@ $(TOBJS) $(OUT)/obj/di3020.a

$(TOUT)/TRACE.PRG: $(TROBJS) $(OUT)/obj/di3020.a | $(TOUT)
	$(CC) $(LDCPU) -o $@ $(TROBJS) $(OUT)/obj/di3020.a

$(OUT)/obj/trace_main.o: tests/trace_main.c $(HEADERS) | $(OUT)/obj
	$(CC) $(CFLAGS) -Dmain=trace_main -c -o $@ $<

$(OUT)/obj/%.o: %.c $(HEADERS) | $(OUT)/obj
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUT)/obj/%.o: %.S | $(OUT)/obj
	$(CC) $(CPU) -c -o $@ $<

$(OUT)/DATA/TERRA_ST.MOD: terra_strike.mod | $(OUT)/DATA
	cp $< $@

$(OUT)/obj $(TOUT) $(OUT)/DATA:
	mkdir -p $@

run: all
	$(AGENT) run $(OUT) $(PRG) $(HATARI_OPTS)

test: all
	$(AGENT) test $(TOUT) TSTEST $(HATARI_OPTS)

HOSTCC ?= cc
trace: $(TOUT)/TRACE.PRG
	rm -f $(TOUT)/TRACE.TXT
	$(AGENT) trace $(TOUT) $(HATARI_OPTS)
	$(HOSTCC) -O2 -Wall -o tests/host_trace tests/trace_main.c $(CORE)
	./tests/host_trace > tests/host_trace.txt
	tr -d '\r' < $(TOUT)/TRACE.TXT | cmp - tests/host_trace.txt && \
		echo "trace: the Falcon's $$(wc -l < tests/host_trace.txt) lines match the host's"

host-test:
	$(HOSTCC) -O2 -Wall -o tests/host_tests tests/host_main.c tests/test_cases.c $(CORE)
	./tests/host_tests

clean:
	rm -rf $(OUT) tests/host_tests tests/host_trace tests/host_trace.txt

.PHONY: all run test trace host-test clean
