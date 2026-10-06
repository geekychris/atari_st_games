# Pea Shooter Blast - Atari STE port
#   make CROSS=~/computers/atari-cc/opt/cross-mint/bin/m68k-atari-mintelf-
#   make run
# The original's music.mod is not included; copy a ProTracker module to
# build/MUSIC.MOD to have music.
NAME     = PEASHOOT
SRCS     = main_st.c game.c draw.c levels.c st_input.c
ST_EXTRA = paula ptplayer amiga_dos ab_log st_blit
MACHINE  = ste
include ../st_port/port.mk
