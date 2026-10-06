# Dot Chase - Atari STE port
#   make CROSS=~/computers/atari-cc/opt/cross-mint/bin/m68k-atari-mintelf-
#   make run
NAME     = DOTCHASE
SRCS     = main_st.c game.c draw.c st_input.c
ST_EXTRA = paula
MACHINE  = ste
# sound.c pokes Paula: built as C++ through sound_st.cpp (see ../st_port/paula.h)
LIBS     = $(OUT)/sound.o
include ../st_port/port.mk

$(PRG): $(OUT)/sound.o
$(OUT)/sound.o: sound_st.cpp sound.c $(HDRS) | $(OUT)
	$(CXX) $(CFLAGS) -fno-exceptions -fno-rtti -c -o $@ $<
