#!/usr/bin/env python3
"""Scripted interactions with the INTERACT app through the Hatari agent API.

Run with a text menu:      python3 scenarios.py
Run one scenario:          python3 scenarios.py house
The GUI (tools/agent/hatari_gui.py) offers the same list in its
Scenarios menu.  Start the app first: tests/start.sh (or make run).
"""
import math
import os
import re
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "agent"))
from hatari_agent import Hatari, HatariError  # noqa: E402


class App:
    """Helpers for talking to INTERACT via its NatFeats console protocol"""

    def __init__(self, h, log=print):
        self.h = h
        self.log = log
        self.pos = 0

    def lines(self, since=0):
        text, _ = self.h.console(since)
        return [l.strip() for l in text.replace("\r", "").split("\n") if l.startswith("INTERACT ")]

    def mark(self):
        _, self.pos = self.h.console(10**12)

    def wait_for(self, pattern, timeout=10):
        end = time.time() + timeout
        while time.time() < end:
            for line in self.lines(self.pos):
                if re.search(pattern, line):
                    return line
            time.sleep(0.15)
        raise HatariError(f"timeout waiting for /{pattern}/")

    def layout(self, name):
        for line in reversed(self.lines()):
            parts = line.split()
            if parts[1:3] == ["LAYOUT", name]:
                return tuple(int(v) for v in parts[3:7])
        raise HatariError(f"no layout for {name} (is INTERACT running?)")

    def center(self, name):
        x, y, w, h = self.layout(name)
        return x + w // 2, y + h // 2

    def symbol(self, name):
        for line in reversed(self.lines()):
            parts = line.split()
            if parts[1:3] == ["SYMBOL", name]:
                return parts[3]
        raise HatariError(f"no symbol {name}")

    def press(self, name):
        self.mark()
        self.h.click(*self.center(name))
        return self.wait_for(rf"CLICK {name}")

    def stroke(self, points):
        """drag with left button through canvas-relative points"""
        cx, cy, _, _ = self.layout("canvas")
        x, y = points[0]
        self.h.mouse(cx + x, cy + y, frames=2)
        self.h.button("down")
        self.h.run(2, pause=False)
        for x, y in points[1:]:
            self.h.mouse(cx + x, cy + y, frames=1)
        self.h.button("up")
        self.h.run(3, pause=False)


# --- scenarios -------------------------------------------------------------

def house(app):
    """Paint a house with a door and a sun"""
    app.press("Clear")
    app.press("Blue")
    app.stroke([(40, 100), (40, 60), (100, 60), (100, 100), (40, 100)])
    app.stroke([(35, 63), (70, 25), (105, 63)])
    app.press("Red")
    app.stroke([(62, 100), (62, 80), (78, 80), (78, 100)])
    app.press("Green")
    app.stroke([(150 + 14 * math.cos(a / 8 * math.tau), 38 + 14 * math.sin(a / 8 * math.tau))
                for a in range(9)])
    app.log("house painted")


def smiley(app):
    """Paint a smiley face"""
    app.press("Clear")
    app.press("Blue")
    cx, cy, r = 90, 60, 40
    app.stroke([(cx + r * math.cos(a / 16 * math.tau), cy + r * math.sin(a / 16 * math.tau))
                for a in range(17)])
    app.press("Red")
    for ex in (cx - 14, cx + 14):
        app.stroke([(ex - 3, cy - 12), (ex + 3, cy - 12), (ex + 3, cy - 6), (ex - 3, cy - 6), (ex - 3, cy - 12)])
    app.press("Green")
    app.stroke([(cx + 24 * math.cos(a / 12 * math.pi), cy + 6 + 18 * math.sin(a / 12 * math.pi))
                for a in range(1, 12)])
    app.log("smiley painted")


def buttons(app):
    """Click the color buttons, one per second"""
    for name in ("Red", "Green", "Blue", "Red", "Green", "Blue"):
        app.log(app.press(name))
        app.h.run(30, pause=False)


def greeting(app):
    """Type a line with a typo, fix it with Backspace, submit"""
    app.mark()
    app.h.type_text("Hello from Pythn")
    app.h.key("backspace")                  # remove the stray 'n'
    app.h.type_text("on!")
    app.h.run(20, pause=False)
    app.h.key("return")
    app.log(app.wait_for(r"TEXT "))


def joystick_dance(app):
    """Drive the ball in a square, then fire"""
    for dirs in ("right", "down", "left", "up"):
        app.mark()
        app.h.joystick(dirs, frames=30)
        app.log(app.wait_for(r"JOY 00 "))
    app.mark()
    app.h.joystick("fire", frames=10)
    app.log(app.wait_for(r"FIRE"))


def breakpoint(app):
    """Break on on_button(), inspect the argument, step, continue"""
    addr = app.symbol("on_button")
    app.h.clear_breakpoints()
    app.h.add_breakpoint(addr=addr)
    app.log(f"breakpoint on on_button() at {addr}; clicking Blue")
    x, y = app.center("Blue")
    import threading
    t = threading.Thread(target=lambda: app.h.click(x, y))
    t.start()
    stop = app.h.wait_stop(10000)
    if not stop.get("stopped"):
        raise HatariError("breakpoint not hit")
    sp = int(stop["regs"]["a7"], 16)
    arg = int.from_bytes(app.h.read_mem(sp + 4, 4), "big")
    app.log(f"stopped at {stop['pc']}, button id argument = {arg}")
    for line in stop["disasm"]:
        app.log("   " + line["text"])
    for _ in range(3):
        s = app.h.step()
        instr = re.split(r"\s{2,}", s["disasm"][0]["text"])[-1]  # after address/hexdump
        app.log(f"step -> {s['pc']}: {instr}")
    app.h.clear_breakpoints()
    app.h.cont()
    t.join()
    app.log("continued: " + app.wait_for(r"CLICK Blue"))


def snapshot(app):
    """Save state, scribble, restore"""
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "tests", "out", "scenario.sav")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    app.h.save_state(path)
    app.log(f"saved {path}; scribbling...")
    app.press("Red")
    app.stroke([(10, 10), (170, 110), (10, 110), (170, 10)])
    app.h.run(25, pause=False)
    app.log("restoring")
    app.h.load_state(path)
    app.log("restored, scribble is gone")


SCENARIOS = [house, smiley, buttons, greeting, joystick_dance, breakpoint, snapshot]


def run(name, h=None, log=print):
    fn = {f.__name__: f for f in SCENARIOS}[name]
    app = App(h or Hatari(), log)
    app.lines()  # raises if emulator is down
    fn(app)


def main():
    h = Hatari()
    if not h.alive():
        sys.exit("No emulator on :7777 - start it with tests/start.sh")
    if len(sys.argv) > 1:
        for name in sys.argv[1:]:
            run(name, h)
        return
    while True:
        print("\nINTERACT scenarios:")
        for i, f in enumerate(SCENARIOS, 1):
            print(f"  {i}. {f.__name__:15} {f.__doc__}")
        print("  q. quit")
        choice = input("> ").strip()
        if choice.lower() == "q":
            break
        try:
            run(SCENARIOS[int(choice) - 1].__name__, h)
        except (ValueError, IndexError):
            print("?")
        except HatariError as err:
            print("ERROR:", err)


if __name__ == "__main__":
    main()
