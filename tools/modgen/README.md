# modgen: the ports' music and voice clips

The MOD music and voice clips that the ports load from disk are made here,
so every note and sample is ours: the songs are written in Python, the
instruments are synthesised (no recorded samples), and the voices are
synthetic speech. The same scripts live in
[geekychris/3do-dev](https://github.com/geekychris/3do-dev) and
[geekychris/amiga_games](https://github.com/geekychris/amiga_games)
(`tools/modgen/`); here they write next to each port.

| Script | Writes | What it is |
|---|---|---|
| `stakattack.py` | `stakattack/stakattack.mod` | "Stack Bounce": original, bouncy puzzle tune, G major |
| `pea_shooter_blast.py` | `pea_shooter_blast/music.mod` | "Pea Patrol": original upbeat march, D major |
| `orbital_patrol.py` | `orbital_patrol/orbital_patrol.mod` | "Patrol Drive": original driving techno, A minor |
| `rj_party.py` | `rj_birthday/party.mod` | "Party Time": original upbeat party tune, C major |
| `rj_birthday.py` | `rj_birthday/birthday.mod` | "Happy Birthday to You" (Mildred and Patty Hill, public domain), our own waltz arrangement |
| `arcade_voices.py` | `rj_birthday/snd_arcade1..4.raw` | "Game on!", "Ready, player!", "High score!", "Level up!" spoken by Kokoro TTS (Apache-2.0), made to sound like a robot |

Ace Pilot's `blue_danube.mod` (Strauss, public domain) comes from
`ace_pilot/gen_mod.py`. Uranus Lander and Frank the Frog play their own
original tunes on the YM2149 (`st_sound.c`, `st_modplay.c`).

## Regenerating

```sh
cd tools/modgen
python3 stakattack.py          # any song script; writes straight into the port
python3 render.py ../../stakattack/stakattack.mod /tmp/stak.wav   # listen on the Mac
python3 arcade_voices.py       # needs kokoro-server on 127.0.0.1:8770 (KOKORO_URL)
make -C ../../stakattack       # copies the MOD into build/ under its 8.3 name
```

`modlib.py` is the writer: `Song`, `Pattern.melody()` / `Pattern.drums()`,
`m("C-2:4 E-2:2 .:2")` for compact note lists, and the synthesised
instruments (`cycle`, `pluck`, `pad`, `kick`, `snare`, `hat`, `clap`, `mix`).
`render.py` previews the effects the songs use; it isn't a full ProTracker
player.
