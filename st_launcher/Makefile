# ST game launcher: a menu of the game ports on drive C:
#   make CROSS=~/computers/atari-cc/opt/cross-mint/bin/m68k-atari-mintelf-
#   make drive    # build every game and assemble build/drive (C:)
#   make play     # ... and start Hatari (STE) with the launcher
NAME     = LAUNCHER
SRCS     = launcher.c
MACHINE  = ste
include ../st_port/port.mk

drive: $(PRG)
	python3 make_drive.py

play: $(PRG)
	python3 make_drive.py --run

.PHONY: drive play
