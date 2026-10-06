# INTERACT: driving an Atari app through the agent API

This example shows the Hatari agent API end to end on a real GEM program:
input injection, screenshots and pixel checks, reading program output,
the 68k debugger and snapshots. It comes in three layers:

| | |
|---|---|
| `interact.c` | Atari-side test target (C, GEM): buttons, a text field, a paint canvas and a joystick-driven ball. Reports every event through NatFeats. |
| `tests/*.sh` | One shell script per API feature, each checking its result (17 checks). |
| `scenarios.py` + `tools/agent/hatari_gui.py` | Python: scripted demos (text menu or CLI) and a Tk GUI with a live, clickable emulator screen. |

```
┌───────────────────────────────────────────┐
│ Red │ Green │ Blue │ Clear │ ███ swatch   │   CLICK <name>, COLOR <pen>
├─────────────────────────┬─────────────────┤
│ canvas (drag to paint)  │ Joystick  ●     │   PAINT ..., JOY <bits> BALL x,y, FIRE
├─────────────────────────┴─────────────────┤
│ Text: _                                    │   KEY <c>, TEXT <line>
│ Clicks:3 Segs:12 Joy:00 Mouse:158,33       │
└───────────────────────────────────────────┘
```

## The app's console protocol

Started with `--natfeats on`, INTERACT prints lines starting with
`INTERACT ` to the host, which the API serves at `GET /console`:

| Line | When |
|---|---|
| `SCREEN w h colors` | start |
| `LAYOUT <name> x y w h` | start and after window moves. Screen coordinates of `Red Green Blue Clear canvas arena text`. |
| `SYMBOL on_button 0x...` | start. Address of the button handler, for breakpoint demos. |
| `READY` | start-up report done |
| `CLICK <name>` / `COLOR <pen>` | button pressed |
| `KEY <c>` / `TEXT <line>` | key typed / Return pressed |
| `PAINT <points> x1,y1 x2,y2` | paint stroke finished |
| `JOY <bits> BALL x,y` / `FIRE color n` | joystick state changed / fire |
| `EXIT ...` | Esc or closer |

Self-describing apps are the key to robust agent tests. The app tells
the agent where its controls are and what happened, so tests don't
depend on reading pixels, while screenshots stay available for visual
checks.

## Build and run

```sh
../../tools/agent/fetch-cross-mint.sh          # once: m68k-atari-mintelf GCC + libs
make CROSS=~/computers/atari-cc/opt/cross-mint/bin/m68k-atari-mintelf-
tests/start.sh --restart                       # ST + EmuTOS, build/ as C:, autostart INTERACT
```

## Shell tests

`tests/run-all.sh` starts a fresh emulator and runs every test:

| Script | Shows |
|---|---|
| `10-screenshot.sh` | native vs. full-window screenshots, reading pixels |
| `20-buttons.sh` | clicks at coordinates from `LAYOUT`, verified by console and swatch pixel color |
| `30-typing.sh` | typing shifted text, Backspace, Return, raw scancodes |
| `40-paint.sh` | press/move/release drags to draw a house, counting colored pixels |
| `50-joystick.sh` | timed and latched joystick directions, fire |
| `60-debugger.sh` | breakpoint on `on_button()`, reading the C argument from the 68k stack, single-step, continue |
| `70-snapshot.sh` | save, change, restore, then compare screen region hashes |

`tests/lib.sh` has the helpers (`click_on Red`, `wait_for REGEX`,
`shot NAME`, `joy right 30`...). `tests/pngtool.py` reads PNG pixels with
the Python stdlib only. Screenshots land in `tests/out/`.

## Python

```sh
python3 scenarios.py                 # text menu
python3 scenarios.py smiley house    # run scenarios directly
python3 ../../tools/agent/hatari_gui.py
```

Scenarios: `house`, `smiley` (painting), `buttons`, `greeting` (typing with a
typo fix), `joystick_dance`, `breakpoint` (debugger), `snapshot`.

The GUI (Tk, stdlib only) shows the live screen at 1-3x zoom. Click, drag
(paints in INTERACT), right-click and type directly on it. It has tabs for
program console output, a joystick pad, a debugger (break/step/registers/
disassembly/breakpoints/commands) and machine setup (ROM list, floppy,
host folder as C:, options), plus menus for snapshots, screenshots, the
scenarios above and the test suite. `tools/agent/hatari_agent.py` is the
reusable client library both are built on.

## Hardware notes the tests uncovered

- TOS boots with IKBD joystick reporting off. Sending `0x14` (joystick
  events) turns the **mouse** off, so INTERACT sends `0x14, 0x08` to get
  both mouse and joystick-1 events (the classic game idiom).
- With the mouse on, the ST reports **joystick 1 fire as the right mouse
  button**, because they share a hardware line. INTERACT treats a right
  click as fire.
- The OS joystick vector receives a 3-byte buffer (header, joystick 0,
  joystick 1), so port 1 is at `2(a0)`. Found by breaking on the handler
  through the API and dumping memory at `a0`.
