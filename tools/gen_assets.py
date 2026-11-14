#!/usr/bin/env python3
"""
gen_assets.py - procedural asset generator for Dead Arrival.

Generates every texture (PNG) and sound (WAV) under assets/ from scratch.
No copyrighted material: everything is synthesized from noise and math.

Usage:
    python3 tools/gen_assets.py            # generate everything
    python3 tools/gen_assets.py --check    # only generate missing files
    python3 tools/gen_assets.py --force    # regenerate all (overwrites)
"""

import argparse
import math
import os
import random
import struct
import wave

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
TEX_DIR = os.path.join(ROOT, "assets", "textures")
SND_DIR = os.path.join(ROOT, "assets", "sounds")

rng = random.Random(20261114)

# ============================================================================
# TEXTURES
# ============================================================================

def clamp(v):
    return max(0, min(255, int(v)))

def jitter(c, amt):
    a = abs(amt)
    d = rng.randint(-a, a)
    return (clamp(c[0] + d), clamp(c[1] + d), clamp(c[2] + d))

def noise_image(size, base, variance, seed=None):
    """Per-pixel random variation around a base color."""
    r = rng if seed is None else random.Random(seed)
    img = Image.new("RGB", (size, size))
    px = img.load()
    for y in range(size):
        for x in range(size):
            d = r.randint(-variance, variance)
            px[x, y] = (clamp(base[0] + d), clamp(base[1] + d), clamp(base[2] + d))
    return img

def value_noise(size, cells, seed):
    """Smooth, tileable value noise: random values on a coarse grid, bilinear + wrap."""
    r = random.Random(seed)
    g = [[r.random() for _ in range(cells)] for _ in range(cells)]
    img = Image.new("RGB", (size, size))
    px = img.load()
    for y in range(size):
        fy = y / size * cells
        y0 = int(fy) % cells
        y1 = (y0 + 1) % cells
        ty = fy - math.floor(fy)
        ty = ty * ty * (3 - 2 * ty)
        for x in range(size):
            fx = x / size * cells
            x0 = int(fx) % cells
            x1 = (x0 + 1) % cells
            tx = fx - math.floor(fx)
            tx = tx * tx * (3 - 2 * tx)
            v = (g[y0][x0] * (1 - tx) + g[y0][x1] * tx) * (1 - ty) + \
                (g[y1][x0] * (1 - tx) + g[y1][x1] * tx) * ty
            px[x, y] = (int(v * 255), int(v * 255), int(v * 255))
    return img

def blend(img, noise, amount):
    """Multiply-blend a grayscale noise image over img."""
    base = img.load()
    n = noise.load()
    size = img.size[0]
    for y in range(size):
        for x in range(size):
            f = 1.0 - amount / 2.0 + (n[x, y][0] / 255.0) * amount
            r, g, b = base[x, y]
            base[x, y] = (clamp(r * f), clamp(g * f), clamp(b * f))
    return img

def tex_wood(size, base, plank_h, gap_col, vertical=False, seed=1):
    """Planked wood: grain streaks + gaps between planks + staggered seams."""
    img = noise_image(size, base, 10, seed)
    px = img.load()
    # grain streaks
    r = random.Random(seed + 7)
    for i in range(size * 3):
        gy = r.randint(0, size - 1)
        gx = r.randint(0, size - 1)
        ln = r.randint(8, 40)
        dark = r.randint(10, 30)
        for k in range(ln):
            x = (gx + k) % size
            c = px[x, gy]
            px[x, gy] = (clamp(c[0] - dark), clamp(c[1] - dark), clamp(c[2] - dark))
    # plank gaps + seams
    for y in range(size):
        if y % plank_h == 0:
            for x in range(size):
                px[x, y] = gap_col
        # staggered vertical seams every plank
        row = y // plank_h
        seam = (row * 97 + 31) % size
        px[seam, y] = gap_col
    if vertical:
        img = img.rotate(90)
    return img

def tex_bricks(size, brick_w, brick_h, brick, mortar, seed=3):
    img = noise_image(size, mortar, 6, seed)
    px = img.load()
    r = random.Random(seed + 11)
    for by in range(0, size, brick_h):
        offset = (by // brick_h % 2) * (brick_w // 2)
        for bx in range(-brick_w, size + brick_w, brick_w):
            x0 = bx + offset
            for y in range(by + 2, min(by + brick_h - 1, size)):
                for x in range(max(0, x0 + 2), min(size, x0 + brick_w - 1)):
                    if 0 <= x < size:
                        c = jitter(brick, 14)
                        # weathering: occasional chipped pixel
                        if r.random() < 0.02:
                            c = jitter(c, -30)
                        px[x, y] = c
    return img

def tex_sandbags(size, seed=5):
    img = noise_image(size, (128, 118, 96), 12, seed)
    px = img.load()
    bag_h, bag_w = size // 4, size // 2
    for by in range(0, size, bag_h):
        offset = (by // bag_h % 2) * (bag_w // 2)
        for bx in range(-bag_w, size + bag_w, bag_w):
            x0, y0 = bx + offset, by
            # sack outline (darker rim)
            for x in range(max(0, x0), min(size, x0 + bag_w)):
                for y in (y0 + 1, y0 + bag_h - 2):
                    if 0 <= y < size:
                        px[x, y] = (86, 78, 62)
            for y in range(max(0, y0), min(size, y0 + bag_h)):
                for x in (x0 + 2, x0 + bag_w - 3):
                    if 0 <= x < size:
                        px[x, y] = (86, 78, 62)
    return img

def tex_curtain(size):
    img = Image.new("RGB", (size, size))
    px = img.load()
    for x in range(size):
        fold = math.sin(x / size * math.pi * 12) * 0.5 + 0.5
        v = 0.55 + 0.45 * fold
        for y in range(size):
            vy = v * (0.9 + 0.1 * math.sin(y / size * math.pi))
            px[x, y] = (clamp(120 * vy), clamp(18 * vy), clamp(24 * vy))
    return blend(img, value_noise(size, 8, 99), 0.25)

def tex_chalk_gun(size):
    """Chalk gun outline on a dark board (wall buy icon)."""
    img = noise_image(size, (34, 34, 40), 6, 21)
    d = ImageDraw.Draw(img)
    w = (225, 222, 210)
    # chalk rifle silhouette (line art)
    d.line([(30, 130), (200, 130)], fill=w, width=6)     # barrel
    d.line([(30, 130), (60, 110)], fill=w, width=4)      # muzzle
    d.line([(80, 130), (130, 130)], fill=w, width=10)    # receiver
    d.line([(90, 130), (105, 175)], fill=w, width=8)     # magazine
    d.line([(130, 130), (185, 160)], fill=w, width=8)    # stock
    d.line([(150, 118), (170, 108)], fill=w, width=5)    # sight
    # chalk dust speckles
    r = random.Random(4)
    for _ in range(140):
        x, y = r.randint(0, size - 1), r.randint(80, 190)
        d.point((x, y), fill=(200, 198, 188))
    return img

def tex_mystery_box(size):
    img = tex_wood(size, (72, 48, 30), size, (40, 26, 16), seed=8)
    d = ImageDraw.Draw(img)
    # gold trim
    d.rectangle([4, 4, size - 5, size - 5], outline=(190, 150, 60), width=4)
    d.rectangle([4, size // 2 - 3, size - 5, size // 2 + 3], fill=(40, 26, 16))
    # glowing question marks
    glow = (120, 200, 255)
    for cx in (size // 4, 3 * size // 4):
        d.text((cx - 22, size // 2 - 74), "?", fill=glow)
        d.text((cx - 6, size // 2 + 6), "?", fill=glow)
    return img

def tex_zombie_skin(size):
    img = blend(noise_image(size, (128, 138, 110), 14, 31),
                value_noise(size, 10, 55), 0.6)
    px = img.load()
    r = random.Random(77)
    # wounds / mottling
    for _ in range(26):
        cx, cy = r.randint(0, size - 1), r.randint(0, size - 1)
        rad = r.randint(4, 14)
        for x in range(max(0, cx - rad), min(size, cx + rad)):
            for y in range(max(0, cy - rad), min(size, cy + rad)):
                if (x - cx) ** 2 + (y - cy) ** 2 <= rad * rad and r.random() < 0.7:
                    px[x, y] = jitter((96, 60, 55), 16)
    return img

def tex_perk_machine(size, panel, glow, letter, seed):
    """Vending machine front: dark cabinet, colored panel, glowing icon."""
    img = blend(noise_image(size, (52, 50, 54), 8, seed),
                value_noise(size, 6, seed + 1), 0.4)
    d = ImageDraw.Draw(img)
    # cabinet frame
    d.rectangle([8, 8, size - 9, size - 9], outline=(28, 26, 30), width=6)
    # colored front panel
    d.rectangle([24, 24, size - 25, size // 2 + 20], fill=panel)
    for y in range(24, size // 2 + 20, 6):     # panel sheen lines
        d.line([(26, y), (size - 26, y)], fill=glow, width=1)
    # glowing icon (letter + halo) on the panel
    cx, cy = size // 2, size // 4 + 12
    d.ellipse([cx - 52, cy - 52, cx + 52, cy + 52], fill=(20, 18, 22))
    d.ellipse([cx - 44, cy - 44, cx + 44, cy + 44], outline=glow, width=4)
    d.text((cx - 11, cy - 22), letter, fill=glow)
    # dispense slot + base
    d.rectangle([size // 3, size - 58, 2 * size // 3, size - 30], fill=(18, 16, 20))
    d.rectangle([size // 3, size - 58, 2 * size // 3, size - 30], outline=glow, width=2)
    return img

def tex_pap(size):
    """Pack-a-Punch: dark iron machine with a hot orange core."""
    img = blend(noise_image(size, (44, 40, 44), 10, 91),
                value_noise(size, 7, 92), 0.5)
    d = ImageDraw.Draw(img)
    # iron plates + rivets
    d.rectangle([6, 6, size - 7, size - 7], outline=(24, 22, 26), width=8)
    for x in (6, size // 2, size - 7):
        d.line([(x, 6), (x, size - 7)], fill=(58, 54, 58), width=3)
    r = random.Random(93)
    for _ in range(24):
        rx, ry = r.randint(14, size - 15), r.randint(14, size - 15)
        d.ellipse([rx - 3, ry - 3, rx + 3, ry + 3], fill=(70, 66, 70))
    # molten core window
    d.rectangle([size // 3, size // 3, 2 * size // 3, 2 * size // 3], fill=(30, 14, 6))
    hot = (255, 150, 40)
    d.rectangle([size // 3 + 8, size // 3 + 8, 2 * size // 3 - 8, 2 * size // 3 - 8],
                outline=hot, width=5)
    for i in range(4):   # heat glow bars
        yy = size // 3 + 18 + i * 16
        d.line([(size // 3 + 14, yy), (2 * size // 3 - 14, yy)],
               fill=(255, 190, 90) if i % 2 else hot, width=5)
    d.text((size // 2 - 11, 3 * size // 4), "P", fill=hot)
    return img

def tex_door(size):
    """Buyable door: heavy planks with iron bands."""
    img = tex_wood(size, (96, 72, 48), 128, (48, 34, 22), vertical=True, seed=94)
    d = ImageDraw.Draw(img)
    iron = (58, 56, 60)
    for yy in (size // 4, 3 * size // 4):     # iron bands
        d.rectangle([0, yy - 9, size - 1, yy + 9], fill=iron)
        d.rectangle([0, yy - 9, size - 1, yy + 9], outline=(30, 28, 32), width=3)
    for x in range(16, size, 32):             # bolt heads
        for yy in (size // 4, 3 * size // 4):
            d.ellipse([x - 3, yy - 3, x + 3, yy + 3], fill=(96, 92, 96))
    d.ellipse([size - 40, size // 2 - 6, size - 28, size // 2 + 6], fill=(150, 120, 60))
    return img

def gen_textures():
    os.makedirs(TEX_DIR, exist_ok=True)
    S = 256
    out = {}

    out["floor_wood"]   = tex_wood(S, (96, 74, 52), 32, (48, 34, 22), seed=1)
    out["roof_wood"]    = tex_wood(S, (66, 50, 36), 64, (30, 22, 14), vertical=True, seed=2)
    out["stage_wood"]   = tex_wood(S, (120, 92, 62), 64, (60, 42, 26), seed=12)
    out["pew_wood"]     = tex_wood(S, (110, 82, 54), 128, (56, 40, 26), seed=13)
    out["cross_wood"]   = tex_wood(S, (168, 138, 96), 256, (90, 70, 46), seed=14)
    out["crate_wood"]   = tex_wood(S, (124, 96, 62), 86, (54, 38, 24), seed=15)
    out["duckboard"]    = tex_wood(S, (102, 80, 54), 64, (44, 32, 20), seed=16)
    out["barrier_board"]= tex_wood(S, (140, 108, 70), 256, (70, 50, 30), seed=17)

    out["wall_brick"]   = tex_bricks(S, 64, 32, (116, 70, 58), (60, 58, 56))
    out["wall_stone"]   = tex_bricks(S, 86, 43, (110, 112, 108), (52, 52, 50), seed=4)
    out["roof_stone"]   = tex_bricks(S, 64, 43, (62, 66, 70), (30, 32, 34), seed=41)
    out["floor_stone"]  = tex_bricks(S, 128, 64, (98, 96, 92), (46, 46, 44), seed=42)

    out["floor_grass"]  = blend(noise_image(S, (44, 62, 38), 12, 51),
                                value_noise(S, 12, 61), 0.55)
    out["floor_mud"]    = blend(noise_image(S, (86, 68, 50), 12, 52),
                                value_noise(S, 10, 62), 0.6)
    out["trench_wall"]  = blend(noise_image(S, (104, 84, 60), 10, 53),
                                value_noise(S, 8, 63), 0.65)
    out["grave_dirt"]   = blend(noise_image(S, (78, 58, 42), 14, 54),
                                value_noise(S, 9, 64), 0.6)

    out["sandbag"]      = tex_sandbags(S)
    out["curtain_red"]  = tex_curtain(S)
    out["seat_red"]     = blend(noise_image(S, (120, 34, 34), 12, 55),
                                value_noise(S, 14, 65), 0.45)
    out["bark_dark"]    = tex_wood(S, (52, 40, 32), 256, (28, 22, 18), vertical=True, seed=18)
    out["leaves_dark"]  = blend(noise_image(S, (34, 48, 30), 16, 56),
                                value_noise(S, 16, 66), 0.7)

    out["headstone"]    = blend(noise_image(S, (118, 120, 116), 10, 57),
                                value_noise(S, 10, 67), 0.5)

    out["zombie_skin"]  = tex_zombie_skin(S)
    out["zombie_cloth"] = blend(noise_image(S, (58, 60, 54), 14, 58),
                                value_noise(S, 9, 68), 0.65)
    out["wallbuy_chalk"]= tex_chalk_gun(S)
    out["mystery_box"]  = tex_mystery_box(S)

    # perk machines (panel, glow, letter)
    out["perk_quick"]  = tex_perk_machine(S, (36, 70, 92),  (120, 220, 255), "Q", 71)
    out["perk_double"] = tex_perk_machine(S, (110, 62, 22), (255, 170, 70),  "D", 72)
    out["perk_speed"]  = tex_perk_machine(S, (34, 88, 44),  (130, 235, 140), "S", 73)
    out["perk_jugg"]   = tex_perk_machine(S, (104, 30, 30), (255, 90, 80),   "J", 74)
    out["pap"]         = tex_pap(S)
    out["door"]        = tex_door(S)

    for name, img in out.items():
        img.save(os.path.join(TEX_DIR, name + ".png"))
    print(f"textures: wrote {len(out)} files to {TEX_DIR}")
    return sorted(out.keys())

# ============================================================================
# SOUNDS (pure stdlib synthesis, 22050 Hz 16-bit mono WAV)
# ============================================================================

SR = 22050

def env_exp(t, dur, attack=0.002):
    if t < attack:
        return t / attack
    tau = max(dur / 5.0, 0.001)
    return math.exp(-(t - attack) / tau)

def lowpass(samples, a):
    out, y = [], 0.0
    for s in samples:
        y += a * (s - y)
        out.append(y)
    return out

def normalize(samples, peak=0.85):
    m = max(1e-9, max(abs(s) for s in samples))
    k = peak / m
    return [s * k for s in samples]

def write_wav(name, samples):
    os.makedirs(SND_DIR, exist_ok=True)
    path = os.path.join(SND_DIR, name + ".wav")
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        frames = b"".join(struct.pack("<h", int(clamp(s * 32000))) for s in samples)
        w.writeframes(frames)

def rnoise(dur, seed):
    r = random.Random(seed)
    n = int(dur * SR)
    return [r.uniform(-1, 1) for _ in range(n)]

def gunshot(dur, boom_amt, crack_amt, seed, hp=False):
    """Layered noise burst + low thump."""
    noise = rnoise(dur, seed)
    if hp:
        noise = [s - p for s, p in zip(noise, lowpass(noise, 0.08))]  # crude highpass
    body = [noise[i] * env_exp(i / SR, dur) * crack_amt for i in range(len(noise))]
    boom = [math.sin(2 * math.pi * 130 * (i / SR)) * env_exp(i / SR, dur, 0.001) * boom_amt
            for i in range(len(noise))]
    return normalize([b + o for b, o in zip(boom, body)])

def pluck(freq, dur, vol=1.0, decay=None):
    n = int(dur * SR)
    decay = decay or dur / 4
    return [math.sin(2 * math.pi * freq * i / SR) *
            math.exp(-(i / SR) / decay) * vol for i in range(n)]

def mix_at(base, add, t_offset):
    off = int(t_offset * SR)
    while len(base) < off + len(add):
        base.append(0.0)
    for i, s in enumerate(add):
        base[off + i] += s
    return base

def snd_shots():
    return {
        "shot_pistol":  gunshot(0.14, 0.7, 0.9, 101),
        "shot_smg":     gunshot(0.10, 0.55, 1.0, 102, hp=True),
        "shot_rifle":   gunshot(0.24, 1.0, 0.95, 103),
        "shot_ar":      gunshot(0.17, 0.85, 0.95, 104),
        "shot_shotgun": gunshot(0.38, 1.2, 1.0, 105),
        "shot_sniper":  gunshot(0.45, 1.1, 1.1, 106),
        "shot_lmg":     gunshot(0.20, 1.0, 0.9, 107),
        "shot_special": normalize(
            [math.sin(2 * math.pi * (900 - 750 * (i / SR) ** 0.7) * i / SR) *
             env_exp(i / SR, 0.3) for i in range(int(0.3 * SR))] +
            [0.05 * s for s in rnoise(0.1, 108)]),
    }

def snd_features():
    """Perks / Pack-a-Punch / doors / grenades / knife sounds."""
    out = {}
    # perk jingle: bright 3-note ding
    jingle = []
    for i, f in enumerate([523, 784, 1047]):
        jingle = mix_at(jingle, pluck(f, 0.35, 0.6, 0.12), i * 0.11)
    out["perk_buy"] = normalize(jingle)
    # pack-a-punch: heavy industrial arpeggio
    pap = []
    for i, f in enumerate([131, 196, 262, 392]):
        pap = mix_at(pap, pluck(f, 0.5, 0.8, 0.2), i * 0.14)
    pap = mix_at(pap, [s * 0.4 for s in lowpass(rnoise(0.5, 301), 0.15)], 0.0)
    out["pap_buy"] = normalize(pap)
    # door opening: creak sweep + thud
    n = int(0.7 * SR)
    creak = [0.35 * math.sin(2 * math.pi * (170 - 70 * i / n) * i / SR)
             * (0.6 + 0.4 * math.sin(2 * math.pi * 26 * i / SR)) * env_exp(i / SR, 0.7)
             for i in range(n)]
    door = mix_at([s * 0.5 for s in creak], pluck(65, 0.25, 0.9, 0.05), 0.5)
    door = mix_at(door, [s * 0.6 for s in lowpass(rnoise(0.3, 302), 0.2)], 0.55)
    out["door_open"] = normalize(door)
    # grenade throw: short whoosh
    out["grenade_throw"] = normalize(
        [s * env_exp(i / SR, 0.25) for i, s in enumerate(lowpass(rnoise(0.25, 303), 0.45))])
    # grenade bounce: small thud
    out["grenade_bounce"] = normalize(
        [s * env_exp(i / SR, 0.07) for i, s in enumerate(lowpass(rnoise(0.07, 304), 0.2))] +
        pluck(130, 0.08, 0.6, 0.02))
    # explosion: big filtered boom + sub thump + crackle
    n = int(1.4 * SR)
    body = [s * env_exp(i / SR, 1.4)
            for i, s in enumerate(lowpass(rnoise(1.4, 305), 0.12))]
    sub = [0.8 * math.sin(2 * math.pi * 48 * i / SR) * env_exp(i / SR, 0.7)
           for i in range(n)]
    crackle = [s * env_exp(i / SR, 0.35) for i, s in enumerate(rnoise(0.35, 306))]
    out["explosion"] = normalize(mix_at(mix_at(body, sub, 0.0),
                                        [s * 0.7 for s in crackle], 0.03))
    # knife hit: wet slice + thump
    out["knife_hit"] = normalize(
        [0.8 * s * env_exp(i / SR, 0.12) for i, s in enumerate(lowpass(rnoise(0.12, 307), 0.3))] +
        pluck(190, 0.1, 0.7, 0.03))
    return out

def snd_ui_and_world():
    out = {}
    # reload: two mechanical ticks
    tick = lambda: [s * 0.8 for s in lowpass(rnoise(0.03, 201), 0.5)]
    out["reload"] = normalize(mix_at(mix_at([], tick(), 0.0), tick(), 0.13))
    out["reload_end"] = normalize(mix_at(mix_at([], tick(), 0.0),
                                         [s * 0.5 for s in lowpass(rnoise(0.05, 202), 0.4)], 0.02))
    out["dry_fire"] = normalize(tick())
    swish = [s * env_exp(i / SR, 0.14) * 0.4
             for i, s in enumerate(rnoise(0.14, 203))]
    out["weapon_switch"] = normalize(swish)
    out["knife"] = normalize([s * env_exp(i / SR, 0.16)
                              for i, s in enumerate(rnoise(0.16, 204))])
    out["hitmarker"] = normalize(pluck(1250, 0.05, 0.6, 0.008))
    # headshot: squish + thump
    out["headshot"] = normalize(
        [0.6 * s * env_exp(i / SR, 0.1) for i, s in enumerate(lowpass(rnoise(0.1, 205), 0.25))] +
        pluck(75, 0.12, 0.9, 0.03))
    out["deny"] = normalize(pluck(110, 0.08, 0.8, 0.02) + pluck(95, 0.10, 0.8, 0.02))
    out["buy"] = normalize(mix_at(mix_at([], pluck(620, 0.1, 0.7, 0.03), 0.0),
                                  pluck(930, 0.14, 0.7, 0.04), 0.09))
    # box jingle: music-box arpeggio
    jingle = []
    for i, f in enumerate([523, 659, 784, 1047, 784, 1047]):
        jingle = mix_at(jingle, pluck(f, 0.5, 0.5, 0.15), i * 0.16)
    out["box_jingle"] = normalize(jingle)
    # box open: rising shimmer
    n = int(0.6 * SR)
    out["box_open"] = normalize(
        [0.4 * math.sin(2 * math.pi * (300 + 900 * i / n) * i / SR) * env_exp(i / SR, 0.6)
         for i in range(n)])
    # wood break / repair
    crack = [s * env_exp(i / SR, 0.16)
             for i, s in enumerate(lowpass(rnoise(0.16, 206), 0.35))]
    crack = mix_at(crack, [s * 0.7 for s in lowpass(rnoise(0.06, 207), 0.5)], 0.05)
    out["board_break"] = normalize(crack)
    knock = [s * env_exp(i / SR, 0.09)
             for i, s in enumerate(lowpass(rnoise(0.09, 208), 0.3))]
    out["board_repair"] = normalize(mix_at(mix_at([], knock, 0.0),
                                           [s * 0.8 for s in knock], 0.12))
    # round stings
    sting = []
    for f in (82, 82, 110, 98):
        sting = mix_at(sting, pluck(f, 1.4, 0.55, 0.5), len(sting) * 0.0 + (0 if not sting else 0))
    # simpler: descending ominous drone
    n = int(2.0 * SR)
    sting = [0.45 * math.sin(2 * math.pi * 65 * i / SR) * env_exp(i / SR, 2.0, 0.25) +
             0.3 * math.sin(2 * math.pi * 97 * i / SR) * env_exp(i / SR, 2.0, 0.3)
             for i in range(n)]
    out["round_start"] = normalize(sting)
    out["round_end"] = normalize(mix_at(mix_at(mix_at([], pluck(392, 0.3, 0.5, 0.1), 0.0),
                                               pluck(311, 0.3, 0.5, 0.1), 0.18),
                                        pluck(233, 0.5, 0.5, 0.15), 0.36))
    return out

def snd_player_zombie():
    out = {}
    # player hurt / death
    n = int(0.25 * SR)
    out["player_hurt"] = normalize(
        [0.5 * math.sin(2 * math.pi * (180 - 60 * i / n) * i / SR) * env_exp(i / SR, 0.25)
         for i in range(n)])
    n = int(0.9 * SR)
    out["player_death"] = normalize(
        [0.5 * math.sin(2 * math.pi * (160 - 100 * i / n) * i / SR) * env_exp(i / SR, 0.9)
         for i in range(n)])

    # zombie voice: wobbling low growl
    def groan(dur, f0, f1, seed, breath=0.25):
        n = int(dur * SR)
        r = random.Random(seed)
        phase = 0.0
        res = []
        for i in range(n):
            t = i / SR
            f = f0 + (f1 - f0) * (t / dur)
            f += 8 * math.sin(2 * math.pi * 5.5 * t)
            phase += 2 * math.pi * f / SR
            s = math.sin(phase) * 0.6 + math.sin(phase * 2) * 0.25
            s = s * env_exp(t, dur, 0.05) + breath * (r.uniform(-1, 1)) * env_exp(t, dur, 0.02)
            res.append(s)
        return normalize(res)

    out["zombie_groan"]  = groan(0.8, 120, 85, 301)
    out["zombie_attack"] = groan(0.35, 160, 110, 302, breath=0.45)
    out["zombie_death"]  = groan(0.6, 190, 55, 303, breath=0.35)
    out["zombie_hit"] = normalize(
        [0.7 * s * env_exp(i / SR, 0.09)
         for i, s in enumerate(lowpass(rnoise(0.09, 304), 0.2))])
    n = int(1.1 * SR)
    out["zombie_rise"] = normalize(
        [0.6 * s * (i / n) for i, s in enumerate(lowpass(rnoise(1.1, 305), 0.06))])
    out["zombie_land"] = normalize(mix_at(
        [s * env_exp(i / SR, 0.15) for i, s in enumerate(lowpass(rnoise(0.15, 306), 0.12))],
        pluck(60, 0.2, 0.8, 0.05), 0.0))
    return out

def snd_menu_theme():
    """Dark ambient loop, ~16s, fade-looped at the ends."""
    dur = 16.0
    n = int(dur * SR)
    r = random.Random(404)
    out = []
    for i in range(n):
        t = i / SR
        s = 0.22 * math.sin(2 * math.pi * 55 * t)
        s += 0.16 * math.sin(2 * math.pi * 82.5 * t + 0.4 * math.sin(2 * math.pi * 0.13 * t))
        s += 0.08 * math.sin(2 * math.pi * 110.3 * t) * (0.5 + 0.5 * math.sin(2 * math.pi * 0.07 * t))
        # sparse deep hits
        for hit_t in (2.0, 6.5, 11.0):
            dt = t - hit_t
            if 0 <= dt < 1.2:
                s += 0.35 * math.sin(2 * math.pi * 42 * dt) * math.exp(-dt / 0.3)
        # vinyl-ish hiss
        s += 0.015 * r.uniform(-1, 1)
        # loop fade
        fade = min(1.0, t / 1.5, (dur - t) / 1.5)
        out.append(s * fade)
    return normalize(out)

def gen_sounds():
    all_sounds = {}
    all_sounds.update(snd_shots())
    all_sounds.update(snd_ui_and_world())
    all_sounds.update(snd_player_zombie())
    all_sounds.update(snd_features())
    all_sounds["menu_theme"] = snd_menu_theme()

    for name, samples in all_sounds.items():
        write_wav(name, samples)
    print(f"sounds: wrote {len(all_sounds)} files to {SND_DIR}")
    return sorted(all_sounds.keys())

# ============================================================================
# entry point
# ============================================================================

def main():
    ap = argparse.ArgumentParser(description="Dead Arrival procedural asset generator")
    ap.add_argument("--check", action="store_true", help="only generate missing files")
    ap.add_argument("--force", action="store_true", help="regenerate everything")
    args = ap.parse_args()

    os.makedirs(TEX_DIR, exist_ok=True)
    os.makedirs(SND_DIR, exist_ok=True)

    texes = ["floor_wood", "roof_wood", "stage_wood", "pew_wood", "cross_wood",
             "crate_wood", "duckboard", "barrier_board", "wall_brick", "wall_stone",
             "roof_stone", "floor_stone", "floor_grass", "floor_mud", "trench_wall",
             "grave_dirt", "sandbag", "curtain_red", "seat_red", "bark_dark",
             "leaves_dark", "headstone", "zombie_skin", "zombie_cloth",
             "wallbuy_chalk", "mystery_box", "perk_quick", "perk_double",
             "perk_speed", "perk_jugg", "pap", "door"]
    snds = ["shot_pistol", "shot_smg", "shot_rifle", "shot_ar", "shot_shotgun",
            "shot_sniper", "shot_lmg", "shot_special", "reload", "reload_end",
            "dry_fire", "weapon_switch", "knife", "knife_hit", "hitmarker",
            "headshot", "deny", "buy", "box_jingle", "box_open", "board_break",
            "board_repair", "round_start", "round_end", "player_hurt",
            "player_death", "zombie_groan", "zombie_attack", "zombie_hit",
            "zombie_death", "zombie_rise", "zombie_land", "perk_buy", "pap_buy",
            "door_open", "grenade_throw", "grenade_bounce", "explosion",
            "menu_theme"]

    if args.check and not args.force:
        missing_t = [t for t in texes
                     if not os.path.exists(os.path.join(TEX_DIR, t + ".png"))]
        missing_s = [s for s in snds
                     if not os.path.exists(os.path.join(SND_DIR, s + ".wav"))]
        if not missing_t and not missing_s:
            print("assets: all present, nothing to do")
            return
        print(f"assets: missing textures: {missing_t}")
        print(f"assets: missing sounds: {missing_s}")
        print("run: python3 tools/gen_assets.py")
        return

    gen_textures()
    gen_sounds()
    print("assets: generation complete")

if __name__ == "__main__":
    main()
