# tools

## gen_guns.py - gun model generator

Builds every gun viewmodel from parameters and exports OBJ + MTL pairs to
`assets/guns/<id>.obj`. No external Python packages needed (plain OBJ output).

```
python3 tools/gen_guns.py
```

### Model conventions

- origin at the **grip**, **+Z forward** (muzzle direction), +Y up, +X right
- units are **meters**
- four materials per model: `body`, `dark`, `accent`, `silver`; their base
  colors approximate the classic look, and the game's skin system tints
  `body`/`dark`/`accent` per selected skin (WHITE in a skin = keep the MTL color)

### Adding a new gun (zero C changes)

1. **Model**: add an entry to `GUNS` in `tools/gen_guns.py` and rerun it
   (or hand-write `assets/guns/<id>.obj` following the conventions above).
2. **Stats**: add an entry to `assets/guns.json`:

```json
"mygun": {
  "name": "My Gun", "mode": "auto", "damage": 40, "pellets": 1,
  "mag": 30, "reserve": 240, "rpm": 720, "reloadTime": 2.4,
  "spread": 0.045, "adsSpread": 0.010, "recoil": 1.8,
  "wallPrice": 1200, "boxWeight": 8,
  "model": "mygun", "adsFov": 50,
  "papDamageMul": 2.5, "papMagBonus": 20,
  "accent": [90, 70, 50]
}
```

Fields you omit fall back to the built-in default for that gun id, or to
generic values for a brand-new id. The gun shows up in the mystery box when
`boxWeight > 0`, on walls where a map's `wallbuys` references `"gun": "mygun"`.

### Regenerating / checking

- `--selftest` in the game verifies every gun in `guns.json` has a model that
  loads (`assets/guns/<model>.obj`).
- `--vmtest` renders the current viewmodel at several angles and checks the
  silhouette stays stable (catches model/basis bugs).
