# Ace Pilot - Atari Falcon030 port
#   make CROSS=~/computers/atari-cc/opt/cross-mint/bin/m68k-atari-mintelf-
#   make run
NAME = ACEPILOT
SRCS = main_falcon.c game.c engine3d.c models.c render.c sound.c st_input.c ptplayer.c
# upstream style: several one-line ifs per line
EXTRA_CFLAGS = -Wno-misleading-indentation
include ../falcon_port/falcon.mk

# the MOD (gen_mod.py), as main_falcon.c loads it
MOD = $(OUT)/BLUE_DAN.MOD
all: $(MOD)
run: $(MOD)
$(MOD): blue_danube.mod | $(OUT)
	cp $< $@
