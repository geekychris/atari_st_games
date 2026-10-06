# Void Trader - Atari Falcon030 port
#   make CROSS=~/computers/atari-cc/opt/cross-mint/bin/m68k-atari-mintelf-
#   make run
NAME = VTRADER
SRCS = main_falcon.c engine3d.c models.c combat.c scanner.c trade.c sfx.c \
       audio_falcon.cpp modplay.c
include ../falcon_port/falcon.mk

# The original modplay.c, unchanged: built as C++ so custom.dmacon writes
# go through the Paula emulation (../st_port/paula.h), entry points renamed so
# audio_falcon.cpp can wrap them.
MP_RENAME = -Dmodplay_init=mp_init_raw -Dmodplay_start=mp_start_raw \
            -Dmodplay_start_song=mp_start_song_raw -Dmodplay_stop=mp_stop_raw \
            -Dmodplay_cleanup=mp_cleanup_raw -Dmodplay_tick=mp_tick_raw \
            -Dmodplay_sfx=mp_sfx_raw
$(OUT)/obj/modplay.o: modplay.c $(HDRS) | $(OUT)/obj
	$(CXX) $(CXXFLAGS) -x c++ $(MP_RENAME) -c -o $@ $<
