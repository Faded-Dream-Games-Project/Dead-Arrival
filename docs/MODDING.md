# Modding Dead Arrival

Nothing gameplay-related is compiled into the binary. Maps, weapons, textures
and sounds all load from `assets/` at startup, so most modding needs no
rebuild at all — edit the file and restart the game.

## Adding a weapon

Open `assets/weapons.cfg` and copy an existing block:

```ini
[weapon.my_rifle]
name     = My Rifle
class    = rifle          ; pistol|smg|rifle|ar|shotgun|sniper|lmg|special
sound    = shot_rifle     ; any WAV name in assets/sounds (without extension)
damage   = 60             ; per bullet (per pellet for shotguns)
head_mult= 2.5            ; headshot damage multiplier
mag      = 10
reserve  = 90             ; spare ammo on buy
rpm      = 350
reload   = 2.6            ; seconds
spread   = 0.011          ; radians of cone
recoil   = 2.5
ads_zoom = 40             ; FOV while aiming (smaller = more zoom)
pellets  = 1              ; 8-9 for shotguns
penetrate= 2              ; how many zombies one bullet can pass through
auto     = 0              ; 1 = hold to fire
range    = 130
wallbuy  = 1              ; may be placed by maps as a wall buy
price    = 650            ; default wall price (ammo refill = price/2)
box_weight = 8            ; mystery box chance, 0 = never appears in the box
tint     = 100 90 72      ; viewmodel colour
vm_len   = 0.9            ; viewmodel length in metres
```

The weapon appears everywhere automatically: give it to a `wallbuy` in a map,
or let it roll out of the mystery box. `class` picks the viewmodel silhouette
and `sound` picks the firing sound — both can be shared freely between guns.

## Adding sounds

Drop a `.wav`, `.ogg` or `.mp3` into `assets/sounds/`. Reference it by
filename without extension (`sound = my_shot` in weapons.cfg, or the hardcoded
names like `zombie_groan`, `round_start`, `buy` in the engine). Missing sounds
log a warning but never crash the game.

To regenerate all procedural sounds from scratch: `python3 tools/gen_assets.py --force`.

## Adding textures

Drop a `.png` into `assets/textures/` and reference it by filename without
extension — in map files (`box = ... my_texture`) or anywhere else. Textures
tile every 2 world metres on boxes, so make them seamless. Missing textures
render as a magenta checker.

## Making a map

See `docs/MAP_FORMAT.md` for the full format. The short version:

1. copy `assets/maps/theater.map` to `assets/maps/mymap.map`
2. change `name`, `desc`, rebuild the geometry with `box =` lines
3. pick a `spawn_mode` and add `barrier` / `grave` / `drop` points
4. place `wallbuy` and `mysterybox` entries
5. run the game — the map is in the menu

Sanity-check your map headlessly:

```sh
./build/dead_arrival --autotest mymap.map 1800 mymap.png
```

It prints `spawned/kills/hits/points` statistics and saves a screenshot;
`RESULT: PASS` means zombies spawned and the scripted player landed hits.

## Tips

- Keep at least one `mysterybox` and a couple of `wallbuy`s per map.
- Barrier widths of 2.4-2.6 m and the 2.1 m opening height match the built-in
  board art; other sizes work but the boards will stretch.
- The engine supports up to 512 boxes, 32 barriers, 64 spawn points, 32 wall
  buys and 4 mystery boxes per map — all listed in `src/game.h`.
