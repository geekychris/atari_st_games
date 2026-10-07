# Bullion Dash - Atari STE port
#   make CROSS=~/computers/atari-cc/opt/cross-mint/bin/m68k-atari-mintelf-
#   make run
NAME     = BULLION
SRCS     = main_st.c player.c enemy.c level.c editor.c render.c sound.c st_input.c
ST_EXTRA = paula amiga_dos ab_log
MACHINE  = ste
# upstream: a few unused variables
EXTRA_CFLAGS = -Wno-unused-variable -Wno-unused-but-set-variable
# modplay.c pokes Paula: built as C++ through modplay_st.cpp (see ../st_port/paula.h)
LIBS     = $(OUT)/modplay.o
include ../st_port/port.mk

$(PRG): $(OUT)/modplay.o
$(OUT)/modplay.o: modplay_st.cpp modplay.c $(HDRS) | $(OUT)
	$(CXX) $(CFLAGS) -fno-exceptions -fno-rtti -c -o $@ $<
