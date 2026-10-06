# GEMDEMO: VDI showcase for the Atari ST

A small GEM application in C that shows off the VDI, the Atari's graphics
device interface. One window with six panels:

| Panel | VDI features |
|---|---|
| Fills | `vsf_interior`/`vsf_style`: 18 fill patterns and 6 hatches, `v_bar` with perimeter |
| Shapes | `v_circle`, `v_pieslice`, `v_ellipse`, `v_rfbox` |
| Lines | `v_pline` with line styles and colours, wide lines with round ends (`vsl_width`, `vsl_ends`) |
| Text | `vst_effects`: bold, light, italic, underline, outline, shadow |
| Colors | the pen palette, `v_pmarker` polymarkers |
| XOR anim | rotating star drawn with `MD_XOR`, driven by AES timer events (`evnt_multi` `MU_TIMER`) |

It's a well-behaved GEM program: it redraws through the AES rectangle list
(`WF_FIRSTXYWH`/`WF_NEXTXYWH`), clips with `vs_clip`, locks the screen with
`wind_update`, and supports moving, sizing, fulling and closing. It adapts
to low, medium and high resolution. Quit with the closer, `q` or Esc.

When run under Hatari with `--natfeats on`, it logs to the host through
NatFeats `NF_STDERR` (`natfeats.c`, which safely detects NatFeats via the
illegal-instruction vector), so the agent API's `/console` shows:

```
GEMDEMO: started (NatFeats available)
GEMDEMO: screen 320x200, 16 colors
GEMDEMO: exit after 1 redraws, 419 animation frames
```

## Build and run

```sh
../../tools/agent/fetch-cross-mint.sh          # once: GCC 14 m68k-atari-mintelf + mintlib + gemlib
make CROSS=~/computers/atari-cc/opt/cross-mint/bin/m68k-atari-mintelf-
make run CROSS=...                             # Hatari ST + agent API, build/ as C:, autostart
```

`make run` mounts `build/` as GEMDOS drive C:. Hatari reads it live, so
after rebuilding, `curl -XPOST localhost:7777/emu/reset` restarts the new
binary without restarting the emulator.
