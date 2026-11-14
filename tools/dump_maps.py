#!/usr/bin/env python3
"""Dump the five map layouts from the old C builders into assets/maps/*.json.

One-shot migration tool: reads src/maps.c's builder functions, which this
script mirrors in Python (same coordinates, same helper semantics), and
emits the JSON schema the game now loads. Kept in tools/ so a map can be
re-derived if its JSON is lost; edit the JSON files for map changes now.
"""
import json
import math
import os

OUT = os.path.join(os.path.dirname(__file__), "..", "assets", "maps")
os.makedirs(OUT, exist_ok=True)

TEX = ["brick", "wood", "plank", "stone", "marble", "velvet", "curtain",
       "concrete", "metal", "rust", "sandbag", "grass", "dirt", "grave",
       "glass", "roof", "stucco", "tile", "asphalt"]
TEXID = {t: i for i, t in enumerate(TEX)}

class Map:
    def __init__(self, **meta):
        self.m = dict(meta)
        self.m["boxes"] = []
        self.m["doors"] = []
        self.m["spawns"] = []
        self.m["wallbuys"] = []
        self.m["perks"] = []
        self.m["pads"] = []
        self.m["ziplines"] = []
        self.m["killzones"] = []

    def B(self, x0, y0, z0, x1, y1, z1, tex):
        if x1 < x0: x0, x1 = x1, x0
        if y1 < y0: y0, y1 = y1, y0
        if z1 < z0: z0, z1 = z1, z0
        self.m["boxes"].append({"min": [x0, y0, z0], "max": [x1, y1, z1], "tex": tex})

    def DC(self, x0, y0, z0, x1, y1, z1, tex):
        if x1 < x0: x0, x1 = x1, x0
        if z1 < z0: z0, z1 = z1, z0
        self.m["boxes"].append({"min": [x0, y0, z0], "max": [x1, y1, z1], "tex": tex, "noCollide": True})

    def BDoor(self, door, x0, y0, z0, x1, y1, z1, tex):
        self.m["boxes"].append({"min": [x0, y0, z0], "max": [x1, y1, z1], "tex": tex, "door": door})

    def Door(self, name, price, cx, cy, cz):
        self.m["doors"].append({"name": name, "price": price, "pos": [cx, cy, cz]})

    def Spawn(self, x, z, door, grave, y):
        self.m["spawns"].append({"pos": [x, y, z], "door": door, "grave": grave})

    def WallBuy(self, gun, x, y, z, yaw):
        self.m["wallbuys"].append({"gun": gun, "pos": [x, y, z], "yaw": yaw})

    def Perk(self, perk, x, y, z, yaw):
        self.m["perks"].append({"perk": perk, "pos": [x, y, z], "yaw": yaw})

    def Pad(self, x, y, z, ix, iy, iz):
        self.m["pads"].append({"pos": [x, y, z], "impulse": [ix, iy, iz]})

    def Zip(self, a, b):
        self.m["ziplines"].append({"a": list(a), "b": list(b)})

    def Kill(self, x0, z0, x1, z1, yTop):
        self.m["killzones"].append({"min": [x0, -999, z0], "max": [x1, yTop, z1]})

    # wall helpers (same semantics as maps.c)
    def WallX(self, x0, x1, z, h, t, tex, gapC=0, gapH=0):
        if gapH > 0:
            if gapC - gapH > x0: self.B(x0, 0, z - t/2, gapC - gapH, h, z + t/2, tex)
            if x1 > gapC + gapH: self.B(gapC + gapH, 0, z - t/2, x1, h, z + t/2, tex)
            if h > 2.4: self.B(gapC - gapH, 2.4, z - t/2, gapC + gapH, h, z + t/2, tex)
        else:
            self.B(x0, 0, z - t/2, x1, h, z + t/2, tex)

    def WallZ(self, z0, z1, x, h, t, tex, gapC=0, gapH=0):
        if gapH > 0:
            if gapC - gapH > z0: self.B(x - t/2, 0, z0, x + t/2, h, gapC - gapH, tex)
            if z1 > gapC + gapH: self.B(x - t/2, 0, gapC + gapH, x + t/2, h, z1, tex)
            if h > 2.4: self.B(x - t/2, 2.4, gapC - gapH, x + t/2, h, gapC + gapH, tex)
        else:
            self.B(x - t/2, 0, z0, x + t/2, h, z1, tex)

    def Stairs(self, x, z, dx, dz, w, total, steps, tex, yBase):
        rise = total / steps
        run = 0.55
        for i in range(steps):
            top = yBase + (i + 1) * rise
            if abs(dx) > 0.5:
                if dx > 0: x0 = x + i * run; x1 = x0 + run
                else:      x1 = x - i * run; x0 = x1 - run
                self.B(x0, yBase - 0.6, z - w/2, x1, top, z + w/2, tex)
            else:
                if dz > 0: z0 = z + i * run; z1 = z0 + run
                else:      z1 = z - i * run; z0 = z1 - run
                self.B(x - w/2, yBase - 0.6, z0, x + w/2, top, z1, tex)

    def save(self, fname):
        with open(os.path.join(OUT, fname + ".json"), "w") as f:
            json.dump(self.m, f, indent=1)
        print("wrote", fname, len(self.m["boxes"]), "boxes")

# ===========================================================================
# MAP 1: THE GRAND THEATER  (from BuildTheater)
# ===========================================================================
m = Map(name="The Grand Theater", desc="The curtain rose on the end of the world. Hold the stage.",
        difficulty=2, playerStart=[0, 0, 17.5], playerYaw=180, boxPos=[0, 1.2, -14.5],
        skyTop=[74, 84, 122], skyHorizon=[165, 135, 160], fogColor=[110, 96, 130], fogDensity=0.012,
        sunDir=[-0.35, -0.8, 0.45], sunColor=[190, 170, 200], ambient=[130, 118, 145],
        fillDir=[0.35, -0.6, -0.45], fillColor=[105, 95, 125])
WALLH = 7
# lobby
m.B(-9.5, -1, 10.5, 9.5, 0, 24.5, "marble")
m.WallX(-9.5, 9.5, 24.2, WALLH, 0.4, "velvet")
m.WallX(-9.5, -3.5, 10.8, WALLH, 0.4, "velvet")
m.WallX(3.5, 9.5, 10.8, WALLH, 0.4, "velvet")
m.WallZ(10.8, 24.2, -9.3, WALLH, 0.4, "velvet", 17.5, 1.6)
m.WallZ(10.8, 24.2, 9.3, WALLH, 0.4, "velvet")
m.DC(-2.0, 0, 23.0, -0.6, 1.1, 24.0, "wood")
m.DC(0.6, 0, 23.0, 2.0, 1.1, 24.0, "wood")
m.DC(-1.4, 0.012, 11.2, 1.4, 0.03, 23.8, "velvet")
# door 0 lobby<->auditorium
m.Door("Auditorium Doors", 750, 0, 1.2, 10.8)
m.BDoor(0, -3.3, 0, 10.6, -0.1, 2.4, 11.0, "plank")
m.BDoor(0, 0.1, 0, 10.6, 3.3, 2.4, 11.0, "plank")
# stairs lobby->balcony + balcony
m.Stairs(-7.6, 17.5, 0, -1, 1.8, 4.2, 8, "marble", 0)
m.B(-9.5, 4.2, 10.6, -4.6, 4.35, 13.1, "marble")
m.B(-9.5, 4.35, 13.1, -8.5, 5.3, 13.4, "velvet")
m.B(-6.7, 4.35, 13.1, -4.6, 5.3, 13.4, "velvet")
m.B(-4.9, 4.35, 10.6, -4.6, 5.3, 13.1, "velvet")
m.B(-9.8, 4.35, 10.6, -9.5, 5.3, 13.4, "velvet")
# auditorium
m.B(-14.2, -1, -12.2, 14.2, 0, 10.6, "marble")
m.WallX(-14.2, 14.2, -12.4, WALLH+3, 0.4, "velvet")
m.WallZ(-12.4, 10.6, -14.0, WALLH, 0.4, "velvet")
m.WallZ(-12.4, -6.0, 14.0, WALLH, 0.4, "velvet")
m.WallZ(-2.0, 10.6, 14.0, WALLH, 0.4, "velvet")
for row in range(5):
    z = 3.0 - row * 1.6
    for s in range(8):
        x = -12.0 + s * 3.1
        m.DC(x, 0, z, x + 1.6, 0.55 + row * 0.12, z + 1.1, "velvet")
m.DC(-1.5, 6.4, -1.5, 1.5, 7.0, 1.5, "metal")
m.B(-14.0, 4.2, -2.0, -9.6, 4.35, 10.6, "marble")
m.B(-9.9, 4.35, -2.0, -9.6, 5.3, 10.6, "velvet")
m.B(-14.0, 4.35, -2.3, -9.6, 5.3, -2.0, "velvet")
m.B(-12.9, 4.35, 10.3, -9.6, 5.3, 10.6, "velvet")
# stage
m.B(-14.2, 0, -20.0, 14.2, 1.2, -12.4, "plank")
m.Stairs(-13.0, -12.4, 0, -1, 2.0, 1.2, 3, "plank", 0)
m.Stairs(13.0, -12.4, 0, -1, 2.0, 1.2, 3, "plank", 0)
m.WallZ(-20.0, -12.4, -14.0, WALLH+3, 0.4, "velvet")
m.WallZ(-20.0, -12.4, 14.0, WALLH+3, 0.4, "velvet")
m.DC(-14.2, 4.6, -12.8, 14.2, 5.4, -12.4, "velvet")
m.DC(-13.8, 1.2, -19.6, -10.0, 6.6, -19.2, "curtain")
m.DC(10.0, 1.2, -19.6, 13.8, 6.6, -19.2, "curtain")
m.DC(-10.0, 6.2, -19.6, 10.0, 6.9, -19.2, "curtain")
m.B(-14.2, 1.2, -13.6, 14.2, 1.5, -13.4, "wood")
# door 1 backstage
m.Door("Backstage Door", 1000, 0, 1.2, -20.2)
m.BDoor(1, -1.3, 0.0, -20.4, 1.3, 2.4, -19.9, "plank")
m.B(-8.2, -1, -26.2, 8.2, 0, -20.0, "plank")
m.WallX(-8.2, -1.5, -20.2, 5, 0.4, "plank")
m.WallX(1.5, 8.2, -20.2, 5, 0.4, "plank")
m.B(-1.5, 2.4, -20.4, 1.5, 5.0, -19.9, "plank")
m.B(-5.4, 0, -20.55, 5.4, 1.2, -19.9, "plank")
m.WallX(-8.2, 8.2, -26.4, 5, 0.4, "plank")
m.WallZ(-26.4, -20.2, -8.0, 5, 0.4, "plank")
m.Stairs(5.4, -21.65, 0, 1, 1.8, 1.2, 3, "plank", 0)
m.Stairs(-5.4, -21.65, 0, 1, 1.8, 1.2, 3, "plank", 0)
m.DC(-7.0, 0, -25.5, -5.5, 2.2, -24.0, "wood")
m.DC(5.5, 0, -25.5, 7.0, 2.2, -24.0, "wood")
# door 2 alley
m.Door("Alley Door", 1000, 14.0, 1.2, -4.0)
m.BDoor(2, 13.8, 0, -5.9, 14.2, 2.4, -4.4, "plank")
m.BDoor(2, 13.8, 0, -3.6, 14.2, 2.4, -2.1, "plank")
m.B(14.2, -1, -12.0, 20.4, 0, 10.6, "asphalt")
m.WallX(14.2, 20.4, -12.2, 6, 0.4, "brick")
m.WallX(14.2, 20.4, 10.8, 6, 0.4, "brick")
m.WallZ(-12.2, 10.8, 20.2, 6, 0.4, "brick")
m.WallZ(-12.2, -6.0, 14.2, 6, 0.4, "brick")
m.WallZ(-2.0, 10.8, 14.2, 6, 0.4, "brick")
m.DC(-14.2, 9.9, -26.2, 20.4, 10.2, 24.5, "velvet")
# wall buys / perks
m.WallBuy("olympia", -8.9, 1.5, 23.6, 90)
m.WallBuy("mp5k", -13.7, 1.5, 2.0, 90)
m.WallBuy("stakeout", 7.0, 1.5, -25.9, 0)
m.WallBuy("ak74u", 19.9, 1.5, 4.0, 270)
m.WallBuy("m16", -13.7, 5.7, 8.4, 90)
m.Perk("revive", 8.2, 0, 22.8, 270)
m.Perk("rapid", -12.9, 0, -3.0, 90)
m.Perk("swift", -6.8, 0, -25.4, 0)
m.Perk("regen", 19.4, 0, -10.8, 0)
m.Perk("stamin", -12.0, 4.2, 6.0, 90)
# spawns
m.Spawn(0, 12.6, -1, False, 0)
m.Spawn(8.6, 12.6, -1, False, 0)
m.Spawn(-13.2, -6.0, -1, False, 0)
m.Spawn(-13.2, 8.0, -1, False, 0)
m.Spawn(6.0, -21.4, 1, False, 0)
m.Spawn(-6.0, -21.4, 1, False, 0)
m.Spawn(19.2, -8.0, 2, False, 0)
m.Spawn(19.2, 8.0, 2, False, 0)
m.Spawn(19.2, 2.0, 2, False, 0)
m.save("theater")

# ===========================================================================
# MAP 2: BUNKER 44  (from BuildBunker)
# ===========================================================================
m = Map(name="Bunker 44", desc="Holding position in the ruins of the final offensive.",
        difficulty=3, playerStart=[0, 0, 7], playerYaw=180, boxPos=[-27, 0, 4],
        skyTop=[96, 88, 74], skyHorizon=[158, 128, 88], fogColor=[128, 110, 84], fogDensity=0.018,
        sunDir=[0.5, -0.7, -0.3], sunColor=[235, 205, 155], ambient=[140, 128, 105],
        fillDir=[-0.5, -0.5, 0.3], fillColor=[120, 105, 85])
m.B(-32, -1, -28, 32, 0, 28, "dirt")
for i in range(32):
    x = -32 + i * 2
    m.B(x, 0, -28, x + 2, 1.7, -27.6, "sandbag")
    m.B(x, 0, 27.6, x + 2, 1.7, 28, "sandbag")
for i in range(28):
    z = -28 + i * 2
    m.B(-32, 0, z, -31.6, 1.7, z + 2, "sandbag")
    m.B(31.6, 0, z, 32, 1.7, z + 2, "sandbag")
# command bunker
m.B(-6, -0.2, 2, 6, 0, 12, "concrete")
m.WallX(-6, -1.5, 2.2, 3.2, 0.5, "concrete")
m.WallX(1.5, 6, 2.2, 2.6, 0.5, "concrete")
m.B(-1.5, 2.4, 1.95, 1.5, 3.2, 2.45, "concrete")
m.B(1.5, 2.4, 1.95, 6.0, 3.2, 2.45, "concrete")
m.WallX(-6, 6, 11.8, 3.2, 0.5, "concrete")
m.WallZ(2.2, 11.8, -5.8, 3.2, 0.5, "concrete")
m.WallZ(2.2, 11.8, 5.8, 3.2, 0.05, "concrete")
m.B(-6, 3.2, 2, 6, 3.5, 12, "concrete")
m.DC(-5.2, 0.75, 10.4, -3.4, 1.05, 11.4, "wood")
m.DC(-4.9, 1.05, 10.6, -4.0, 1.35, 11.2, "metal")
# trenches
for i in range(11):
    z = 2 - i * 2
    m.B(-2.2, 0, z - 2, -1.8, 1.6, z, "sandbag")
    m.B(1.8, 0, z - 2, 2.2, 1.6, z, "sandbag")
for i in range(12):
    x = 2 + i * 2
    m.B(x, 0, -6.2, x + 2, 1.6, -5.8, "sandbag")
    m.B(x, 0, -2.2, x + 2, 1.6, -1.8, "sandbag")
for i in range(12):
    x = -2 - i * 2
    m.B(x - 2, 0, 3.5, x, 1.6, 3.9, "sandbag")
    m.B(x - 2, 0, 7.1, x, 1.6, 7.5, "sandbag")
for i in range(6):
    z = 12 + i * 2
    m.B(-2.2, 0, z, -1.8, 1.6, z + 2, "sandbag")
    m.B(1.8, 0, z, 2.2, 1.6, z + 2, "sandbag")
# bunker A west, door 0
m.Door("West Bunker Door", 750, -27.9, 1.2, 5.5)
m.BDoor(0, -28.3, 0, 4.1, -27.9, 2.4, 6.9, "metal")
m.B(-30, -0.2, -2, -24, 0, 10, "concrete")
m.WallX(-30, -24, -2.2, 3.4, 0.5, "concrete")
# bunker A continued
m.WallX(-30, -28.3, 9.8, 3.4, 0.5, "concrete")
m.WallX(-27.9, -24, 9.8, 3.4, 0.5, "concrete")
m.B(-28.3, 2.4, 9.55, -27.9, 3.4, 10.05, "concrete")
m.WallZ(-2.2, 9.8, -30.2, 3.4, 0.5, "concrete")
m.WallZ(-2.2, 4.1, -24.2, 3.4, 0.5, "concrete")
m.WallZ(6.9, 9.8, -24.2, 3.4, 0.5, "concrete")
m.B(-30, 3.4, -2, -24, 3.7, 10, "concrete")
m.DC(-29.6, 0, 8.6, -29.0, 1.6, 9.4, "sandbag")
# bunker B north, door 1
m.Door("North Bunker Door", 1000, 0, 1.2, -20.3)
m.BDoor(1, -1.4, 0, -20.6, 1.4, 2.4, -19.9, "metal")
m.B(-10, -0.2, -26, 10, 0, -20, "concrete")
m.WallX(-10, -1.5, -26.2, 3.4, 0.5, "concrete")
m.WallX(1.5, 10, -26.2, 3.4, 0.5, "concrete")
m.B(-1.5, 2.4, -26.45, 1.5, 3.4, -25.95, "concrete")
m.WallZ(-26.2, -20.2, -10.2, 3.4, 0.5, "concrete")
m.WallZ(-26.2, -20.2, 10.2, 3.4, 0.5, "concrete")
m.B(-10, 3.4, -26, 10, 3.7, -20, "concrete")
m.WallX(-10, -7.8, -22.4, 3.4, 0.4, "concrete")
m.WallX(-6.2, -4, -22.4, 3.4, 0.4, "concrete")
m.B(-7.8, 2.4, -22.6, -6.2, 3.4, -22.2, "concrete")
m.DC(-9.6, 0.8, -25.4, -8.4, 1.1, -24.6, "wood")
m.DC(-8.0, 0, -25.6, -6.8, 1.3, -24.4, "wood")
# ammo depot east, door 2
m.Door("Ammo Depot Door", 1250, 25.9, 1.2, -4.0)
m.BDoor(2, 25.8, 0, -6.1, 26.2, 2.4, -4.4, "metal")
m.BDoor(2, 25.8, 0, -3.6, 26.2, 2.4, -1.9, "metal")
m.B(20, -0.2, -12, 30, 0, 4, "concrete")
m.WallX(20, 30, -12.2, 3.4, 0.5, "concrete")
m.WallX(20, 30, 4.2, 3.4, 0.5, "concrete")
m.WallZ(-12.2, 4.2, 30.2, 3.4, 0.5, "concrete")
m.WallZ(-12.2, -6.1, 19.8, 3.4, 0.5, "concrete")
m.WallZ(-1.9, 4.2, 19.8, 2.6, 0.05, "concrete")
m.B(20, 3.4, -12, 30, 3.7, 4, "concrete")
for i in range(4):
    m.DC(21+i*2.2, 0, -10.5, 22.4+i*2.2, 1.4, -9.1, "wood")
# craters + tank decor
for (cx, cz) in [(-14,-18),(10,-16),(14,8),(-18,14),(8,18),(-22,-10),(16,-22),(24,14),(-8,-24),(20,22)]:
    m.DC(cx-1.2, 0, cz-1.2, cx+1.2, 0.18, cz+1.2, "dirt")
m.DC(8, 0, -14, 12, 1.5, -11, "rust")
m.DC(9, 1.5, -13.4, 11, 2.3, -11.6, "rust")
m.DC(11.6, 0.8, -13.0, 14.2, 1.1, -12.2, "rust")
# wall buys / perks / spawns
m.WallBuy("mp5k", -1.9, 1.4, 11.5, 0)
m.WallBuy("mpl", -29.9, 1.4, -1.0, 0)
m.WallBuy("hs10", 9.5, 1.4, -25.9, 0)
m.WallBuy("enfield", 29.9, 1.4, -11.5, 0)
m.WallBuy("cz75", 1.9, 1.4, 13.5, 0)
m.Perk("revive", 4.8, 0, 10.8, 180)
m.Perk("swift", -28.8, 0, 8.6, 180)
m.Perk("rapid", 8.6, 0, -25.4, 180)
m.Perk("regen", 28.8, 0, 2.8, 180)
m.Perk("stamin", 0.0, 0, 25.5, 0)
m.Spawn(-30.5, -0.5, -1, False, 0)
m.Spawn(-30.5, 6.5, -1, False, 0)
m.Spawn(0, -25.4, 1, False, 0)
m.Spawn(6, -25.4, 1, False, 0)
m.Spawn(29.4, -9.0, 2, False, 0)
m.Spawn(29.4, 1.0, 2, False, 0)
m.Spawn(0, 26.5, -1, False, 0)
m.Spawn(-3.5, 26.5, -1, False, 0)
m.Spawn(3.5, 26.5, -1, False, 0)
m.Spawn(-25.0, 26.5, -1, False, 0)
m.Spawn(25.0, 26.5, -1, False, 0)
m.save("bunker")

# ===========================================================================
# MAP 3: GRAVEYARD HOLLOW
# ===========================================================================
m = Map(name="Graveyard Hollow", desc="The dead were never resting. The church is your only refuge.",
        difficulty=4, playerStart=[0, 0, 26], playerYaw=0, boxPos=[0, 0.1, -8.5],
        skyTop=[6, 8, 16], skyHorizon=[24, 28, 40], fogColor=[16, 20, 28], fogDensity=0.030,
        sunDir=[0.2, -0.85, -0.4], sunColor=[70, 85, 120], ambient=[55, 62, 82],
        fillDir=[-0.2, -0.5, 0.4], fillColor=[70, 82, 110])
m.B(-38, -1, -34, 38, 0, 34, "grass")
m.WallX(-38, 38, -33.8, 2.6, 0.6, "stone")
m.WallX(-38, 38, 33.8, 2.6, 0.6, "stone")
m.WallZ(-33.8, 33.8, -37.8, 2.6, 0.6, "stone")
m.WallZ(-33.8, 33.8, 37.8, 2.6, 0.6, "stone")
m.DC(-2.2, 0.012, 6.0, 2.2, 0.03, 33.5, "dirt")
# church
m.B(-7.4, -0.2, -10.4, 7.4, 0, 6.4, "tile")
m.WallX(-7.4, -2.2, -10.6, 6, 0.5, "stucco")
m.WallX(2.2, 7.4, -10.6, 6, 0.5, "stucco")
m.B(-2.2, 2.6, -10.85, 2.2, 6.0, -10.35, "stucco")
m.WallZ(-10.6, 6.6, -7.2, 6, 0.5, "stucco", -2.0, 1.3)
m.WallZ(-10.6, 6.6, 7.2, 6, 0.5, "stucco", -2.0, 1.3)
m.B(-7.4, 6.0, -10.4, 7.4, 6.3, 6.4, "roof")
m.DC(-7.6, 5.0, -10.8, 0, 8.2, -10.2, "roof")
m.DC(0, 5.0, -10.8, 7.6, 8.2, -10.2, "roof")
m.DC(-7.6, 5.0, 6.2, 0, 8.2, 6.8, "roof")
m.DC(0, 5.0, 6.2, 7.6, 8.2, 6.8, "roof")
# steeple
m.B(-3.2, -0.2, 6.6, 3.2, 0, 12.2, "tile")
m.WallX(-3.2, -1.1, 12.4, 9, 0.5, "stucco")
m.WallX(1.1, 3.2, 12.4, 9, 0.5, "stucco")
m.B(-1.1, 2.6, 12.15, 1.1, 9.0, 12.65, "stucco")
m.WallZ(6.6, 12.4, -3.0, 9, 0.5, "stucco", 7.5, 1.3)
m.WallZ(6.6, 12.4, 3.0, 9, 0.5, "stucco", 7.5, 1.3)
m.DC(-3.2, 9.0, 6.6, 3.2, 14.0, 12.2, "roof")
m.DC(-1.0, 14.0, 9.0, 1.0, 17.0, 11.0, "roof")
# pews
for r in range(4):
    z = 3.4 - r * 1.7
    m.DC(-6.2, 0, z, -1.4, 0.5, z + 0.9, "plank")
    m.DC(1.4, 0, z, 6.2, 0.5, z + 0.9, "plank")
m.B(-1.6, 0, -9.9, 1.6, 0.5, -8.7, "stone")
m.DC(-0.3, 0.5, -9.6, 0.3, 1.6, -9.0, "metal")
# crypt door 0
m.Door("Crypt Gate", 750, 20.0, 1.2, -14.0)
m.BDoor(0, 19.8, 0, -14.7, 20.2, 2.4, -13.3, "metal")
m.B(14, -0.2, -20, 26, 0, -8, "stone")
m.WallX(14, 19.8, -20.2, 3.6, 0.5, "stone")
m.WallX(20.2, 26, -20.2, 3.6, 0.5, "stone")
m.B(19.8, 2.4, -20.45, 20.2, 3.6, -19.95, "stone")
m.WallX(14, 26, -7.8, 3.6, 0.5, "stone")
m.WallZ(-20.2, -14.7, 14.2, 3.6, 0.5, "stone")
m.WallZ(-13.3, -7.8, 14.2, 3.6, 0.5, "stone")
m.WallZ(-20.2, -7.8, 25.8, 3.6, 0.5, "stone")
m.B(14, 3.6, -20, 26, 3.9, -8, "stone")
m.DC(15.2, 0, -18.6, 16.4, 1.1, -17.4, "grave")
m.DC(23.6, 0, -18.6, 24.8, 1.1, -17.4, "grave")
# mausoleum door 1
m.Door("Mausoleum Gate", 1000, -26.0, 1.2, 24.4)
m.BDoor(1, -26.4, 0, 23.7, -26.0, 2.4, 25.1, "metal")
m.B(-30, -0.2, 12, -22, 0, 26, "stone")
m.WallX(-30, -22, 11.8, 3.6, 0.5, "stone")
m.WallX(-30, -26.4, 26.2, 3.6, 0.5, "stone")
m.WallX(-26.0, -22, 26.2, 3.6, 0.5, "stone")
m.B(-26.4, 2.4, 25.85, -26.0, 3.6, 26.35, "stone")
m.WallZ(11.8, 26.2, -30.2, 3.6, 0.5, "stone")
m.WallZ(11.8, 26.2, -22.2, 3.6, 0.5, "stone")
m.B(-30, 3.6, 12, -22, 3.9, 26, "stone")
m.DC(-29.2, 0, 13.2, -28.0, 1.2, 14.4, "grave")
m.DC(-24.0, 0, 13.2, -22.8, 1.2, 14.4, "grave")
# graves + rising spawns
graves = [
    (-14,20),(-18,18),(-22,22),(-26,16),(-30,20),(-12,24),(-16,28),(-20,26),(-10,16),
    (-14,10),(-18,6),(-22,2),(-26,-2),(-30,6),(-12,-4),(-16,-8),(-20,-14),(-24,-18),
    (14,20),(18,24),(22,20),(26,24),(30,20),(12,16),(16,12),(20,8),(24,4),
    (28,8),(12,6),(16,2),(20,-2),(24,-8),(28,-4),(14,-4),(18,-16),(22,-20),
    (26,-24),(30,-16),(10,-14),(6,-18),(2,-22),(-4,-18),(-8,-24),(8,-24),(-6,-28),
    (-12,-20),(-16,-26),(-20,-24),(-24,-26),(-28,-22),(4,-26),(8,-28),(-2,-30),(-6,-24),
]
for (gx, gz) in graves:
    if -8.5 < gx < 8.5 and -11.5 < gz < 12.8: continue
    if 13.5 < gx < 26.5 and -20.5 < gz < -7.5: continue
    if -30.5 < gx < -21.5 and 11.5 < gz < 26.5: continue
    m.DC(gx-0.45, 0, gz-0.15, gx+0.45, 1.05, gz+0.15, "grave")
    m.DC(gx-0.7, 0.012, gz-1.1, gx+0.7, 0.05, gz+1.1, "dirt")
    m.Spawn(gx, gz, -1, True, 0)
# dead trees
for (tx, tz) in [(-10,10),(10,-10),(-32,-8),(32,-12),(-8,30),(12,30),(-34,26),(34,26),(-34,-30),(34,-30)]:
    m.DC(tx-0.25, 0, tz-0.25, tx+0.25, 4.5, tz+0.25, "wood")
    m.DC(tx-1.6, 3.6, tz-0.1, tx+1.6, 3.9, tz+0.1, "wood")
    m.DC(tx-0.1, 3.9, tz-1.4, tx+0.1, 4.2, tz+1.4, "wood")
m.WallBuy("olympia", -7.0, 1.4, 22.0, 90)
m.WallBuy("stakeout", 14.5, 1.4, -19.5, 90)
m.WallBuy("m16", -29.5, 1.4, 20.5, 90)
m.WallBuy("cz75", 6.8, 1.4, 0.0, 270)
m.Perk("revive", 5.8, 0, 30.5, 180)
m.Perk("regen", -5.8, 0, 30.5, 180)
m.Perk("swift", 24.8, 0, -18.5, 180)
m.Perk("rapid", -28.8, 0, 22.5, 180)
m.Perk("stamin", 0.0, 0, 10.0, 0)
m.Spawn(0, -33.0, -1, False, 0)
m.Spawn(-36.0, -33.0, -1, False, 0)
m.Spawn(36.0, -33.0, -1, False, 0)
m.Spawn(24.0, -18.5, 0, False, 0)
m.Spawn(-26.0, 22.0, 1, False, 0)
m.Spawn(0, -11.0, -1, False, 0)
m.save("graveyard")

# ===========================================================================
# MAP 4: DIE ARISE
# ===========================================================================
m = Map(name="Die Arise", desc="The city is gone. Only the rooftops remain - and they go up.",
        difficulty=5, playerStart=[0, 0, 2], playerYaw=90, boxPos=[-26, 0.1, 10],
        skyTop=[18, 22, 40], skyHorizon=[90, 60, 70], fogColor=[40, 34, 52], fogDensity=0.008,
        sunDir=[-0.4, -0.75, 0.3], sunColor=[200, 150, 130], ambient=[98, 88, 108],
        fillDir=[0.4, -0.55, -0.3], fillColor=[110, 98, 130])
# tower A
m.DC(-8, -40, -8, 8, -0.6, 8, "concrete")
m.B(-8, -1, -8, 8, 0, 8, "concrete")
m.B(-8, 0, -8, 8, 0.9, -7.6, "concrete")
m.B(-8, 0, 7.6, 8, 0.9, 8, "concrete")
m.B(-8, 0, -8, -7.6, 0.9, 8, "concrete")
m.B(7.6, 0, -8, 8, 0.9, 8, "concrete")
m.B(-2, 0, 5.6, 2, 2.6, 7.6, "brick")
m.B(-2, 2.6, 5.6, 2, 2.9, 7.6, "concrete")
m.DC(-6.5, 0, -6.5, -4.5, 1.0, -4.5, "metal")
m.DC(4.5, 0, -6.8, 6.5, 1.2, -4.8, "metal")
# bridge A->B door 0
m.B(8, -0.25, -1.0, 24, 0, 1.0, "plank")
m.B(8, 0.45, -1.3, 24, 0.75, -1.0, "plank")
m.B(8, 0.45, 1.0, 24, 0.75, 1.3, "plank")
m.Door("Bridge Barricade", 750, 16.0, 1.2, 0)
m.BDoor(0, 15.6, 0, -1.3, 16.4, 2.2, 1.3, "plank")
# tower B
m.DC(24, -40, -7, 38, -0.6, 7, "concrete")
m.B(24, -1, -7, 38, 0, 7, "concrete")
m.B(24, 0, -7, 38, 0.9, -6.6, "concrete")
m.B(24, 0, 6.6, 38, 0.9, 7, "concrete")
m.B(24, 0, -7, 24.4, 0.9, 7, "concrete")
m.B(37.6, 0, -7, 38, 0.9, 7, "concrete")
m.Stairs(30.0, 3.6, 1, 0, 2.0, 2.4, 5, "metal", 0)
m.B(26, 2.4, -6.5, 38, 2.55, 3.2, "metal")
m.B(26, 2.55, -6.5, 38, 3.3, -6.1, "metal")
m.B(26, 2.55, 2.9, 38, 3.3, 3.2, "metal")
m.B(25.6, 2.55, -6.5, 26, 3.3, 3.2, "metal")
m.B(37.6, 2.55, -6.5, 38, 3.3, 3.2, "metal")
m.DC(36.0, 2.55, -5.5, 37.5, 3.5, -4.0, "metal")
# tower C
m.DC(-32, -40, 4, -20, -0.6, 16, "concrete")
m.B(-32, -1, 4, -20, 0, 16, "concrete")
m.B(-32, 0, 4, -20, 0.9, 4.4, "concrete")
m.B(-32, 0, 15.6, -20, 0.9, 16, "concrete")
m.B(-32, 0, 4, -31.6, 0.9, 16, "concrete")
m.B(-20.4, 0, 4, -20, 0.9, 16, "concrete")
m.DC(-30.5, 0, 5.5, -29.5, 3.2, 6.5, "rust")
m.DC(-31.2, 3.2, 4.8, -28.8, 5.6, 7.2, "rust")
m.DC(-27, 0, 13.5, -25, 5.0, 13.8, "metal")
m.DC(-29.5, 4.6, 11.0, -22.5, 6.6, 11.4, "rust")
# A<->C walkway
m.B(-13.5, -0.25, 2.0, -8, 0, 4.0, "plank")
m.B(-13.5, 0.45, 1.7, -8, 0.75, 2.0, "plank")
m.B(-13.5, 0.45, 4.0, -8, 0.75, 4.3, "plank")
m.B(-16, -0.25, 2.0, -13.5, 0, 7.0, "plank")
m.B(-16, 0.45, 1.7, -13.5, 0.75, 2.0, "plank")
m.B(-16, 0.45, 7.0, -13.5, 0.75, 7.3, "plank")
m.B(-20, -0.25, 5.0, -16, 0, 7.0, "plank")
m.B(-20, 0.45, 4.7, -16, 0.75, 5.0, "plank")
m.B(-20, 0.45, 7.0, -16, 0.75, 7.3, "plank")
m.Zip((-20.5, 3.5, 10.0), (-8.5, 1.2, 2.0))
# spire
m.DC(8, -40, -30, 16, 7.4, -22, "brick")
m.B(8, 7.4, -30, 16, 8.0, -22, "concrete")
m.B(8, 8.0, -30, 16, 8.9, -29.6, "concrete")
m.B(8, 8.0, -22.4, 16, 8.9, -22, "concrete")
m.B(8, 8.0, -30, 8.4, 8.9, -22, "concrete")
m.B(15.6, 8.0, -30, 16, 8.9, -22, "concrete")
m.DC(11.5, 8.9, -27.0, 12.5, 15.0, -26.0, "metal")
m.Pad(34.0, 0.0, -3.0, -10.0, 19.0, -10.0)
m.Zip((12.0, 9.2, -22.5), (4.0, 1.5, 6.5))
# pit
m.DC(-22, -48, -14, -8, -8.6, 0, "brick")
m.B(-22, -9, -14, -8, -8, 0, "concrete")
m.B(-22, -8, -14, -8, -7.1, -13.6, "concrete")
m.B(-22, -8, 0.4, -8, -7.1, 0, "concrete")
m.B(-22, -8, -14, -21.6, -7.1, 0, "concrete")
m.B(-8.4, -8, -14, -8, -7.1, 0, "concrete")
m.Stairs(-9.25, -12.0, 0, 1, 1.5, 8.0, 16, "rust", -8.0)
m.DC(-20.5, -8, -12.5, -19.0, -6.6, -11.0, "wood")
m.DC(-18.5, -8, -12.2, -17.0, -7.0, -10.7, "wood")
m.Pad(-18.0, -8.0, -12.0, 0, 16.0, 0)
m.Kill(-60, -60, 60, 60, -25.0)
m.WallBuy("olympia", -7.4, 1.4, -4.0, 90)
m.WallBuy("ak74u", 37.4, 1.4, 2.0, 270)
m.WallBuy("galil", -31.4, 1.4, 8.0, 90)
m.WallBuy("python", 12.0, 9.3, -29.4, 0)
m.WallBuy("mp5k", -21.4, -6.6, -2.0, 90)
m.Perk("revive", 6.0, 0, -6.0, 180)
m.Perk("swift", 36.0, 0, 5.0, 180)
m.Perk("regen", -30.0, 0, 14.0, 180)
m.Perk("rapid", 5.5, 0, 2.5, 180)
m.Perk("stamin", 32.0, 2.4, 0.0, 180)
m.Spawn(-13.5, 6.4, -1, False, 0)
m.Spawn(-26.0, 15.0, -1, False, 0)
m.Spawn(-30.0, 15.0, -1, False, 0)
m.Spawn(-15.0, -1.2, -1, False, -8)
m.Spawn(-11.0, -1.2, -1, False, -8)
m.Spawn(31.0, 6.0, 0, False, 0)
m.Spawn(25.0, 6.0, 0, False, 0)
m.Spawn(36.0, 6.0, 0, False, 0)
m.save("diearise")

# ===========================================================================
# MAP 5: DEAD CON 5
# ===========================================================================
import math as _math
m = Map(name="Dead Con 5", desc="The Oval Office first. The FBI's secrets are upstairs.",
        difficulty=4, playerStart=[0, 0, 6], playerYaw=180, boxPos=[7.5, 0.1, -1.5],
        skyTop=[14, 16, 26], skyHorizon=[70, 60, 72], fogColor=[26, 24, 34], fogDensity=0.010,
        sunDir=[0.3, -0.8, -0.35], sunColor=[170, 160, 180], ambient=[92, 88, 104],
        fillDir=[-0.3, -0.5, 0.4], fillColor=[90, 84, 110])
# F0 oval office
m.B(-10.5, -1, -8.5, 10.5, 0, 8.5, "marble")
for a in range(16):
    t0 = a / 16.0 * 2 * _math.pi
    t1 = (a + 1) / 16.0 * 2 * _math.pi
    x0, z0 = _math.cos(t0) * 10.0, _math.sin(t0) * 8.0
    x1, z1 = _math.cos(t1) * 10.0, _math.sin(t1) * 8.0
    if z0 < -7.0 and z1 < -7.0: continue
    m.B(min(x0, x1) - 0.35, 0, min(z0, z1) - 0.35, max(x0, x1) + 0.35, 4.6, max(z0, z1) + 0.35, "velvet")
m.B(-1.6, 0, -6.4, 1.6, 0.78, -5.2, "wood")
m.DC(-6.0, 0.012, -5.0, 6.0, 0.02, 5.0, "velvet")
m.DC(-4.6, 0, 1.6, -1.4, 0.85, 2.6, "velvet")
m.DC(1.4, 0, 1.6, 4.6, 0.85, 2.6, "velvet")
m.DC(-1.2, 0, 1.9, 1.2, 0.45, 2.3, "wood")
m.B(-10.1, 0, -1.6, -9.0, 2.6, 1.6, "marble")
m.Door("Elevator to Control Room", 1000, 0, 1.2, -7.8)
m.BDoor(0, -1.3, 0, -8.2, 1.3, 2.6, -7.4, "metal")
# stairwell 1 + F0 ceiling
m.Stairs(2.5, -7.0, 1, 0, 2.0, 6.0, 11, "concrete", 0)
m.B(-10.5, 4.6, -8.5, 2.0, 5.0, 8.5, "marble")
m.B(9.0, 4.6, -8.5, 10.5, 5.0, 8.5, "marble")
m.B(2.0, 4.6, -5.5, 9.0, 5.0, 8.5, "marble")
# F1 control room
m.B(1.8, 5.0, -12.4, 2.0, 6.0, 1.4, "concrete")
m.B(6.0, 5.0, -12.4, 22.4, 6.0, 1.4, "concrete")
m.WallX(1.8, 22.2, 1.2, 5, 0.4, "concrete")
m.WallZ(-12.2, 1.2, 2.2, 5, 0.4, "concrete", -9.4, 0.9)
m.WallZ(-7.6, 1.2, 2.2, 5, 0.4, "concrete")
m.WallX(2.2, 22.2, -12.2, 5, 0.4, "concrete")
m.WallZ(-12.2, 1.2, 22.2, 5, 0.4, "concrete")
m.WallZ(-12.2, -7.4, 6.2, 5, 0.4, "concrete", -9.4, 0.9)
for i in range(4):
    x = 9.0 + i * 3.4
    m.B(x, 6.0, -11.8, x + 1.2, 8.4, -10.8, "metal")
for i in range(3):
    x = 8.0 + i * 4.6
    m.B(x, 6.0, -6.2, x + 2.4, 6.75, -5.0, "metal")
    m.DC(x + 0.3, 6.75, -6.0, x + 2.1, 7.15, -5.2, "glass")
m.Door("Elevator to FBI Floor", 1250, 21.8, 7.2, -6.0)
m.BDoor(1, 21.9, 6.0, -6.6, 22.6, 8.6, -5.4, "metal")
# stairwell 2 + F1 ceiling
m.Stairs(8.5, -11.0, 1, 0, 2.0, 6.0, 11, "concrete", 6.0)
m.B(1.8, 10.4, -5.9, 22.4, 10.8, 1.4, "concrete")
m.B(1.8, 10.4, -12.4, 8.0, 10.8, -5.9, "concrete")
m.B(15.0, 10.4, -12.4, 22.4, 10.8, -5.9, "concrete")
# F2 FBI floor
m.B(1.8, 11.4, -9.8, 30.4, 12.0, 4.4, "tile")
m.B(1.8, 11.4, -12.4, 8.0, 12.0, -9.8, "tile")
m.B(15.0, 11.4, -12.4, 30.4, 12.0, -9.8, "tile")
m.WallX(1.8, 30.2, 4.2, 5, 0.4, "stucco")
m.WallZ(-12.2, 4.2, 2.2, 5, 0.4, "stucco")
m.WallX(2.2, 30.2, -12.2, 5, 0.4, "stucco")
m.WallZ(-12.2, 4.2, 30.2, 5, 0.4, "stucco")
m.B(1.8, 16.4, -12.4, 30.4, 16.8, 4.4, "concrete")
# power room (door price 0 = open by design)
m.Door("Power Room Door", 0, 24.0, 13.2, -6.0)
m.BDoor(2, 21.6, 12.0, -6.6, 22.4, 14.6, -5.4, "metal")
m.WallZ(-12.2, -6.6, 22.2, 5, 0.4, "stucco", -9.4, 0.9)
m.WallZ(-5.4, 4.2, 22.2, 5, 0.4, "stucco", -1.0, 0.9)
m.WallX(22.2, 30.2, -1.2, 5, 0.4, "stucco")
m.m["powerPos"] = [29.6, 12.6, -9.4]
m.B(27.6, 12.0, -11.6, 29.8, 13.3, -10.2, "rust")
# file room door 3
m.Door("File Room Door", 1000, 14.0, 13.2, -8.0)
m.BDoor(3, 13.6, 12.0, -8.4, 14.4, 14.6, -7.6, "plank")
m.WallZ(-12.2, -8.4, 12.2, 5, 0.4, "stucco", -10.2, 0.9)
m.WallZ(-7.6, -1.2, 12.2, 5, 0.4, "stucco")
m.WallX(12.2, 20.2, -1.2, 5, 0.4, "stucco")
for i in range(3):
    m.B(13.0 + i * 2.2, 12.0, -11.8, 14.0 + i * 2.2, 14.2, -11.0, "wood")
# interrogation room door 4
m.Door("Interrogation Room Door", 1000, 26.0, 13.2, 2.0)
m.BDoor(4, 25.6, 12.0, 1.6, 26.4, 14.6, 2.4, "plank")
m.WallZ(-1.2, 1.6, 24.2, 5, 0.4, "stucco")
m.WallX(24.2, 30.2, 4.0, 5, 0.4, "stucco")
m.B(26.8, 12.0, 1.2, 29.0, 12.75, 2.4, "metal")
# outside door 5 + balcony
m.Door("Outside Door", 1250, 16.0, 13.2, 4.0)
m.BDoor(5, 15.6, 12.0, 4.2, 16.4, 14.6, 4.4, "plank")
m.B(1.8, 10.8, 4.4, 30.4, 11.4, 8.4, "asphalt")
m.WallX(1.8, 30.2, 8.2, 1.2, 0.3, "metal")
m.Kill(-60, -60, 60, 60, 6.0)
# wall buys / perks / PaP
m.WallBuy("m1911", -9.4, 1.5, 5.4, 90)
m.WallBuy("cz75", 8.0, 7.5, -5.6, 0)
m.WallBuy("mp5k", 13.6, 13.5, -8.0, 0)
m.WallBuy("ak74u", 25.8, 13.5, 2.0, 0)
m.WallBuy("m16", 26.0, 13.5, -6.0, 0)
m.WallBuy("stakeout", 15.6, 13.5, 4.2, 270)
m.Perk("revive", 8.6, 6.0, -2.4, 180)
m.Perk("jugg", 19.6, 12.0, -10.6, 0)
m.Perk("swift", 27.6, 12.0, 0.4, 180)
m.Perk("regen", 28.6, 12.0, -4.4, 180)
m.Perk("rapid", 4.4, 12.0, -2.4, 180)
m.Perk("stamin", 17.0, 11.4, 6.4, 0)
m.m["papPos"] = [6.4, 12.0, -10.4]
m.Spawn(6.6, 6.6, -1, False, 0)
m.Spawn(-6.6, 6.6, -1, False, 0)
m.Spawn(0, -6.6, -1, False, 0)
m.Spawn(16.0, -2.0, 0, False, 6)
m.Spawn(12.0, -10.0, 0, False, 6)
m.Spawn(20.0, 2.0, 1, False, 12)
m.Spawn(16.0, 2.0, 1, False, 12)
m.Spawn(24.0, 2.0, 1, False, 12)
m.Spawn(15.0, -3.0, 1, False, 12)
m.Spawn(27.0, -9.0, 1, False, 12)
m.Spawn(13.0, -10.0, 3, False, 12)
m.Spawn(28.0, 2.5, 4, False, 12)
m.save("deadcon5")

print("all maps dumped")
