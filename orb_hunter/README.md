# Orb Hunter: Atari STE port

Port of Orb Hunter, the cavern exploration platformer in
[geekychris/amiga_games](https://github.com/geekychris/amiga_games) (`orb_hunter/`,
commit in `.upstream-commit`), built on the shared ST layer in `../st_port`.

## Screenshots

<table><tr>
<td align="center"><img src="docs/title.png" width="320" alt="Title"><br>Title</td>
<td align="center"><img src="docs/caverns.png" width="320" alt="The first room, with a save point"><br>The first room, with a save point</td>
<td align="center"><img src="docs/citadel.png" width="320" alt="Citadel"><br>Citadel</td>
</tr></table>

Captured through the agent API (`/screen`, 2x).

```sh
make CROSS=~/computers/atari-cc/opt/cross-mint/bin/m68k-atari-mintelf-
make run CROSS=...      # STE, EmuTOS 1024k, autostart
```

Controls, as on the Amiga:
- Cursor keys, WASD or joystick: move.
- Up: jump / aim up.
- Down (or Z): roll form.
- Space, Alt or fire: shoot (or drop a bomb when rolled).
- Return: start.
- Esc: quit.

## Music

No music is included: the Amiga version's MOD is a cover of a commercial track. Copy a 4-channel ProTracker module of your own to `build/MUSIC.MOD`; the original loader validates and plays it. Without it, the game plays its sound effects only.

## What changed

| | |
|---|---|
| `game.c`, `levels.c`, `game.h`, `input.h` | **Unmodified** (with the original's fixes, below). |
| `main_st.c` | Replaces `main.c` (kept as `main.c.amiga`). The procedural sound effects, the MOD loading and validation, and the state machine are copied from it. Effects and music play on the C ptplayer (`../st_port/ptplayer`) on the Paula emulation, at 6258 Hz on the STE's DMA sound. Game logic runs once per 50 Hz VBL, as on the Amiga, and the drawing once per frame. The bridge hooks are left out; `/mem` covers them. |
| `draw.c` | `ST port:` blocks, listed below. |
| `st_input.c` | The same keys from the IKBD handler, plus joystick 1. |

### ST port changes in `draw.c`

The Amiga version clears the screen and redraws the whole room, all 320 tiles, every frame. The port instead:
- **Rooms:** the static tiles are the ST layer's scenery, redrawn only when the room changes or one of its tiles does (a door opening, an item block taken). That's checked against a copy of the room.
- **Every frame:** the animated tiles (lava, item blocks, save points) are listed once per room. They and the actors go into the back buffer and are undone through dirty rectangles.
- **HUD and boxes:** the HUD and the item box are on the HUD layer; the title and game-over pages too. Strings there are cached operations.
- **Text:** a scale-1 character is drawn as one 16-pixel masked write per row instead of a `WritePixel` per pixel.

- **Flashes:** the hit and invulnerability flashes skip every other *drawn* frame (`g_flash`). The game's step count, which the original uses, goes up 1–4 a drawn frame here, so its parity could stay the same for frame after frame.

### Fixes from the original

The Amiga original's fixes ([amiga_games#50](https://github.com/geekychris/amiga_games/pull/50)) are in the port too:
- **Frame count:** `gs.frame` went up twice a step (`main_st.c` and `game_update`). In the port that kept it even, so the invulnerable hunter never flickered and the animations ran at double speed. `game_update` alone counts it now.
- **Hit flash:** enemies flash for 8 steps when hit (`hit_timer`), instead of testing the AI's behaviour timer.
- **Presses:** the title, the item box and the game-over page need a new press of fire; a held fire no longer skips them.
- **Timers:** the item box closes after 90 steps and the game-over page goes back to the title after 180 (`game_update` runs in those states).
- **Text:** the title says `UP+FIRE - MISSILE`. The HUD shows the score where the original had a debug line (`Y.. F.. R..`), and the game-over page shows the final score.
- **Citadel:** its creatures start on the floor (row 12), not inside it (row 13).

The original's joystick decode fix doesn't apply: the port reads the joystick through the IKBD.

## Performance

**16–25 fps on an 8 MHz STE**, with game logic at 50 Hz. The first rooms run at 25 fps.

## Agent hooks

The game logs `ORBH SYMBOL ...` lines with addresses for `gs`, `hunter`, `room_x`, `state` and `frame_sync`. It also logs `ORBH STATE` events with the room and health, and `ORBH PERF`. The Amiga version's bridge hooks (give items, full health) are writes to `gs.hunter` through `/mem`.
