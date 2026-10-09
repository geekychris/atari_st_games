# Rolling Steel: Atari Falcon030 (CT60) port

Port of Rolling Steel from
[geekychris/amiga_games](https://github.com/geekychris/amiga_games) (`rolling_steel/`,
commit in `.upstream-commit`; MIT, see `LICENSE.original`). It's an isometric
roll-a-marble-downhill game in the spirit of the 1984 Atari cabinet: six floating courses
and one clock. It runs on the Falcon layer in [../falcon_3do](../falcon_3do).

**It needs an accelerated Falcon.** The 3D is rasterised in software with a
depth buffer (`softcel.c`), sized for an Amiga 68060. A stock 16 MHz Falcon030
manages 1–2 fps. On an emulated CT60-style Falcon (68060 at 32 MHz with
TT-RAM) it runs at 10–15 fps.

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
- P: pause. Esc: quit.
- On the title: up/down picks the starting course, left/right picks one or two players
  (player 2 uses WASD, Q/E and Tab), C turns the music on or off, and Space starts.

## What changed

| | |
|---|---|
| everything except `softcel.c` | **Unmodified**, including `main_68k.c` (on `../falcon_3do`). |
| `softcel.c` | The depth-buffer clear uses `movem` (32 bytes per instruction) on a 68020 or later under `__MINT__`. |
| `data/` | The six courses, music themes and effects built by the 3DO version's tools (unchanged from upstream). |

The frame is 320×240 15-bit RGB, converted to RGB565 by `sys3do.c`.

## Agent hooks

The bridge client logs as `ROLL`. Every 5 s, `ROLL I fps=... ms/frame logic=... draw=... hud+blit=...`.
