#!/usr/bin/env python3
"""Uranus Lander autopilot: plays the ST port through the Hatari agent API.

A closed control loop running outside the emulator:
  1. advance exactly N frames: a breakpoint on the game's per-frame
     gfx_swap() + POST /debug/continue?wait=1 (pausing at an arbitrary
     instruction could catch the game mid-update)
  2. read the ship state from emulated RAM  (GET /mem; addresses come
     from the game's "URANUS SYMBOL ..." NatFeats lines)
  3. decide rotate/thrust and set the joystick  (POST /input/joystick)

Usage: autopilot.py [--levels N] [--step FRAMES] [--shots DIR]
Start the game first: make run (then it waits on the title screen).
"""
import argparse
import os
import struct
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "agent"))
from hatari_agent import Hatari, HatariError  # noqa: E402

FIX = 65536.0
STATES = ["TITLE", "PLAYING", "LANDED", "CRASHING", "GAMEOVER", "ENTER_NAME"]


class Lander:
    def __init__(self, h):
        self.h = h
        self.sym = {}
        text, _ = h.console(0)
        for line in text.replace("\r", "").split("\n"):
            parts = line.split()
            if parts[:2] == ["URANUS", "SYMBOL"]:
                self.sym[parts[2]] = int(parts[3], 16)
        if "ship" not in self.sym:
            raise HatariError("game symbols not found - is URANUS.PRG running with --natfeats on?")

    def word(self, name):
        return struct.unpack(">h", self.h.read_mem(self.sym[name], 2))[0]

    def ship(self):
        x, y, vx, vy, angle, fuel, alive, thrusting = struct.unpack(
            ">llllhhhh", self.h.read_mem(self.sym["ship"], 24))
        return dict(x=x / FIX, y=y / FIX, vx=vx / FIX, vy=vy / FIX,
                    angle=angle if angle < 128 else angle - 256,  # signed, 0 = up
                    fuel=fuel, alive=alive)

    def pads(self):
        n = self.word("num_pads")
        raw = self.h.read_mem(self.sym["pads"], 8 * n)
        return [dict(zip(("x", "width", "y", "mult"), struct.unpack(">hhhh", raw[i * 8:i * 8 + 8])))
                for i in range(n)]

    def terrain(self):
        return struct.unpack(">320h", self.h.read_mem(self.sym["terrain_y"], 640))

    def state(self):
        return STATES[self.word("state")]


def clamp(v, lo, hi):
    return max(lo, min(hi, v))


def choose_pad(pads, x):
    # best multiplier, then nearest
    return max(pads, key=lambda p: (p["mult"], -abs(p["x"] + p["width"] / 2 - x)))


def control(s, pad, terrain):
    """returns (rotate, thrust): rotate -1 left / 0 / +1 right"""
    cx = pad["x"] + pad["width"] / 2
    err = cx - s["x"]
    over_pad = abs(err) < pad["width"] / 2 - 3
    # altitude above the higher of terrain under ship and pad (ship bottom is y+6)
    ground = min(terrain[int(clamp(s["x"], 0, 319))], pad["y"])
    alt = ground - (s["y"] + 6)

    # horizontal: wanted velocity towards pad centre, tilt to get there
    vx_want = clamp(err * 0.025, -0.9, 0.9)
    dv = vx_want - s["vx"]
    if alt < 18 and over_pad:
        angle_want = 0                          # upright for touchdown
    else:
        angle_want = clamp(round(dv * 40), -24, 24)

    # vertical: descent speed limit shrinks with altitude; hover if not over pad
    vy_want = clamp(0.25 + alt * 0.012, 0.25, 1.4)
    if not over_pad:
        vy_want = min(vy_want, (alt - 35) * 0.02)   # keep ~35px of clearance
    thrust = s["vy"] > vy_want
    if abs(dv) > 0.15 and abs(s["angle"]) > 4 and s["vy"] > -0.4 and alt > 20:
        thrust = True                           # use tilted thrust to move sideways
    if s["vy"] < -0.6:
        thrust = False

    rot = 0
    if s["angle"] < angle_want - 1:
        rot = 1
    elif s["angle"] > angle_want + 1:
        rot = -1
    return rot, thrust


def joystick(h, rot, thrust):
    dirs = [d for d, on in (("left", rot < 0), ("right", rot > 0), ("fire", thrust)) if on]
    h.joystick(",".join(dirs) or "none")


def frames(h, n):
    """advance n frames, stopping at the frame_sync breakpoint"""
    for _ in range(n):
        r = h.cont(wait=True, timeout_ms=5000)
        if not r.get("stopped"):
            raise HatariError("frame sync breakpoint not reached")


def wait_state(lander, h, want, max_frames=600):
    for _ in range(max_frames // 10):
        if lander.state() in want:
            return True
        frames(h, 10)
    return False


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--levels", type=int, default=3, help="levels to play")
    ap.add_argument("--step", type=int, default=2, help="frames per control tick")
    ap.add_argument("--shots", help="save a screenshot per landing/crash into this dir")
    args = ap.parse_args()

    h = Hatari()
    lander = Lander(h)
    print(f"symbols: ship=${lander.sym['ship']:06x} pads=${lander.sym['pads']:06x}")
    h.clear_breakpoints()
    h.add_breakpoint(addr=hex(lander.sym["frame_sync"]))
    h.debug_break()
    frames(h, 1)

    if lander.state() == "TITLE":
        h.joystick("fire")
        frames(h, 3)
        h.joystick("none")
        wait_state(lander, h, ("PLAYING",))

    landed = crashed = 0
    t0 = time.time()
    while landed < args.levels:
        state = lander.state()
        if state in ("GAMEOVER", "ENTER_NAME", "TITLE"):
            print("game over")
            break
        if state != "PLAYING":
            joystick(h, 0, False)
            frames(h, 10)
            continue

        pads = lander.pads()
        terrain = lander.terrain()
        level = lander.word("level")
        s = lander.ship()
        pad = choose_pad(pads, s["x"])
        print(f"level {level}: target x{pad['mult']} pad at x={pad['x']}..{pad['x'] + pad['width'] - 1}"
              f" y={pad['y']}, fuel {s['fuel']}")

        ticks = 0
        while lander.state() == "PLAYING":
            s = lander.ship()
            rot, thrust = control(s, pad, terrain)
            joystick(h, rot, thrust)
            frames(h, args.step)
            ticks += 1
            if ticks % 25 == 0:
                print(f"   x={s['x']:6.1f} y={s['y']:6.1f} vx={s['vx']:+.2f} vy={s['vy']:+.2f}"
                      f" angle={s['angle']:+3d} fuel={s['fuel']}")
        joystick(h, 0, False)
        result = lander.state()
        score = struct.unpack(">l", h.read_mem(lander.sym["score"], 4))[0]
        print(f"   -> {result} after {ticks * args.step} frames, score {score},"
              f" vx={s['vx']:+.2f} vy={s['vy']:+.2f} angle={s['angle']:+d}")
        if args.shots:
            os.makedirs(args.shots, exist_ok=True)
            frames(h, 3)
            with open(os.path.join(args.shots, f"level{level}-{result.lower()}.png"), "wb") as fh:
                fh.write(h.screen())
        if result == "LANDED":
            landed += 1
            wait_state(lander, h, ("PLAYING",))   # next level starts by itself
        else:
            crashed += 1
            wait_state(lander, h, ("PLAYING", "GAMEOVER", "ENTER_NAME"))

    h.clear_breakpoints()
    h.cont()
    print(f"landed {landed}, crashed {crashed}, {time.time() - t0:.1f}s wall time")
    return 0 if crashed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
