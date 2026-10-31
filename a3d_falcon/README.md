# amiga3d on the Falcon030

The Atari Falcon030 platform layer for the amiga3d engine games from
[amiga_games](https://github.com/geekychris/amiga_games/tree/main/amiga3d):
the counterpart of the engine's `main_68k.c` (classic Amiga) and
`main_os4.c` (AmigaOS 4), with a drawing backend, sound and the bridge
calls for the Falcon. The games build on it from their own, unchanged
sources (ScAIry Fun's HUD cache has a third slot, for the third screen
buffer).

| Game | Stock Falcon030 (16 MHz) | 68060 at 32 MHz |
|---|---|---|
| [Block Breaker 3D](../block_breaker_3d) | 6.6-6.7 fps | 25 |
| [Marble Tilt](../marble_tilt) | 6.0-6.1 | 25 |
| [Harbour Crane](../harbour_crane) | 6.4-6.6 | 25 |
| [Tank Duel](../tank_duel) | 6.1-7.5 | 25 |
| [Orbit Docking](../orbit_docking) | 7.4-8.7 | 25 |
| [Stack Tower](../stack_tower) | 3.0 | 25 |
| [Ridge Runner](../ridge_runner) | 4.0-4.6 | 25 |
| [Drone Courier](../drone_courier) | 9.2-11.1 | 25 |
| [Canyon Rail](../canyon_rail) | 6.9-7.5 | 25 |
| [Lander 3D](../lander_3d) | 1.3 (and slow motion) | 25 |
| [Boing Pinball](../boing_pinball) | 12.3-14.6 | 25 |
| [ScAIry Fun](../scairy_fun) | 2.9-3.0 | 25 |
| [Ace Pilot 2](../ace_pilot2) | 4-8 | 25 |

Frames a second with each game's autopilot playing, in Hatari; 25 is the
cap, the Amiga versions' rate. The games run their simulations at 25
steps a second from the clock, so on a stock Falcon they keep time and
only the drawing is jerky, except Lander 3D, whose simulation alone is
slower than real time there. They are all set up for an accelerated
Falcon in `../games.ini` (`--cpulevel 6 --cpuclock 32 --addr24 off
--ttram 64`, as Rolling Steel and Planet Chomp). Each is one program for
both machines.

## What a game needs

The amiga_games sources as they are (`tools/port_game.py GAME PRG TPRG`
copies them, with its music and voice, and writes the Makefile), and a
Makefile that sets what the engine's `a3d.mk` asks for plus the 8.3 names:

```make
NAME    = block_breaker_3d
PRG     = BBREAK3D          # build/BBREAK3D.PRG
TPRG    = BBTEST            # build/obj/test/BBTEST.PRG: the host tests' cases
CORE    = bb_core.c
GAME    = game.c render.c tests/test_cases.c
TESTS   = tests/test_cases.c
HEADERS = bb.h breaker.h tests/vectors.h
DATA    = some.mod          # optional: build/DATA/SOME.MOD
TRACE   = tests/trace_main.c   # optional: make trace
include ../a3d_falcon/a3d.mk
```

`make`, `make run`, `make test` (the tests in Hatari: they must all pass,
the long autopilot trace hashes included), `make trace` (a trace program
in Hatari diffed with the host build's output), `make host-test`.

## The files

| | |
|---|---|
| `a3d.h`, `a3d_math.c`, `a3d_fix.c`, `a3d_plat.h`, `a3d_sound.h`, `test_host.c` | the engine's, unchanged but for `#ifdef A3_FALCON` parts: `FMUL`, `FDIV` and `a3_angle` use the 68030's 64-bit `muls.l` / `divs.l` when the CPU has them (`a3f_mul64`, from the `_CPU` cookie), the same results either way |
| `a3d_draw.h`, `a3d_draw.c` | the engine's drawing: the 2D, the 3D draw list and the models unchanged; the Falcon's `Canvas`, span filler, text and tall picture in place of the Amiga's bitplanes |
| `a3d_rows.S` | the polygon fill and the scan conversion in 68030 assembly |
| `a3d_sound.c` | the engine's effects and voice clips, with the C library's files and memory |
| `falcon_sys.h`, `falcon_sys.c` | the machine layer, shared with [Terra Strike](../terra_strike) (its own engine): the screen and its row map, the palette, the pens' plane patterns and rectangles, the TOS font and text, IKBD keys (ST scancodes to the Amiga raw keys the games map) and joystick, the timers and frame pacing, the games' files, ptplayer's music |
| `main_falcon.c` | the engine's platform side on it: `a3d_plat.h`'s calls, the sky pens, the effects, the main loop |
| `bridge.c`, `compat/bridge_client.h` | the AmigaBridge client: logging through NatFeats, the variables and hooks through a mailbox in memory |
| `test_falcon.c`, `trace_falcon.c` | a game's tests, and its trace program, on the Falcon |
| `tools/a3d_agent.py` | drives the games in Hatari through the agent API: run, test, trace, `cmd` (the mailbox), keys, screenshots, frame rates |
| `tools/a3d_e2e.py` | the end-to-end check each game's README reports (Terra Strike's too) |
| `tools/port_game.py` | copies a game in from amiga_games |

## The screen

The Falcon's 256-colour mode (320 x 240 on VGA, 320 x 200 on RGB), triple
buffered so drawing never waits for the display. Its eight bitplanes are
interleaved a word at a time, and the games use pens 0-31 plus 32-47 for
the sky, so a span writes three longs (planes 0-5) for each 16 pixels and
planes 6 and 7 stay clear. That is a third of the stores a 16-bit
true-colour screen would take, and a full-screen clear is about 10 ms.

The games draw on 320 x 256 as on the Amiga. Each canvas has a table of
row offsets; the 16 rows that don't fit the 240-line screen are -1 and
not drawn, except in text, where a dropped glyph row is drawn onto the
screen row it shares with the next (without that a 0 lost its base and
read as an A). `plat_sky`, the copper gradient on the Amiga, becomes 16
pens shaded from the top colour to the bottom, chosen per row when the
sky's pen is drawn.

## Speed

A 16 MHz 68030 reading and writing ST-RAM over a 16-bit bus does about
2.7 million instructions a second. What it took:

- **The polygon fill** (`a3d_rows.S`). In C, the row loop with a call to
  `g_span` per row came to more than the 68030's 256-byte instruction
  cache, so every row missed it; the assembly fill keeps everything in
  registers and fits, and `poly_spans` is assembly too. Rectangles go
  through it as well. Block Breaker 3D went from 2.5 to 6.7 fps.
- **Triple buffering**, so the drawing never waits for a VBL (6%).
- **64-bit arithmetic.** The build is 68020-68060 code, which turns the
  fixed-point multiply into a libgcc call; Lander 3D spent 55% of its
  11 s level start there. `FMUL` and `FDIV` now use the 030's own 64-bit
  forms (the level starts in 4 s), and the depth-bucket divide is 32 bits
  when it fits. The test suites pass on both paths.

The rest is the games' own work (their simulations, transforms and
overdraw), so 20 frames a second on a stock Falcon would take rewriting
the games, not the platform. With a 68060 every game reaches the 25 fps
cap.

## Tested

For every game: `make test` passes on the 68030 and the 68060 (the trace
hashes in `tests/vectors.h`, or for Lander 3D and ScAIry Fun `make trace`,
identical to the host's); in Hatari every key reaches the game, the start
key and Esc work, and the autopilot plays (`docs/` screenshots); music
and voices load (Stack Tower's music was captured from the DMA sound to
check it plays). Launched through `../games.ini` with the host launcher.

## Not done

- The OS 4 versions' full-colour pictures (`g_image_*`): as on the classic
  Amiga, `g_image_load` gives NULL and the games keep their 32-colour look.
- The Falcon's DSP is not used.
- Stock-Falcon speed: see above.
