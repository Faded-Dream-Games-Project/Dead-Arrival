# Dead Arrival

A round-based zombies survival FPS written **fully in C** on [raylib](https://www.raylib.com/) 5.5.
COD-zombies-style difficulty curve: zombies get slowly tougher each round, you earn points
for every hit and kill, buy weapons off walls, gamble on the Mystery Box, drink perk sodas,
and open doors to expand the map — until you go down.

![Dead Arrival](textures/logo.png)

## Features

- **Pure C** (C11), one game binary, no engine, no assets required to run (procedural fallbacks).
- **First-person WASD** movement: sprint, jump, crouch, ADS (aim down sights).
- **Round-based survival**: every round spawns a quota of zombies; HP and speed creep up slowly.
  Round 1: 100 HP. Round 2: 200 HP. Then +100/round, later +10%/round.
- **Knife > pistol rule** (as designed): the knife one-hit-kills round-1 zombies and
  needs exactly two hits on round 2.
- **20 guns** (pistols, SMGs, shotguns, rifles, LMGs, sniper) plus the knife.
  A few are wall buys; everything else can drop from the Mystery Box (950 points).
- **Points economy**: +10 per bullet hit, +50 per kill, 500 to start.
- **Perk sodas** (machines glow on the map): Self Revive (auto get-back-up when
  downed, one use per purchase), Regen Soda (auto-regen 10 s after the last hit),
  Swift Soda (40% faster reloads), Rapid Cola (25% faster fire rate),
  Stamin-Up (move & sprint faster).
- **2-hit down system**: 100 HP, zombies take 50 per swipe.
- **4 maps**, each with its own lighting, fog, ambience and gimmicks:
  1. **The Grand Theater** — auditorium, stage, balcony, backstage, back alley.
  2. **Bunker 44** — WW-style war zone: cross trenches, sandbags, three concrete bunkers.
  3. **Graveyard Hollow** — zombies claw out of their graves; a church sits in the middle.
  4. **Die Arise** — sky-high rooftops with jump pads, a zipline, a loot pit and a spire.
- **Doors to unlock** with points, wall buys, perk machines, Mystery Box on every map.
- **Custom maps & multiplayer**: shown in the menu as *Coming Soon* — the map data is
  already data-driven in code, designed to be externalized later.

## Controls

| Input | Action |
| --- | --- |
| WASD | Move |
| Mouse | Look |
| Left click / F | Fire / knife |
| Right click | Aim down sights |
| R | Reload |
| E | Interact (doors, buys, perks, box, ziplines) |
| 1 / 2 / 3 (or V) | Weapon slots / knife |
| Q / mouse wheel | Quick swap |
| Shift | Sprint |
| Ctrl / C | Crouch |
| Space | Jump |
| F11 | Fullscreen |
| Esc | Pause / back |

## Build

### Windows
Download `DeadArrival.exe` from GitHub Actions artifacts (auto-built on every push),
or build with MSYS2/MinGW:

```
git clone --depth 1 --branch 5.5 https://github.com/raysan5/raylib.git raylib-src
gcc -O2 -Isrc -Iraylib-src/src src/*.c raylib.lib -o DeadArrival.exe -lopengl32 -lgdi32 -lwinmm -static
```

### Linux
```
sudo apt install libasound2-dev libx11-dev libxcursor-dev libxrandr-dev libxinerama-dev libxi-dev libgl-dev
git clone --depth 1 --branch 5.5 https://github.com/raysan5/raylib.git raylib-src
# build raylib static lib into deps/libraylib.a (see CI workflow for exact flags)
make
./deadarrival
```

## Assets
`textures/` and `sfx/` are plain PNG/WAV files sitting next to the binary —
mod-friendly by design. If a file is missing, the game generates a procedural
fallback at runtime so it always runs.

## Troubleshooting

**No sound** — the audio device must exist before sounds load; the game calls
`InitAudioDevice()` itself, so if you see `WARNING: AUDIO: Failed to initialize
playback device`, check `aplay -l` / pulseaudio / pipewire first. On a headless
box there is simply no device and the game stays silent by design.

**Built against raylib 6.x and something broke** — the game targets raylib 5.5
(the Makefile clones and builds 5.5 automatically). raylib 6.x changed some
audio/stream internals; if you link 6.x and hit issues, use `make` which pins
5.5, or grab the prebuilt exe from Actions artifacts.

**Mouse stuck / free** — the cursor is captured automatically when a round
starts and released on pause/menus (Esc). F11 toggles fullscreen.

## Status

- [x] 4 maps, 20 weapons, 5 perks, mystery box, doors, points economy
- [x] Selftest mode (`./deadarrival --selftest`) validating maps, nav paths, weapon rules
- [x] Smoke mode (`./deadarrival --smoke <dir> <mapId>`) headless run with screenshots + movement-speed regression check
- [ ] Custom map loading (menu entry present, marked Coming Soon)
- [ ] Multiplayer (menu entry present, marked Coming Soon)

## License
MIT — see LICENSE.
