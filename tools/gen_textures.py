#!/usr/bin/env python3
"""Generate all texture PNGs for Dead Arrival (procedural, PIL)."""
import os
from PIL import Image, ImageDraw, ImageFilter
import math, random

OUT = '/home/z/my-project/Dead-Arrival/textures'
os.makedirs(OUT, exist_ok=True)
random.seed(42)
S = 256

def noise(amount, base, lo=0, hi=255):
    return tuple(max(lo, min(hi, c + random.randint(-amount, amount))) for c in base)

def save(img, name):
    img.save(f'{OUT}/{name}.png')
    print(name)

# brick
img = Image.new('RGB', (S, S), (120, 72, 58))
d = ImageDraw.Draw(img)
bh, bw = 32, 64
for row in range(S // bh):
    off = (row % 2) * (bw // 2)
    for col in range(-1, S // bw + 1):
        x = col * bw + off
        if x + bw - 2 < 0 or x + 2 > S: continue
        c = noise(14, (120, 72, 58))
        d.rectangle([x+2, row*bh+2, x+bw-2, (row+1)*bh-2], fill=c)
save(img, 'brick')

# wood (planks with grain)
img = Image.new('RGB', (S, S), (110, 80, 50))
d = ImageDraw.Draw(img)
for y in range(S):
    for x in range(0, S, 4):
        g = 110 + int(18 * math.sin(x*0.05 + y*0.15) + random.randint(-6, 6))
        d.line([(x, y), (x+4, y)], fill=(g, int(g*0.72), int(g*0.45)))
for pl in range(0, S, 64):
    d.line([(0, pl), (S, pl)], fill=(70, 50, 30), width=2)
save(img, 'wood')

# plank (rough horizontal boards)
img = Image.new('RGB', (S, S), (130, 98, 62))
d = ImageDraw.Draw(img)
for pl in range(0, S, 42):
    c = noise(10, (130, 98, 62))
    d.rectangle([0, pl, S, pl+40], fill=c)
    for i in range(30):
        x = random.randint(0, S)
        d.line([(x, pl+3), (x + random.randint(-8, 8), pl+37)], fill=(100, 74, 46))
save(img, 'plank')

# stone
img = Image.new('RGB', (S, S), (105, 103, 98))
d = ImageDraw.Draw(img)
for row in range(4):
    off = (row % 2) * 32
    for col in range(-1, 5):
        x = col*64 + off
        if x + 61 < 0 or x + 3 > S: continue
        c = noise(16, (105, 103, 98))
        d.rectangle([x+3, row*64+3, x+61, row*64+61], fill=c, outline=(80, 78, 74))
save(img, 'stone')

# marble (dark veined)
img = Image.new('RGB', (S, S), (200, 196, 186))
d = ImageDraw.Draw(img)
for i in range(14):
    x, y = random.randint(0, S), random.randint(0, S)
    pts = [(x, y)]
    for seg in range(14):
        x += random.randint(-22, 22); y += random.randint(-22, 22)
        pts.append((x, y))
    d.line(pts, fill=(150, 145, 140), width=2)
save(img, 'marble')

# velvet
img = Image.new('RGB', (S, S), (96, 16, 28))
d = ImageDraw.Draw(img)
for y in range(S):
    for x in range(0, S, 2):
        v = 96 + int(14 * math.sin(x*0.22))
        d.line([(x, y), (x+2, y)], fill=(v, int(v*0.17), int(v*0.29)))
save(img, 'velvet')

# curtain (vertical folds)
img = Image.new('RGB', (S, S), (90, 14, 22))
d = ImageDraw.Draw(img)
for x in range(S):
    v = int(70 + 45 * math.sin(x*0.09)**2)
    d.line([(x, 0), (x, S)], fill=(v, int(v*0.16), int(v*0.24)))
save(img, 'curtain')

# concrete
img = Image.new('RGB', (S, S), (118, 116, 112))
d = ImageDraw.Draw(img)
for i in range(2600):
    x, y = random.randint(0, S-1), random.randint(0, S-1)
    v = random.randint(100, 132)
    d.point((x, y), fill=(v, v-2, v-5))
save(img, 'concrete')

# metal
img = Image.new('RGB', (S, S), (110, 114, 122))
d = ImageDraw.Draw(img)
for x in range(S):
    v = 104 + int(10 * math.sin(x*0.3))
    d.line([(x, 0), (x, S)], fill=(v, v+4, v+12))
save(img, 'metal')

# rust
img = Image.new('RGB', (S, S), (122, 66, 38))
d = ImageDraw.Draw(img)
for i in range(2400):
    x, y = random.randint(0, S-1), random.randint(0, S-1)
    r = random.randint(100, 150)
    d.point((x, y), fill=(r, int(r*0.5), int(r*0.28)))
save(img, 'rust')

# sandbag
img = Image.new('RGB', (S, S), (150, 132, 94))
d = ImageDraw.Draw(img)
for row in range(4):
    off = (row % 2) * 32
    for col in range(-1, 5):
        x = col*64 + off
        if x + 62 < 0 or x + 2 > S: continue
        d.ellipse([x+2, row*64+4, x+62, row*64+60], fill=noise(12, (150, 132, 94)), outline=(120, 104, 72))
save(img, 'sandbag')

# grass
img = Image.new('RGB', (S, S), (58, 84, 38))
d = ImageDraw.Draw(img)
for i in range(3000):
    x, y = random.randint(0, S-1), random.randint(0, S-1)
    g = random.randint(60, 105)
    d.point((x, y), fill=(int(g*0.7), g, int(g*0.45)))
save(img, 'grass')

# dirt
img = Image.new('RGB', (S, S), (96, 74, 50))
d = ImageDraw.Draw(img)
for i in range(2800):
    x, y = random.randint(0, S-1), random.randint(0, S-1)
    v = random.randint(76, 112)
    d.point((x, y), fill=(v, int(v*0.76), int(v*0.5)))
save(img, 'dirt')

# grave (granite)
img = Image.new('RGB', (S, S), (138, 138, 134))
d = ImageDraw.Draw(img)
for i in range(2000):
    x, y = random.randint(0, S-1), random.randint(0, S-1)
    v = random.randint(120, 150)
    d.point((x, y), fill=(v, v, v-4))
d.line([(0, 40), (S, 40)], fill=(110, 110, 106), width=3)
d.line([(0, 200), (S, 200)], fill=(110, 110, 106), width=3)
save(img, 'grave')

# glass
img = Image.new('RGBA', (S, S), (160, 190, 200, 120))
d = ImageDraw.Draw(img)
for i in range(10):
    x = random.randint(0, S)
    d.line([(x, 0), (x + random.randint(-40, 40), S)], fill=(200, 220, 230, 60), width=2)
save(img, 'glass')

# roof (shingles)
img = Image.new('RGB', (S, S), (70, 52, 44))
d = ImageDraw.Draw(img)
for row in range(8):
    off = (row % 2) * 16
    for col in range(9):
        x = col*32 + off - 16
        if x + 30 < 0 or x > S: continue
        d.rectangle([max(0, x), row*32, min(S, x+30), row*32+28], fill=noise(8, (70, 52, 44)))
save(img, 'roof')

# stucco
img = Image.new('RGB', (S, S), (178, 168, 148))
d = ImageDraw.Draw(img)
for i in range(3200):
    x, y = random.randint(0, S-1), random.randint(0, S-1)
    v = random.randint(160, 190)
    d.point((x, y), fill=(v, v-10, v-30))
save(img, 'stucco')

# tile (checker)
img = Image.new('RGB', (S, S), (168, 160, 146))
d = ImageDraw.Draw(img)
for row in range(8):
    for col in range(8):
        c = (168, 160, 146) if (row+col) % 2 else (140, 130, 118)
        d.rectangle([col*32, row*32, col*32+31, row*32+31], fill=c)
save(img, 'tile')

# asphalt
img = Image.new('RGB', (S, S), (72, 72, 76))
d = ImageDraw.Draw(img)
for i in range(3600):
    x, y = random.randint(0, S-1), random.randint(0, S-1)
    v = random.randint(60, 84)
    d.point((x, y), fill=(v, v, v+4))
save(img, 'asphalt')

# zombie skin
img = Image.new('RGB', (S, S), (138, 148, 118))
d = ImageDraw.Draw(img)
for i in range(2600):
    x, y = random.randint(0, S-1), random.randint(0, S-1)
    g = random.randint(115, 165)
    d.point((x, y), fill=(int(g*0.85), g, int(g*0.75)))
for i in range(8):  # wounds
    x, y = random.randint(20, S-20), random.randint(20, S-20)
    d.ellipse([x, y, x+random.randint(10, 26), y+random.randint(10, 26)], fill=(110, 60, 55))
save(img, 'zombie')

# box (mystery box wood + glow runes)
img = Image.new('RGB', (S, S), (110, 78, 40))
d = ImageDraw.Draw(img)
d.rectangle([0, 0, S, 20], fill=(80, 54, 28))
d.rectangle([0, S-20, S, S], fill=(80, 54, 28))
d.rectangle([0, 0, 20, S], fill=(80, 54, 28))
d.rectangle([S-20, 0, S, S], fill=(80, 54, 28))
# rune circle
d.ellipse([68, 68, 188, 188], outline=(255, 210, 120), width=4)
d.ellipse([88, 88, 168, 168], outline=(255, 190, 90), width=2)
for i in range(8):
    a = i * math.pi / 4
    x1 = 128 + 52*math.cos(a); y1 = 128 + 52*math.sin(a)
    x2 = 128 + 34*math.cos(a); y2 = 128 + 34*math.sin(a)
    d.line([(x1, y1), (x2, y2)], fill=(255, 220, 140), width=3)
save(img, 'box')

# particle (soft white dot)
img = Image.new('RGBA', (S, S), (0, 0, 0, 0))
d = ImageDraw.Draw(img)
for r in range(S//2, 0, -2):
    a = int(240 * (1 - r/(S/2))**2)
    d.ellipse([S/2-r, S/2-r, S/2+r, S/2+r], fill=(255, 255, 255, a))
save(img, 'particle')

# flash (muzzle)
img = Image.new('RGBA', (S, S), (0, 0, 0, 0))
d = ImageDraw.Draw(img)
for i in range(12):
    a = i * math.pi / 6
    d.polygon([(128, 128),
               (128 + 120*math.cos(a - 0.1), 128 + 120*math.sin(a - 0.1)),
               (128 + 60*math.cos(a), 128 + 60*math.sin(a)),
               (128 + 120*math.cos(a + 0.1), 128 + 120*math.sin(a + 0.1))],
              fill=(255, 230, 150, 200))
d.ellipse([108, 108, 148, 148], fill=(255, 255, 230, 255))
save(img, 'flash')

# blood vignette
img = Image.new('RGBA', (S, S), (0, 0, 0, 0))
d = ImageDraw.Draw(img)
for i in range(40):
    x, y = random.randint(-30, S), random.randint(-30, S)
    r = random.randint(4, 20)
    a = random.randint(30, 90)
    d.ellipse([x, y, x+r, y+r], fill=(120, 0, 0, a))
img = img.filter(ImageFilter.GaussianBlur(6))
# darken edges heavily
vign = Image.new('L', (S, S), 0)
dv = ImageDraw.Draw(vign)
for i in range(9):
    m = i * 14
    dv.rectangle([m, m, S-m, S-m], outline=max(0, 200 - i*24), width=10)
dark = Image.new('RGBA', (S, S), (90, 0, 0, 255))
img = Image.composite(dark, img, vign.point(lambda p: 255 - p))
save(img, 'bloodvign')

print("ALL TEXTURES DONE")
