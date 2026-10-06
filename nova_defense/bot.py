#!/usr/bin/env python3
"""Nova Defense bot: plays the ST port through the Hatari agent API.

Each tick (frame-synchronised by a breakpoint on the game's gfx_swap):
read the swarm, bullets and player from emulated RAM (addresses from the
game's "NOVA SYMBOL" lines), pick the lowest alien nearest to the player,
move under it, fire when lined up, and sidestep alien bullets.

Usage: bot.py [--seconds N] [--step FRAMES]
"""
import argparse
import os
import struct
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "agent"))
from hatari_agent import Hatari, HatariError  # noqa: E402

ROWS, COLS, CELL_W, CELL_H, ALIEN_W = 5, 11, 24, 16, 12  # collision box
PLAYER_W, PLAYER_Y = 15, 232
STATES = ["TITLE", "PLAYING", "DYING", "GAMEOVER", "WAVE_CLEAR"]


class Nova:
    def __init__(self, h):
        self.h = h
        self.sym = {}
        text, _ = h.console(0)
        for line in text.replace("\r", "").split("\n"):
            p = line.split()
            if p[:2] == ["NOVA", "SYMBOL"]:
                self.sym[p[2]] = int(p[3], 0)
        if "alive" not in self.sym:
            raise HatariError("NOVA symbols not found (run with --natfeats on)")
        self.boolsz = self.sym.get("sizeof_bool", 2)

    def w(self, name, off=0):
        return struct.unpack(">h", self.h.read_mem(self.sym[name] + off, 2))[0]

    def l(self, name):
        return struct.unpack(">l", self.h.read_mem(self.sym[name], 4))[0]

    def snapshot(self):
        b = self.boolsz
        alive_raw = self.h.read_mem(self.sym["alive"], ROWS * COLS * b)
        alive = [[alive_raw[(r * COLS + c) * b + b - 1] != 0 for c in range(COLS)] for r in range(ROWS)]
        gx, gy = self.w("grid_x"), self.w("grid_y")
        pb = struct.unpack(">hhh", self.h.read_mem(self.sym["player_bullet"], 6))
        ab = self.h.read_mem(self.sym["alien_bullets"], 18)
        bullets = [struct.unpack(">hhh", ab[i * 6:i * 6 + 6]) for i in range(3)]
        return dict(state=STATES[self.w("state")], px=self.w("player_x"), alive=alive,
                    gx=gx, gy=gy, dir=self.w("swarm_dir"), pbullet=pb[2] != 0,
                    abullets=[(x, y) for x, y, a in bullets if a], score=self.l("score"),
                    lives=self.w("lives"), wave=self.w("wave"))


def decide(s):
    pc = s["px"] + PLAYER_W // 2
    # lowest alive alien per column
    targets = []
    for c in range(COLS):
        for r in range(ROWS - 1, -1, -1):
            if s["alive"][r][c]:
                ax = s["gx"] + c * CELL_W + ALIEN_W // 2 + s["dir"] * 3   # small lead
                targets.append((abs(ax - pc), ax))
                break
    move, fire = 0, False
    if targets:
        _, tx = min(targets)
        if tx > pc + 2:
            move = 1
        elif tx < pc - 2:
            move = -1
        fire = abs(tx - pc) <= 5 and not s["pbullet"]
    # dodge: bullet coming down near us
    for bx, by in s["abullets"]:
        if PLAYER_Y - 60 < by < PLAYER_Y + 4 and abs(bx - pc) < 12:
            move = -1 if bx >= pc else 1
            if s["px"] < 20:
                move = 1
            elif s["px"] > 290:
                move = -1
    return move, fire


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--seconds", type=float, default=60, help="wall time to play")
    ap.add_argument("--step", type=int, default=2, help="frames per tick")
    args = ap.parse_args()

    h = Hatari()
    nova = Nova(h)
    h.clear_breakpoints()
    h.add_breakpoint(addr=hex(nova.sym["frame_sync"]))
    h.debug_break()

    def frames(n):
        for _ in range(n):
            if not h.cont(wait=True, timeout_ms=10000).get("stopped"):
                raise HatariError("frame sync lost")

    t0 = time.time()
    last = None
    while time.time() - t0 < args.seconds:
        s = nova.snapshot()
        if s["state"] in ("TITLE", "GAMEOVER"):
            if s["state"] == "GAMEOVER" and last and last["state"] != "GAMEOVER":
                print(f"game over: score {s['score']} wave {s['wave']}")
            h.joystick("fire")
            frames(3)
            h.joystick("none")
            frames(5)
        else:
            move, fire = decide(s)
            dirs = [d for d, on in (("left", move < 0), ("right", move > 0), ("fire", fire)) if on]
            h.joystick(",".join(dirs) or "none")
            frames(args.step)
        if last and (s["wave"] != last["wave"] or s["lives"] != last["lives"]):
            print(f"wave {s['wave']} lives {s['lives']} score {s['score']}")
        last = s
    h.joystick("none")
    h.clear_breakpoints()
    h.cont()
    s = nova.snapshot()
    print(f"end: state {s['state']} score {s['score']} wave {s['wave']} lives {s['lives']}")


if __name__ == "__main__":
    main()
