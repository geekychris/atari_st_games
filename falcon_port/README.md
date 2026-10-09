# Falcon port layer

Shared code for porting Amiga AGA games to the Atari Falcon030. It is used
by [../fractalus](../fractalus), [../void_trader](../void_trader) and [../ace_pilot](../ace_pilot), and its
screen code by the 3DO-derived games through [../falcon_3do](../falcon_3do). The ST equivalent is [../st_port](../st_port),
whose IKBD input, NatFeats and `amiga_types.h` are reused here.

| File | |
|---|---|
| `falcon.mk` | Build and run rules. A port sets `NAME` and `SRCS` (C and C++), then includes it. `make run` starts a Falcon (14 MB, VGA, no DSP) on EmuTOS 1024k with the build directory as C: and autostart. |
| `fgfx.c/.h` | 16-bit true-colour screens (320x240 VGA, 320x200 RGB), triple buffered. Provides the graphics.library subset (`SetAPen`, `Move`, `Draw`, `RectFill`, `WritePixel`, `Text`, `SetRast`, `SetDrMd`), with 256 pens mapped through an RGB565 table. `AreaMove`/`AreaDraw`/`AreaEnd` fill convex polygons, with no `TmpRas`/`AreaInfo` setup needed. The game keeps its 256-line Amiga coordinates, and Y is scaled to the screen. Also strip fills for raycasters. |
| `../st_port/paula.c/.h` | (shared) Paula on DMA sound, see the ST port layer. Amiga code writing `custom.dmacon` / `custom.aud[]` works unchanged when compiled as C++. Four channels are mixed into an 8-bit stereo ring at 9834 Hz from the VBL. An optional player tick runs at a fixed rate. |
| `abstub.c`, `compat/bridge_client.h` | The amiga_games bridge client mapped to NatFeats lines: `AB_I` logging with the name given to `ab_init` as prefix (`FRACTALUS` by default); variable and hook registration are no-ops. |
| `compat/` | Amiga header shims (exec, graphics, intuition, dos, hardware). |

Toolchain notes:
- Code is compiled `-m68030 -msoft-float` and linked against the 68000
  libraries, so programs run on Falcons without an FPU.
- C++ is built with `-fno-exceptions -fno-rtti`.
