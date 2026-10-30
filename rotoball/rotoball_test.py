#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""Rotoball (Falcon port) test through the Hatari agent API.

Resets the emulator (the game autostarts on its kick-off screen), checks
that play is frozen until SPACE starts the match, that W/A/S/D move the
player's skimmer (its position read from emulated memory at the address
the game logs), plays the rest of the match with fast-forward and checks
that it lasts 90 emulated seconds, that play is frozen at full time, that
SPACE starts a new match, and that Esc quits. Saves screenshots.

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


def status_frame():
    return call("/status")["frame"]


def ball_pos(addr):
    return struct.unpack(">ll", bytes.fromhex(call("/mem", addr=addr, len=8)["hex"]))


# cold reset: Hatari autostarts the game again (make run set it up)
boot = len(console())
call("/emu/reset", "POST", type="cold")
check("kick-off screen (state=KICKOFF)", wait_for("rotoball I state=KICKOFF", 1800, boot))
con = console()[boot:]
p1 = int(re.findall(r"rotoball I SYMBOL p1 0x([0-9a-f]+)", con)[-1], 16)
ball = int(re.findall(r"rotoball I SYMBOL ball 0x([0-9a-f]+)", con)[-1], 16)
run(30)
shot("kickoff")

x0, z0 = p1_pos(p1)
hold("w", 60)
check("play is frozen on the kick-off screen", p1_pos(p1) == (x0, z0))

mark = len(console())
hold("space", 4)
check("SPACE starts the match (state=PLAYING)", wait_for("state=PLAYING", 120, mark))
start_frame = status_frame()
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

perf = re.findall(r"rotoball I PERF frame=\d+ fps=([\d.]+)", console()[boot:])
if perf:
    print("  INFO fps (PERF lines):", ", ".join(perf[-4:]))

# the rest of the 90-second match, fast-forwarded (P1 idle, the CPU plays)
call("/emu/fastforward", "POST", on=1)
over = wait_for("state=FULLTIME", 7200, mark, step=60)
call("/emu/fastforward", "POST", on=0)
secs = (status_frame() - start_frame) / 60.0      # VGA: 60 VBLs a second
check("the match runs to full time (state=FULLTIME)", over, console()[-400:])
check(f"the match lasts 90 s ({secs:.1f} emulated s, VBL-counted)", 88.0 <= secs <= 93.0)
m = re.findall(r"state=FULLTIME score=(\d+):(\d+)", console()[mark:])
tackles = console()[mark:].count("tackle:")
if m:
    print(f"  INFO final score {m[-1][0]}:{m[-1][1]}, {tackles} tackles in the match")
run(30)
shot("fulltime")

px, pz = p1_pos(p1)
b0 = ball_pos(ball)
hold("w", 100)
check("play is frozen at full time", p1_pos(p1) == (px, pz) and ball_pos(ball) == b0)

mark = len(console())
hold("space", 4)
check("SPACE starts a new match", wait_for("state=PLAYING score=0:0", 120, mark))
run(60)
shot("restart")

mark = len(console())
call("/input/key", "POST", key="esc", action="down")
run(10)
call("/input/key", "POST", key="esc", action="up")
check("Esc quits", wait_for("shutting down", 300, mark))
print("FAIL" if failures else "PASS", f"({len(failures)} failure(s))")
sys.exit(1 if failures else 0)
