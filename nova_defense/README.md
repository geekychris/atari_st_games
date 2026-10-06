# Nova Defense: Atari ST port

Port of Nova Defense, the invaders game in
[geekychris/amiga_games](https://github.com/geekychris/amiga_games) (`nova_defense/`,
commit in `.upstream-commit`), built on the shared ST layer in `../st_port`.

```sh
make CROSS=~/computers/atari-cc/opt/cross-mint/bin/m68k-atari-mintelf-
make run CROSS=...           # ST, PAL EmuTOS, autostart
python3 bot.py --seconds 60  # watch a bot play it through the agent API
```

Controls: mouse, cursor keys / A,D or joystick to move. Left button, Space,
Alt or fire to shoot. Esc quits.

## Changes from the Amiga version

| | |
|---|---|
| `game.c` | Unchanged, except one **performance fix worth upstreaming**: when aliens reach the shields, the original tested every shield pixel against every alien (~77,000 loop iterations per frame). On an 8 MHz 68000 that froze the game for ~2 s. It now clears each alien's overlap with each shield: same result, ~100x cheaper. Found with the agent API: `/debug/break` → backtrace `game_update+0x4d8` → `addr2line`. |
| `draw.c` | `draw_game()` (clear + redraw everything) split into a diffed HUD layer (scaled 5x7 text cached per string, shields cached by pixel checksum, lives, ground) and a sprite layer. The 55 aliens use `gfx_or16_row()`: an OR blit per alien row with masks pre-shifted for the two x phases a 24 px spaced row has. |
| input | Mouse (x movement, left button), keyboard and joystick through `st_ikbd` (IKBD 0x14 + 0x08: mouse and joystick together). |
| sound | Paula waveforms → YM2149 at the same pitches (Paula period P with 32-sample waves = YM period P x 1.128): march, melody, shots, UFO warble, explosions. |
| main | Layered frame loop. HUD layer skipped entirely when score/lives/shields haven't changed. 50 Hz game updates, steady 25 fps display. NatFeats `NOVA` log lines and symbols. |

Performance: 1–1.4 frames of work at the start of a wave on a plain 8 MHz
ST, more with many bullets and explosions. It runs a steady 25 fps, with game
logic at the original 50 Hz.
