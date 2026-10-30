#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""Rotoball (Falcon port) test through the Hatari agent API.

Resets the emulator (the game autostarts and the match starts at once, as
on the Amiga), checks that W/A/S/D move the player's skimmer (its position
read from emulated memory at the address the game logs), plays the whole
90-second match through with fast-forward, checks the match-over line,
quits with Esc and saves screenshots.

    make run            # in another shell (HATARI_API if not :7777)
    ./rotoball_test.py [outdir]
"""
import json
import os
import re
import struct
import sys
import urllib.parse
import urllib.request

API = os.environ.get("HATARI_API", "http://127.0.0.1:7777")
OUT = sys.argv[1] if len(sys.argv) > 1 else "test_shots"
failures = []


def call(path, method="GET", **params):
    url = API + path + ("?" + urllib.parse.urlencode(params) if params else "")
    with urllib.request.urlopen(urllib.request.Request(url, method=method), timeout=300) as r:
        data = r.read()
    return json.loads(data) if data[:1] == b"{" else data


def run(frames):
    call("/emu/run", "POST", frames=frames, pause=1)


def console():
    return call("/console")["text"]


def wait_for(text, frames=600, since=0, step=20):
    for _ in range(frames // step):
        if text in console()[since:]:
            return True
        run(step)
    return text in console()[since:]


def shot(name):
    """Screenshot at 2x (nearest neighbour) when PIL is there, else 1x."""
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, name + ".png")
    with open(path, "wb") as f:
        f.write(call("/screen"))
    try:
        from PIL import Image
        img = Image.open(path)
        img.resize((img.width * 2, img.height * 2), Image.NEAREST).save(path)
    except ImportError:
        pass


def check(label, ok, detail=""):
    print(("  PASS " if ok else "  FAIL ") + label + (f"  ({detail})" if detail and not ok else ""))
    if not ok:
        failures.append(label)


def p1_pos(addr):
    x, z = struct.unpack(">ll", bytes.fromhex(call("/mem", addr=addr, len=8)["hex"]))
    return x / 65536.0, z / 65536.0


def hold(key, frames):
    call("/input/key", "POST", key=key, action="down")
    run(frames)
    call("/input/key", "POST", key=key, action="up")
    run(2)


# cold reset: Hatari autostarts the game again (make run set it up)
boot = len(console())
call("/emu/reset", "POST", type="cold")
check("match starts (score=0:0)", wait_for("rotoball I score=0:0", 1800, boot))
m = re.findall(r"rotoball I SYMBOL p1 0x([0-9a-f]+)", console()[boot:])
p1 = int(m[-1], 16) if m else None
run(30)
shot("kickoff")

if p1 is not None:
    x0, z0 = p1_pos(p1)
    hold("w", 90)
    x1, z1 = p1_pos(p1)
    check(f"W drives forward (+x: {x0:.1f} -> {x1:.1f})", x1 > x0 + 5)
    shot("play")
    hold("a", 60)
    x2, z2 = p1_pos(p1)
    hold("d", 120)
    x3, z3 = p1_pos(p1)
    # P1 faces +x; its screen-right is -z (pitch.c projects -z to the right)
    check(f"A / D strafe left / right (z {z1:.1f} -> {z2:.1f} -> {z3:.1f})", z2 > z1 and z3 < z2)
    hold("s", 90)
    x4, z4 = p1_pos(p1)
    check(f"S drives back (x {x3:.1f} -> {x4:.1f})", x4 < x3)
else:
    check("p1 address logged", False)

perf = re.findall(r"rotoball I PERF frame=\d+ fps=([\d.]+)", console()[boot:])
if perf:
    print("  INFO fps (PERF lines):", ", ".join(perf[-4:]))

# the rest of the 90-second match, fast-forwarded
call("/emu/fastforward", "POST", on=1)
over = wait_for("match over", 7200, boot, step=300)
call("/emu/fastforward", "POST", on=0)
m = re.findall(r"match over score=(\d+):(\d+)", console()[boot:])
check("the match runs to the end (match over)", over, console()[-400:])
if m:
    print(f"  INFO final score {m[-1][0]}:{m[-1][1]} (P1 idle after the key checks; CPU plays on)")
run(30)
shot("match_over")

mark = len(console())
call("/input/key", "POST", key="esc", action="down")
run(10)
call("/input/key", "POST", key="esc", action="up")
check("Esc quits", wait_for("shutting down", 300, mark))
print("FAIL" if failures else "PASS", f"({len(failures)} failure(s))")
sys.exit(1 if failures else 0)
