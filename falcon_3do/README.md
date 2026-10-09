# Falcon layer for the 3DO-derived ports

Shared code for [../spectral_keep](../spectral_keep), [../rolling_steel](../rolling_steel)
and [../planet_chomp](../planet_chomp). In
[geekychris/amiga_games](https://github.com/geekychris/amiga_games) these three are 3DO
games with a classic 68k AmigaOS build. That build's game code draws into a 15-bit RGB frame
(the 3DO's pixel format) and talks to the machine through two small files,
`amiga68k.c` (window, keyboard, timer, data) and `paula68k.c` (sound). This
layer provides those on the Falcon, so the games' own `main_68k.c` and
game code compile unchanged. Upstream's two files are kept in each game as
`*.amiga`.

| File | |
|---|---|
| `f3do.mk` | Build and run rules. A game's Makefile sets `NAME`, `SRCS` and optional `EXTRA_CFLAGS`, then includes it. `make run` starts a Falcon (14 MB, VGA, no DSP) on EmuTOS 1024k with `build/` as C: and autostart; add Hatari options with `HATARI_OPTS=...`. `data/` is copied to `build/DATA` with 8.3 names. |
| `sys3do.c` | The machine layer, implementing the `amiga68k.h` API. **Frame**: 15-bit RGB is converted to the Falcon's RGB565, two pixels per long, into the back screen of `../falcon_port/fgfx` (320×240 VGA triple buffer); a 256-line frame drops rows evenly. A game that draws RGB565 itself sets `sys_rgb565`, and `fb` is then the back screen (Spectral Keep does). **Keyboard**: IKBD key events become `IDCMP_RAWKEY` messages with Amiga key codes on the window's port, so the games' Intuition loops read them unchanged. **Joystick 1** as pad bits; **timer** from the 200 Hz system timer; **data** from `DATA\` beside the program; Topaz from the Line-A 8×8 font. Its `main()` enters supervisor mode once around the game's (built as `game_main`). |
| `paula.h` | The Paula API (`paula_open`, `paula_set_tick`, `paula_tick`, `custom`) on `../st_port/paula.c`: four channels mixed onto Falcon DMA sound. |
| `compat/` | Header stubs for the AmigaOS includes the games use (exec, dos, intuition, graphics, timer, `hardware/custom.h`), declared in `amiga3do_compat.h`. AmigaDOS `Open`/`Read`/`Seek` come from `../st_port/amiga_dos.c`. |

## Toolchain notes

- **`-m68020-60 -msoft-float`**, linked against the 68000 libraries. With
  `-m68030`, GCC uses the 64-bit `muls.l`/`divs.l` forms, which a 68060
  doesn't have (an accelerated Falcon crashed in `fgfx_init`). This code
  runs on both.
- **`-fno-omit-frame-pointer`**. Without a frame pointer, GCC 14.3 (m68k)
  miscompiles some 64-bit arguments. For
  `((long long)tw << 32) / w` in `softcel.c` it pushes the value with
  `subq.l #8,%sp; move.l N(%sp),(%sp)`, and N isn't adjusted for the
  `subq`, so the division reads a different local. Planet Chomp's sky then
  covered half the screen and its sprites were mis-scaled.
  `objdump` the code for that pattern if you change the flags.

## Speed

Spectral Keep runs on a stock 16 MHz Falcon030. Rolling Steel and Planet
Chomp rasterise 3D in software. The Amiga build targets a 68060, and a stock
Falcon manages 1–2 fps. In Hatari, emulate a CT60-style accelerated Falcon:

```sh
make run HATARI_OPTS="--cpulevel 6 --cpuclock 32 --addr24 off --ttram 64"
```

`--ttram` needs `--addr24 off`. `examples/games.ini` starts them this way.
