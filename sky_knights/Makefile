# Sky Knights - Atari STE port
#   make CROSS=~/computers/atari-cc/opt/cross-mint/bin/m68k-atari-mintelf-
#   make run
NAME     = SKYKNGHT
SRCS     = main_st.c game.c draw.c st_input.c
ST_EXTRA = paula amiga_dos
MACHINE  = ste
# sound.c pokes Paula: built as C++ through sound_st.cpp (see ../st_port/paula.h)
LIBS     = $(OUT)/sound.o
include ../st_port/port.mk

$(PRG): $(OUT)/sound.o
$(OUT)/sound.o: sound_st.cpp sound.c $(HDRS) | $(OUT)
	$(CXX) $(CFLAGS) -fno-exceptions -fno-rtti -c -o $@ $<

# the generated samples (gen_sounds.py), as sound.c loads them
RAW = $(OUT)/FLAP.RAW $(OUT)/SMASH.RAW
all: $(RAW)
run: $(RAW)
$(OUT)/FLAP.RAW: flap.raw | $(OUT)
	cp $< $@
$(OUT)/SMASH.RAW: smash.raw | $(OUT)
	cp $< $@
