#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""Copy an amiga3d engine game from amiga_games into this repo for the Falcon.

    port_game.py GAME PRG TPRG [--from DIR]

Copies the game's C sources and headers, tests/test_cases.c and
tests/vectors.h, and its music and voice (the files its game.c names as
game_music / game_voice), unchanged; writes a Makefile for ../a3d.mk from
the upstream Makefile's NAME / CORE / GAME / TESTS / HEADERS. The default
source is ../../../amiga_mcp/examples/GAME (the amiga_games submodule).
"""
import os
import re
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))

game, prg, tprg = sys.argv[1:4]
src = sys.argv[sys.argv.index("--from") + 1] if "--from" in sys.argv else \
    os.path.join(REPO, "..", "amiga_mcp", "examples", game)
dst = os.path.join(REPO, game)
os.makedirs(os.path.join(dst, "tests"), exist_ok=True)
os.makedirs(os.path.join(dst, "docs"), exist_ok=True)
for f in sorted(os.listdir(src)):
    if f.endswith((".c", ".h")):
        shutil.copy2(os.path.join(src, f), dst)
for f in ("test_cases.c", "vectors.h"):
    if os.path.exists(os.path.join(src, "tests", f)):
        shutil.copy2(os.path.join(src, "tests", f), os.path.join(dst, "tests"))

mk = open(os.path.join(src, "Makefile")).read()
var = {k: re.search(r"^%s\s*=\s*(.*)$" % k, mk, re.M).group(1).strip()
       for k in ("NAME", "CORE", "GAME", "TESTS", "HEADERS")}
game_c = open(os.path.join(src, "game.c")).read()
data = []
for k in ("game_music", "game_voice"):
    m = re.search(r'%s\[\]\s*=\s*"(?:PROGDIR:)?([^"]*)"' % k, game_c)
    if m and m.group(1):
        shutil.copy2(os.path.join(src, m.group(1)), dst)
        data.append(m.group(1))
title = re.search(r'game_title\[\]\s*=\s*"([^"]*)"', game_c).group(1)

with open(os.path.join(dst, "Makefile"), "w") as f:
    f.write("""# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
# %s - Atari Falcon030 port (the amiga3d engine, ../a3d_falcon)
#   make CROSS=~/computers/atari-cc/opt/cross-mint/bin/m68k-atari-mintelf-
#   make run / make test / make host-test
NAME    = %s
PRG     = %s
TPRG    = %s
CORE    = %s
GAME    = %s
TESTS   = %s
HEADERS = %s
""" % (title, var["NAME"], prg, tprg, var["CORE"], var["GAME"], var["TESTS"], var["HEADERS"]))
    if data:
        f.write("DATA    = %s\n" % " ".join(data))
    f.write("include ../a3d_falcon/a3d.mk\n")
print("%s: %s -> %s (%s)" % (game, src, dst, ", ".join(data) or "no data"))
