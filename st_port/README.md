# ST port layer

Shared code for porting Amiga games from
[geekychris/amiga_games](https://github.com/geekychris/amiga_games) to the Atari ST and STE.

The ports that use it:
- [uranus_lander](../uranus_lander), [nova_defense](../nova_defense) and [frank_the_frog](../frank_the_frog) run on a plain ST.
- [rock_blaster](../rock_blaster), [orbital_patrol](../orbital_patrol), [jump_quest](../jump_quest), [stakattack](../stakattack), [dot_chase](../dot_chase), [lunar_rider](../lunar_rider), [pea_shooter_blast](../pea_shooter_blast), [sky_knights](../sky_knights), [orb_hunter](../orb_hunter), [bullion_dash](../bullion_dash) and [rj_birthday](../rj_birthday) need an STE for DMA sound. Lunar Rider, Pea Shooter Blast and RJ's Birthday Bash also use its blitter.
- The Falcon layer in [../falcon_port](../falcon_port) reuses the IKBD, NatFeats and Paula code.

A port's Makefile sets `NAME`, `SRCS`, optionally `ST_EXTRA`, `MACHINE` and `EXTRA_CFLAGS`, then includes `port.mk`.
- `make run` starts Hatari through the agent API with `build/` as C: and autostarts the program. `MEMSIZE` (in MB) sets the ST RAM when a game needs more than Hatari's default 1 MB.
- STE targets use EmuTOS 1024k. The 192k image reports TOS 1.04, which Hatari only runs as a plain ST.

| Module | |
|---|---|
| `st_gfx.c/.h` | The graphics.library subset (`SetAPen`, `Move`, `Draw`, `RectFill`, `WritePixel`, `Text`, `SetRast`) on ST low resolution. The game keeps its 320x256 Amiga coordinates, and Y is mapped to 200 lines. Display layers are below. Also forces PAL 50 Hz (the games pace by the VBL) and switches a Mega STE to 16 MHz with cache. |
| `st_ikbd.c/.h` | Own IKBD interrupt handler: full key state, counted presses (`ikbd_key_hit` returns how many since the last call), the last key pressed (`ikbd_last_hit`, for typing), joysticks 1 and 0, mouse. |
| `st_ym.c/.h` | YM2149 helpers (the plain-ST ports' sound). |
| `paula.c/.h` (`ST_EXTRA=paula`) | Amiga Paula on STE/TT/Falcon DMA sound. `custom` has the real register offsets, so code poking `custom.aud[]` or computing addresses from the custom base works. `custom.dmacon = x` works unchanged from C++. Four channels are mixed Amiga-style into an 8-bit stereo ring from the VBL: 6258 or 12517 Hz on the STE, 9834 Hz on the Falcon. |
| `ptplayer.c/.h` (`ST_EXTRA=ptplayer`) | ProTracker MOD player and `mt_playfx` sound effects, with the C API of Frank Wille's ptplayer, which the Amiga games call from assembly. |
| `amiga_dos.c` (`ST_EXTRA=amiga_dos`) | AmigaDOS `Open`/`Read`/`Write`/`Seek`/`Close`/`Delay` on GEMDOS. Volume prefixes are dropped and names shortened to 8.3. |
| `st_blit.c/.h` (`ST_EXTRA=st_blit`) | STE blitter copies between 4-plane bitmaps of any width, at any pixel offset (`BLIT_COPY` or `BLIT_OR`). For scrolling layers: pre-render a wide strip once, then blit the visible window each frame. `blit_available()` checks for an STE / Mega STE. |
| `ab_log.c` (`ST_EXTRA=ab_log`) | The amiga_games bridge's `AB_I`/`AB_W`/`AB_E` logging, sent to the host through NatFeats. |
| `natfeats.c/.h` | Hatari NatFeats (`nf_print` → agent API `/console`). |
| `compat/` | Amiga header shims: exec, graphics, intuition, dos, hardware, bridge_client. |

## Display layers (`st_gfx`)

Bottom to top:
- **Scenery:** a static picture, drawn rarely.
- **HUD:** drawn every frame through `gfx_hud()`. Operations are recorded and diffed, so only changes are actually rendered.
- **Sprites:** drawn into the back buffer each frame and undone through dirty rectangles (`gfx_restore_back`).

Helpers for faster drawing:

| Helper | |
|---|---|
| `gfx_sprite_build` / `gfx_sprite_draw[_xy]` | Pre-shifted sprites rendered by the game's own drawing code. |
| `gfx_or_mode` | Lines that only OR their colour, for vector graphics over colour 0. |
| `gfx_heightmap_*` / `gfx_column_band` | Scrolling terrain. |
| `gfx_fill_band` / `gfx_fill_rows` / `gfx_copy_band` | movem fills and row copies. |
| `gfx_bg_commit_rows` / `gfx_bg_dirty_rows` | Scenery that changes in parts. |
| `gfx_bg_copy_rect` | Copy a rectangle of the scenery into the back buffer (restoring under sprites). |
| `gfx_vspans` | A run of vertical spans (silhouettes, terrain), filled 16 columns at a time from a difference mask. |
| (built in) | A text glyph cache for strings redrawn every frame; single-group `RectFill`s take a fast path. |

## Notes

- **Supervisor mode.** The ports run between `Super(0L)` and `Super(old_ssp)` in `main()`, and EmuTOS's `Super()` restores the user stack pointer saved by the first call. `port.mk` therefore builds with `-fno-defer-pop`, so the stack is at the same depth at both calls.
- **Measuring.** Speed work was measured with the agent API: `profile on`, run, `profile save`, then `tools/debugger/hatari_profile.py`. Each port logs a PERF line every 100 frames.
