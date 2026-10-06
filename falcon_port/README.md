# Falcon port layer

Shared code for porting Amiga AGA games to the Atari Falcon030. It is used
by [../fractalus](../fractalus). The ST equivalent is [../st_port](../st_port),
whose IKBD input, NatFeats and `amiga_types.h` are reused here.

| File | |
|---|---|
| `falcon.mk` | Build and run rules. A port sets `NAME` and `SRCS` (C and C++), then includes it. `make run` starts a Falcon (14 MB, VGA, no DSP) on EmuTOS 1024k with the build directory as C: and autostart. |
| `fgfx.c/.h` | 16-bit true-colour screens (320x240 VGA, 320x200 RGB), triple buffered. Provides the graphics.library subset (`SetAPen`, `Move`, `Draw`, `RectFill`, `WritePixel`, `Text`, `SetRast`, `SetDrMd`), with 256 pens mapped through an RGB565 table. The game keeps its 256-line Amiga coordinates, and Y is scaled to the screen. Also strip fills for raycasters. |
| `fpaula.cpp/.h` | Paula on DMA sound. Amiga code writing `custom.dmacon` / `custom.aud[]` works unchanged when compiled as C++. Four channels are mixed into an 8-bit stereo ring at 9834 Hz from the VBL. An optional player tick runs at a fixed rate. |
| `abstub.c`, `compat/bridge_client.h` | The amiga_games bridge client (`AB_I` logging, variable registration) mapped to NatFeats lines. |
| `compat/` | Amiga header shims (exec, graphics, intuition, dos, hardware). |

Toolchain notes:
- Code is compiled `-m68030 -msoft-float` and linked against the 68000
  libraries, so programs run on Falcons without an FPU.
- C++ is built with `-fno-exceptions -fno-rtti`.
