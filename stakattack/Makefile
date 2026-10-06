# StakAttack - Atari STE port
#   make CROSS=~/computers/atari-cc/opt/cross-mint/bin/m68k-atari-mintelf-
#   make run
NAME     = STAKATTK
SRCS     = main_st.c game.c draw.c st_input.c
ST_EXTRA = paula ptplayer amiga_dos
MACHINE  = ste
include ../st_port/port.mk

# the MOD, under the 8.3 name the AmigaDOS shim maps it to
MOD = $(OUT)/STAKATTA.MOD
all: $(MOD)
run: $(MOD)
$(MOD): stakattack.mod | $(OUT)
	cp $< $@
