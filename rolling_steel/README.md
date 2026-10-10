# Rolling Steel: Atari Falcon030 (CT60) port

Port of Rolling Steel from
[geekychris/amiga_games](https://github.com/geekychris/amiga_games) (`rolling_steel/`,
commit in `.upstream-commit`; MIT, see `LICENSE.original`). It's an isometric
roll-a-marble-downhill game in the spirit of the 1984 Atari cabinet: six floating courses
and one clock. It runs on the Falcon layer in [../falcon_3do](../falcon_3do).

**It needs an accelerated Falcon.** The 3D is rasterised in software with a
depth buffer (`softcel.c`), sized for an Amiga 68060. As on the Amiga, with a depth buffer, a stock
16 MHz Falcon030 manages 1–2 fps. With the port's fast renderer (below) it
manages 3.5–4.8 fps, still too slow to play. An emulated CT60-style Falcon
(68060 at 32 MHz with TT-RAM) runs it at 31–39 fps.

## Screenshots

<table><tr>
<td align="center"><img src="docs/title.png" width="320" alt="Title"><br>Title (demo behind)</td>
<td align="center"><img src="docs/play.png" width="320" alt="Practice course"><br>Practice course</td>
</tr></table>

Captured through the agent API (`/screen`, 2x).

```sh
make CROSS=~/computers/atari-cc/opt/cross-mint/bin/m68k-atari-mintelf-
make run CROSS=... HATARI_OPTS="--cpulevel 6 --cpuclock 32 --addr24 off --ttram 64"
```

Controls, as on the Amiga:
- Cursor keys, keypad 8/4/6/2 or joystick 1: push the marble (up is away from you).
- Z/X: turn the view. =/- or keypad +/-: zoom. C with up/down: tilt.
- F (or F10): cycle the display modes (see below).
- P: pause. Esc: quit.
- On the title: up/down picks the starting course, left/right picks one or two players
  (player 2 uses WASD, Q/E and Tab), C turns the music on or off, and Space starts.

## What changed

| | |
|---|---|
| `game.c`, `phys.c`, `course.c`, `sound_paula.c`, the headers | **Unmodified.** |
| `softcel.c` | The depth-buffer clear uses `movem` (32 bytes per instruction) on a 68020 or later under `__MINT__`; with `SOFTCEL_RGB565` its blends are `fastcel.c`'s. |
| `glcels_soft.c` | Under `__MINT__`: the four display modes, colours and textures converted to RGB565. |
| `fastcel.c`, `fastcel.h` | New: the fast renderer. |
| `render.c` | Under `__MINT__`: one far-to-near pass in the fast mode. |
| `main_68k.c` | Under `__MINT__`: draws into the screen in RGB565 (`sys_rgb565`, the pens converted), with a direct HUD text plotter. |
| `amiga68k.h` | Declares `sys_modes`, `sys_mode`, `sys_mode_names`, `sys_rgb565` and `sys_cpu`. |
| `data/` | The six courses, music themes and effects built by the 3DO version's tools (unchanged from upstream). |

## Display modes (F)

F (F10 too: the keys that switch window and screen on the Amiga) cycles
four ways of drawing the 3D:

| fps in play | 68060 @ 32 MHz + TT-RAM | stock 16 MHz Falcon030 |
|---|---|---|
| **fast** (the default) | 31–39 | 3.5–4.8 |
| fast, half resolution | 43 | 3.7–4.8 |
| depth buffer (as on the Amiga) | 13–14 | 1.4–1.7 |
| depth buffer, half resolution | 19–22 | 1.7–2.1 |

**Fast** (`fastcel.c`) draws the way 1980s 3D games got their speed:
- **No depth buffer.** `render.c` already sorts faces far to near, as the 3DO
  did, so nearer faces just cover farther ones; shadows and the ghost go into
  that same order (one pass). The cost: where two track pieces meet, a
  rail's side face can show over the deck as a small dark notch (the
  glitch the Amiga version added a depth buffer to fix).
- **32-bit maths only.** One `divs.l` per edge, none per pixel. Each quad is
  filled by two edge steppers walking its sides.
- **Solid rows in assembly**, small enough for the 68030's 256-byte
  instruction cache. Spans are `move.l` loops, darkened HUD boxes go two
  pixels per longword, and the HUD text is plotted directly.
- **No conversion pass.** The game draws straight into the screen in RGB565
  (`sys_rgb565`); `softcel.c` is built with `SOFTCEL_RGB565` for the
  depth-buffer modes.

**Half resolution** draws the 3D at 160×120 and doubles it, with the HUD
still sharp.

On a stock Falcon, about a fifth of the frame is still the physics. It
runs at most 6 of the game's 50 Hz steps per frame, so below about 8 fps
the game runs slower than real time.

## Agent hooks

The bridge client logs as `ROLL`. Every 5 s, `ROLL I fps=... ms/frame logic=... draw=... hud+blit=...`.
