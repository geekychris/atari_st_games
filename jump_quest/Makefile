# Jump Quest - Atari STE port
#   make CROSS=~/computers/atari-cc/opt/cross-mint/bin/m68k-atari-mintelf-
#   make run
NAME     = JUMPQST
SRCS     = main_st.c gfx.c level.c player.c enemy.c items.c hud.c title.c st_input.c
ST_EXTRA = paula amiga_dos
MACHINE  = ste
# sound.c is built as C++ (below) so its DMACON writes reach the Paula
# emulation (see ../st_port/paula.h)
LIBS     = $(OUT)/sound.o
include ../st_port/port.mk

$(PRG): $(OUT)/sound.o
$(OUT)/sound.o: sound.c $(HDRS) | $(OUT)
	$(CXX) $(CFLAGS) -fno-exceptions -fno-rtti -x c++ -c -o $@ $<
