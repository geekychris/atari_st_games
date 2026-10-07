# Orb Hunter - Atari STE port
#   make CROSS=~/computers/atari-cc/opt/cross-mint/bin/m68k-atari-mintelf-
#   make run
# The original's axelf.mod is not included; copy a ProTracker module to
# build/AXELF.MOD to have music.
NAME     = ORBHUNTR
SRCS     = main_st.c game.c draw.c levels.c st_input.c
ST_EXTRA = paula ptplayer amiga_dos ab_log
MACHINE  = ste
include ../st_port/port.mk
