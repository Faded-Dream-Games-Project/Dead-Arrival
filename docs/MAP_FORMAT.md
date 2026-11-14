# Dead Arrival map format

Maps are plain text files with the `.map` extension in `assets/maps/`.
Every `.map` file in that folder appears automatically in the map selection menu —
the game never hardcodes a map list.

The parser is line based: `key = value`, `#` starts a comment.
Coordinates are metres; **X** is east, **Z** is south, **Y** is up.
Box positions are the **bottom center** of the box; sizes are full extents.

## Header keys

| Key | Meaning |
| --- | --- |
| `name` | Display name in the menu |
| `desc` | Short description shown under the name |
| `author` | Your name |
| `spawn_mode` | `barriers`, `graves` or `drop` (see below) |
| `ambient` | `r g b` (0-255) tint over all world textures — the lighting mood |
| `sky` | `r g b` clear colour |
| `player_spawn` | `x y z yaw_degrees` — where the player starts, yaw 0 faces -X, 90 faces -Z |

## Geometry

```
box = x y z width height depth texture
```

An axis-aligned solid box. Boxes can overlap (the engine does not care).
Boxes are collision (players and zombies) and rendering in one. Textures tile
automatically every 2 metres, so make textures that look good repeated.
The texture name is a file in `assets/textures/` without the extension.
If the texture is missing the game draws a magenta checker and keeps running.

Tip: build floors as thin boxes (`height 0.5`) just below y=0, and walls as
boxes from y=0 upward. Walls with openings are just several boxes plus a
lintel box above the gap — see `theater.map` for examples.

## Zombie entry points

Which of the following you use is decided by `spawn_mode`:

### `barriers` — boarded windows (theater style)

```
barrier = x y z face width
```

- Position: center of the opening (at floor level, the opening is 2.1 m tall)
- `face`: the side the zombies come **from**: `0` +Z, `1` -Z, `2` +X, `3` -X
- `width`: opening width in metres

Zombies spawn behind the barrier, tear off boards one at a time, then climb
through. The barrier blocks the **player** permanently (even with all boards
gone), so put it in a gap of your wall boxes. Rebuild boards with `F` (+10
points each).

### `graves` — rise from the ground (cemetery style)

```
grave = x y z
```

A point where a zombie claws out of the earth over ~1.4 s. Place a low mound
box under it (`grave_dirt` texture) so it reads visually.

### `drop` — fall in from above (trenches style)

```
drop = x y z
```

A point on elevated ground (a parapet, a ledge). Zombies spawn in the air
above it, land, then chase — and happily walk off edges, which is how they
get down into trenches.

## Equipment

```
wallbuy = x y z face weapon_id [price]
```

A chalk gun outline on a wall. `face` is the direction the icon faces
(into the room): `0` +Z, `1` -Z, `2` +X, `3` -X. `weapon_id` must exist in
`assets/weapons.cfg`. `price` is optional — 0 means "use the weapon's default".
If the player already owns the weapon, the buy becomes an ammo refill at half
price.

```
mysterybox = x y z
```

A mystery box (950 points, random weapon, 10 s to take it). Position is the
**center** of the box (for a 0.7 m tall box on a floor at y=0 use y=0.35).
Multiple boxes are allowed.

```
wallbuy = x y z face grenades [price]
```

Grenade refill station (default 450 points, fills you up to 4 grenades).

## Rooms and buyable doors

```
room = name x1 y1 z1 x2 y2 z2
```

A named axis-aligned area. Rooms are optional — a map without `room` lines
behaves like before (all barriers/spawns eligible). When rooms exist:

- Barriers are assigned to the room that contains them automatically.
- Zombies strongly prefer to spawn from barriers in the room the player is
  currently in; otherwise from any room the player has already visited
  (i.e. opened). Barriers outside all rooms are always eligible.

```
door = x y z face width cost
```

A buyable door blocking an opening in a wall. `face` is the outward normal
(`0` +Z, `1` -Z, `2` +X, `3` -X), like barriers. Press F near it to buy;
it then opens permanently (the slab disappears and the way is free for you
and the zombies). Doors block the player exactly like boarded barriers.

## Perk machines and Pack-a-Punch

```
perk = x y z type
```

A vending machine; `type` is one of `quick` (Quick Revive, 500 — faster
health regen), `double` (Double Tap, 2000 — +30% fire rate), `speed`
(Speed Cola, 3000 — ~half reload time), `jugg` (Juggernog, 2500 — +100 max
health). One machine per type makes sense; position is the machine's base
(floor level).

```
pap = x y z
```

The Pack-a-Punch machine (one per map, 5000 points): upgrades your current
weapon — 2.5x damage, +50% magazine size, shown with a `*` prefix.

## Complete example

```
name = Tiny Yard
desc = Two windows and a box
author = you
spawn_mode = barriers
ambient = 190 190 180
sky = 10 10 14
player_spawn = 0 0 0 90

box = 0 -0.5 0 12 0.5 10 floor_grass
# walls (east wall has the window gap z -1.2..1.2)
box = 0 0 -5.25 12 3 0.5 wall_stone
box = -6.25 0 0 0.5 3 10 wall_stone
box = 6.25 0 -3.1 0.5 3 3.8 wall_stone
box = 6.25 0 3.1 0.5 3 3.8 wall_stone
box = 6.25 2.1 0 0.5 0.9 2.4 wall_stone
barrier = 6.25 0 0 2 2.4

wallbuy = -6 1.6 0 2 kar98k 200
mysterybox = 0 0.35 -3
```

Drop it in `assets/maps/tiny_yard.map` and it shows up in the menu.
