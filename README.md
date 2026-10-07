# atari_st_games

Atari ST / STE / Falcon030 ports of the games in
[geekychris/amiga_games](https://github.com/geekychris/amiga_games). The Amiga
versions are the originals; these ports keep their game and drawing code and
run it on small Atari layers that emulate the Amiga graphics API, Paula
sound and the AmigaDOS calls they need.

MIT licensed, same as the originals (see [LICENSE](LICENSE)).

| Game | Machine | Directory |
|---|---|---|
| Uranus Lander | ST | [uranus_lander](uranus_lander) |
| Nova Defense | ST | [nova_defense](nova_defense) |
| Frank the Frog | ST | [frank_the_frog](frank_the_frog) |
| Rock Blaster | STE | [rock_blaster](rock_blaster) |
| Orbital Patrol | STE | [orbital_patrol](orbital_patrol) |
| Jump Quest | STE | [jump_quest](jump_quest) |
| StakAttack | STE | [stakattack](stakattack) |
| Dot Chase | STE | [dot_chase](dot_chase) |
| Lunar Rider | STE | [lunar_rider](lunar_rider) |
| Pea Shooter Blast | STE | [pea_shooter_blast](pea_shooter_blast) |
| Rescue on Fractalus | Falcon030 | [fractalus](fractalus) |
| Void Trader | Falcon030 | [void_trader](void_trader) |

Also here:

- [st_port](st_port) and [falcon_port](falcon_port): the port layers (Amiga
  header shims, graphics on ST bitplanes and Falcon 16-bit chunky, Paula
  emulation on STE DMA sound, a C ProTracker player, IKBD input, NatFeats
  logging).
- [st_launcher](st_launcher): an Atari-side menu that puts every port on one
  C: drive.
- [games.ini](games.ini): the game list used by both launchers.
- [gemdemo](gemdemo) and [interact](interact): GEM and agent-API demos.

## Building

You need the m68k-atari-mint cross compiler (GCC 14 `m68k-atari-mintelf-`):

```sh
make -C rock_blaster                       # -> rock_blaster/build/ROCK.PRG
make -C rock_blaster CROSS=/path/to/m68k-atari-mintelf-
```

## Running

Running, the bots/autopilots and the test scripts use the Hatari emulator and
agent tooling from [geekychris/hatari_augmented](https://github.com/geekychris/hatari_augmented)
(which also provides EmuTOS images and a script to fetch the cross compiler).
Check it out next to this repo, or point `HATARI_DIR` at it:

```sh
git clone https://github.com/geekychris/hatari_augmented ../hatari_augmented
../hatari_augmented/tools/agent/fetch-cross-mint.sh     # once: cross compiler
make -C rock_blaster run                                # Hatari + agent API, autostarts the game
HATARI_DIR=~/src/hatari_augmented make -C fractalus run
python3 ../hatari_augmented/tools/games/launcher.py --ini games.ini   # host-side launcher
```

## History

These ports were developed in `hatari_augmented/examples/` and moved here with
their git history. The same games also run on the 3DO through
[geekychris/3do-dev](https://github.com/geekychris/3do-dev).
