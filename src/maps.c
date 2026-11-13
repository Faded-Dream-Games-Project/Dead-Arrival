// ============================================================================
//  DEAD ARRIVAL - maps.c
//  Four maps built from AABB boxes:
//    1. The Grand Theater - auditorium + stage + balcony + backstage + alley
//    2. Bunker 44         - war zone: cross trenches + three concrete bunkers
//    3. Graveyard Hollow  - graves rise, church in the middle, crypt, mausoleum
//    4. Die Arise         - sky-high rooftops, jump pads + ziplines + pit
//  Navigation: uniform walk-grid (up to 2 surface levels per cell) + BFS.
//  Collision: axis-separated AABB vs box with step-up.
// ============================================================================
#include "game.h"

// ============================================================================
// AABB utilities
// ============================================================================
AABB AABB_Make(Vector3 min, Vector3 max) { AABB a; a.min = min; a.max = max; return a; }

bool AABB_Overlap(AABB a, AABB b) {
    return a.min.x < b.max.x && a.max.x > b.min.x &&
           a.min.y < b.max.y && a.max.y > b.min.y &&
           a.min.z < b.max.z && a.max.z > b.min.z;
}

// Slab-method ray/AABB intersect. Returns hit distance t in [0,maxDist], else -1.
float AABB_RayHit(AABB b, Vector3 o, Vector3 d, float maxDist) {
    float tmin = 0.0f, tmax = maxDist;
    if (fabsf(d.x) < 1e-8f) { if (o.x < b.min.x || o.x > b.max.x) return -1; }
    else {
        float t1 = (b.min.x-o.x)/d.x, t2 = (b.max.x-o.x)/d.x;
        if (t1 > t2) { float t = t1; t1 = t2; t2 = t; }
        tmin = t1 > tmin ? t1 : tmin; tmax = t2 < tmax ? t2 : tmax;
        if (tmin > tmax) return -1;
    }
    if (fabsf(d.y) < 1e-8f) { if (o.y < b.min.y || o.y > b.max.y) return -1; }
    else {
        float t1 = (b.min.y-o.y)/d.y, t2 = (b.max.y-o.y)/d.y;
        if (t1 > t2) { float t = t1; t1 = t2; t2 = t; }
        tmin = t1 > tmin ? t1 : tmin; tmax = t2 < tmax ? t2 : tmax;
        if (tmin > tmax) return -1;
    }
    if (fabsf(d.z) < 1e-8f) { if (o.z < b.min.z || o.z > b.max.z) return -1; }
    else {
        float t1 = (b.min.z-o.z)/d.z, t2 = (b.max.z-o.z)/d.z;
        if (t1 > t2) { float t = t1; t1 = t2; t2 = t; }
        tmin = t1 > tmin ? t1 : tmin; tmax = t2 < tmax ? t2 : tmax;
        if (tmin > tmax) return -1;
    }
    return tmin;
}

// ============================================================================
// rng (xorshift32)
// ============================================================================
static uint32_t sRngState = 0x9E3779B9u;
void SeedRng(uint32_t seed) { sRngState = seed ? seed : 0x9E3779B9u; }
uint32_t RngNext(void) {
    uint32_t x = sRngState;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    sRngState = x;
    return x;
}
float RngRange(float a, float b) { return a + (b-a) * (RngNext() & 0xFFFFFF) / 16777216.0f; }
int RngInt(int n) { return n > 0 ? (int)(RngNext() % (uint32_t)n) : 0; }

// ============================================================================
// Map building helpers
// ============================================================================
static int BCount(MapDef *m) { return m->boxCount; }
static void B(MapDef *m, float x0,float y0,float z0, float x1,float y1,float z1, int tex) {
    if (m->boxCount >= MAX_BOXES) return;
    if (x1 < x0) { float t=x0; x0=x1; x1=t; }
    if (y1 < y0) { float t=y0; y0=y1; y1=t; }
    if (z1 < z0) { float t=z0; z0=z1; z1=t; }
    m->boxes[m->boxCount].aabb = AABB_Make(Vec3(x0,y0,z0), Vec3(x1,y1,z1));
    m->boxes[m->boxCount].tex = tex;
    m->boxes[m->boxCount].noCollide = false;
    m->boxes[m->boxCount].doorId = 0;
    m->boxCount++;
}
static void DC(MapDef *m, float x0,float y0,float z0, float x1,float y1,float z1, int tex) {
    if (m->boxCount >= MAX_BOXES) return;
    if (x1 < x0) { float t=x0; x0=x1; x1=t; }
    if (z1 < z0) { float t=z0; z0=z1; z1=t; }
    m->boxes[m->boxCount].aabb = AABB_Make(Vec3(x0,y0,z0), Vec3(x1,y1,z1));
    m->boxes[m->boxCount].tex = tex;
    m->boxes[m->boxCount].noCollide = true;
    m->boxes[m->boxCount].doorId = 0;
    m->boxCount++;
}
static void BDoor(MapDef *m, int doorIdx, float x0,float y0,float z0, float x1,float y1,float z1, int tex) {
    if (m->boxCount >= MAX_BOXES) return;
    m->boxes[m->boxCount].aabb = AABB_Make(Vec3(x0,y0,z0), Vec3(x1,y1,z1));
    m->boxes[m->boxCount].tex = tex;
    m->boxes[m->boxCount].noCollide = false;
    m->boxes[m->boxCount].doorId = doorIdx+1;
    m->boxCount++;
}
static void DoorAdd(MapDef *m, const char *name, int price, float cx, float cy, float cz) {
    if (m->doorCount >= MAX_DOORS) return;
    m->doors[m->doorCount].name = name;
    m->doors[m->doorCount].price = price;
    m->doors[m->doorCount].center = Vec3(cx,cy,cz);
    m->doorCount++;
}
static void SpawnAdd(MapDef *m, float x, float z, int doorId, bool grave, float y) {
    if (m->spawnCount >= MAX_SPAWNS) return;
    m->spawns[m->spawnCount].pos = Vec3(x,y,z);
    m->spawns[m->spawnCount].doorId = doorId;
    m->spawns[m->spawnCount].grave = grave;
    m->spawnCount++;
}
static void WallBuyAdd(MapDef *m, int widx, float x, float y, float z, float yaw) {
    if (m->wallbuyCount >= MAX_WALLBUYS) return;
    m->wallbuys[m->wallbuyCount].widx = widx;
    m->wallbuys[m->wallbuyCount].pos = Vec3(x,y,z);
    m->wallbuys[m->wallbuyCount].yaw = yaw;
    m->wallbuyCount++;
}
static void PerkAdd(MapDef *m, PerkType t, float x, float z, float yaw) {
    if (m->perkCount >= MAX_PERKSTATIONS) return;
    m->perks[m->perkCount].type = t;
    m->perks[m->perkCount].pos = Vec3(x,0,z);
    m->perks[m->perkCount].yaw = yaw;
    m->perkCount++;
}
static void PadAdd(MapDef *m, float x, float z, float ix, float iy, float iz) {
    if (m->padCount >= MAX_JUMPPADS) return;
    m->pads[m->padCount].pos = Vec3(x,0,z);
    m->pads[m->padCount].radius = 1.3f;
    m->pads[m->padCount].impulse = Vec3(ix,iy,iz);
    m->padCount++;
}
static void ZipAdd(MapDef *m, Vector3 a, Vector3 b) {
    if (m->ziplineCount >= MAX_ZIPLINES) return;
    m->ziplines[m->ziplineCount].a = a;
    m->ziplines[m->ziplineCount].b = b;
    m->ziplineCount++;
}
static void KillAdd(MapDef *m, float x0,float z0, float x1,float z1, float yTop) {
    if (m->killzoneCount >= MAX_KILLZONES) return;
    m->killzones[m->killzoneCount] = AABB_Make(Vec3(x0,-999,z0), Vec3(x1,yTop,z1));
    m->killzoneCount++;
}

// Wall along X at fixed z, optional centered gap (doorway) with lintel at 2.4.
static void WallX(MapDef *m, float x0, float x1, float z, float h, float t, int tex, float gapC, float gapH) {
    if (gapH > 0) {
        if (gapC-gapH > x0) B(m, x0, 0, z-t/2, gapC-gapH, h, z+t/2, tex);
        if (x1 > gapC+gapH) B(m, gapC+gapH, 0, z-t/2, x1, h, z+t/2, tex);
        if (h > 2.4f) B(m, gapC-gapH, 2.4f, z-t/2, gapC+gapH, h, z+t/2, tex);
    } else B(m, x0, 0, z-t/2, x1, h, z+t/2, tex);
}
static void WallZ(MapDef *m, float z0, float z1, float x, float h, float t, int tex, float gapC, float gapH) {
    if (gapH > 0) {
        if (gapC-gapH > z0) B(m, x-t/2, 0, z0, x+t/2, h, gapC-gapH, tex);
        if (z1 > gapC+gapH) B(m, x-t/2, 0, gapC+gapH, x+t/2, h, z1, tex);
        if (h > 2.4f) B(m, x-t/2, 2.4f, gapC-gapH, x+t/2, h, gapC+gapH, tex);
    } else B(m, x-t/2, 0, z0, x+t/2, h, z1, tex);
}

// Stairs from (x,z) heading along a unit axis (dx,dz) in {(±1,0),(0,±1)},
// width w, rising to `total` over `steps` steps of 0.55 run each.
static void Stairs(MapDef *m, float x, float z, float dx, float dz,
                   float w, float total, int steps, int tex, float yBase) {
    float rise = total/steps, run = 0.55f;
    for (int i = 0; i < steps; i++) {
        float top = yBase + (i+1)*rise;
        if (fabsf(dx) > 0.5f) {
            float x0, x1;
            if (dx > 0) { x0 = x + i*run; x1 = x0 + run; }
            else        { x1 = x - i*run; x0 = x1 - run; }
            B(m, x0, yBase - 0.6f, z - w/2, x1, top, z + w/2, tex);
        } else {
            float z0, z1;
            if (dz > 0) { z0 = z + i*run; z1 = z0 + run; }
            else        { z1 = z - i*run; z0 = z1 - run; }
            B(m, x - w/2, yBase - 0.6f, z0, x + w/2, top, z1, tex);
        }
    }
}


// ============================================================================
// MAP 1: THE GRAND THEATER
// ============================================================================
static void BuildTheater(MapDef *m) {
    m->name = "The Grand Theater";
    m->desc = "The curtain rose on the end of the world. Hold the stage.";
    m->difficulty = 2;
    m->skyTop = (Color){  8, 10, 22 };
    m->skyHorizon = (Color){ 40, 30, 42 };
    m->fogColor = (Color){ 22, 18, 30 };
    m->fogDensity = 0.012f;
    m->sunDir = Vec3Norm(Vec3(-0.35f, -0.8f, 0.45f));
    m->sunColor = (Color){ 120, 105, 130 };
    m->ambient = (Color){ 70, 62, 80 };
    m->playerStart = Vec3(0, 0, 17.5f);
    m->playerYaw = 180.0f;
    m->boxPos = Vec3(0, 1.2f, -14.5f);

    const int WALLH = 7;

    // ---- Lobby (x -9..9, z 11..24) ----
    B(m, -9.5f, -1, 10.5f, 9.5f, 0, 24.5f, TEX_MARBLE);
    WallX(m, -9.5f, 9.5f, 24.2f, WALLH, 0.4f, TEX_VELVET, 0, 0);
    WallX(m, -9.5f, -3.5f, 10.8f, WALLH, 0.4f, TEX_VELVET, 0, 0);
    WallX(m,  3.5f,  9.5f, 10.8f, WALLH, 0.4f, TEX_VELVET, 0, 0);
    WallZ(m, 10.8f, 24.2f, -9.3f, WALLH, 0.4f, TEX_VELVET, 17.5f, 1.6f);
    WallZ(m, 10.8f, 24.2f,  9.3f, WALLH, 0.4f, TEX_VELVET, 0, 0);
    DC(m, -2.0f, 0, 23.0f, -0.6f, 1.1f, 24.0f, TEX_WOOD);
    DC(m,  0.6f, 0, 23.0f,  2.0f, 1.1f, 24.0f, TEX_WOOD);
    DC(m, -1.4f, 0.012f, 11.2f, 1.4f, 0.03f, 23.8f, TEX_VELVET);

    // door 0: lobby <-> auditorium
    DoorAdd(m, "Auditorium Doors", 750, 0, 1.2f, 10.8f);
    BDoor(m, 0, -3.3f, 0, 10.6f, -0.1f, 2.4f, 11.0f, TEX_PLANK);
    BDoor(m, 0,  0.1f, 0, 10.6f,  3.3f, 2.4f, 11.0f, TEX_PLANK);

    // stairs lobby -> balcony: climb north along west wall (through wall gap)
    Stairs(m, -7.6f, 17.5f, 0, -1, 1.8f, 4.2f, 8, TEX_MARBLE, 0);
    // balcony floor north of the stair top (stair top: x -8.5..-6.7, z 13.1, y 4.2)
    B(m, -9.5f, 4.2f, 10.6f, -4.6f, 4.35f, 13.1f, TEX_MARBLE);
    // balcony railings (south edge, leaving the stair opening clear)
    B(m, -9.5f, 4.35f, 13.1f, -8.5f, 5.3f, 13.4f, TEX_VELVET);
    B(m, -6.7f, 4.35f, 13.1f, -4.6f, 5.3f, 13.4f, TEX_VELVET);
    B(m, -4.9f, 4.35f, 10.6f, -4.6f, 5.3f, 13.1f, TEX_VELVET);
    B(m, -9.8f, 4.35f, 10.6f, -9.5f, 5.3f, 13.4f, TEX_VELVET);

    // ---- Auditorium (x -14..14, z -12..10.6) ----
    B(m, -14.2f, -1, -12.2f, 14.2f, 0, 10.6f, TEX_MARBLE);
    WallX(m, -14.2f, 14.2f, -12.4f, WALLH+3, 0.4f, TEX_VELVET, 0, 0);
    WallZ(m, -12.4f, 10.6f, -14.0f, WALLH, 0.4f, TEX_VELVET, 0, 0);
    WallZ(m, -12.4f, -6.0f, 14.0f, WALLH, 0.4f, TEX_VELVET, 0, 0);
    WallZ(m, -2.0f, 10.6f, 14.0f, WALLH, 0.4f, TEX_VELVET, 0, 0);
    // seats
    for (int row = 0; row < 5; row++) {
        float z = 3.0f - row*1.6f;
        for (int s = 0; s < 8; s++) {
            float x = -12.0f + s*3.1f;
            DC(m, x, 0, z, x+1.6f, 0.55f + row*0.12f, z+1.1f, TEX_VELVET);
        }
    }
    DC(m, -1.5f, 6.4f, -1.5f, 1.5f, 7.0f, 1.5f, TEX_METAL);
    // west balcony over auditorium floor (from lobby balcony continue)
    B(m, -14.0f, 4.2f, -2.0f, -9.6f, 4.35f, 10.6f, TEX_MARBLE);
    B(m, -9.9f, 4.35f, -2.0f, -9.6f, 5.3f, 10.6f, TEX_VELVET);
    B(m, -14.0f, 4.35f, -2.3f, -9.6f, 5.3f, -2.0f, TEX_VELVET);
    B(m, -12.9f, 4.35f, 10.3f, -9.6f, 5.3f, 10.6f, TEX_VELVET);

    // ---- Stage (z -20..-12.4) raised 1.2 ----
    B(m, -14.2f, 0, -20.0f, 14.2f, 1.2f, -12.4f, TEX_PLANK);
    Stairs(m, -13.0f, -12.4f, 0, -1, 2.0f, 1.2f, 3, TEX_PLANK, 0);
    Stairs(m,  13.0f, -12.4f, 0, -1, 2.0f, 1.2f, 3, TEX_PLANK, 0);
    WallZ(m, -20.0f, -12.4f, -14.0f, WALLH+3, 0.4f, TEX_VELVET, 0, 0);
    WallZ(m, -20.0f, -12.4f,  14.0f, WALLH+3, 0.4f, TEX_VELVET, 0, 0);
    DC(m, -14.2f, 4.6f, -12.8f, 14.2f, 5.4f, -12.4f, TEX_VELVET);
    DC(m, -13.8f, 1.2f, -19.6f, -10.0f, 6.6f, -19.2f, TEX_CURTAIN);
    DC(m,  10.0f, 1.2f, -19.6f, 13.8f, 6.6f, -19.2f, TEX_CURTAIN);
    DC(m, -10.0f, 6.2f, -19.6f, 10.0f, 6.9f, -19.2f, TEX_CURTAIN);
    B(m, -14.2f, 1.2f, -13.6f, 14.2f, 1.5f, -13.4f, TEX_WOOD);

    // door 1: backstage
    DoorAdd(m, "Backstage Door", 1000, 0, 1.2f, -20.2f);
    BDoor(m, 1, -1.3f, 0.0f, -20.4f, 1.3f, 2.4f, -19.9f, TEX_PLANK);
    B(m, -8.2f, -1, -26.2f, 8.2f, 0, -20.0f, TEX_PLANK);
    WallX(m, -8.2f, -1.5f, -20.2f, 5, 0.4f, TEX_PLANK, 0, 0);
    WallX(m,  1.5f,  8.2f, -20.2f, 5, 0.4f, TEX_PLANK, 0, 0);
    B(m, -1.5f, 2.4f, -20.4f, 1.5f, 5.0f, -19.9f, TEX_PLANK);
    WallX(m, -8.2f, 8.2f, -26.4f, 5, 0.4f, TEX_PLANK, 0, 0);
    WallZ(m, -26.4f, -20.2f, -8.0f, 5, 0.4f, TEX_PLANK, 0, 0);
    DC(m, -7.0f, 0, -25.5f, -5.5f, 2.2f, -24.0f, TEX_WOOD);
    DC(m,  5.5f, 0, -25.5f,  7.0f, 2.2f, -24.0f, TEX_WOOD);

    // door 2: alley (auditorium east)
    DoorAdd(m, "Alley Door", 1000, 14.0f, 1.2f, -4.0f);
    BDoor(m, 2, 13.8f, 0, -5.9f, 14.2f, 2.4f, -4.4f, TEX_PLANK);
    BDoor(m, 2, 13.8f, 0, -3.6f, 14.2f, 2.4f, -2.1f, TEX_PLANK);
    B(m, 14.2f, -1, -12.0f, 20.4f, 0, 10.6f, TEX_ASPHALT);
    WallX(m, 14.2f, 20.4f, -12.2f, 6, 0.4f, TEX_BRICK, 0, 0);
    WallX(m, 14.2f, 20.4f, 10.8f, 6, 0.4f, TEX_BRICK, 0, 0);
    WallZ(m, -12.2f, 10.8f, 20.2f, 6, 0.4f, TEX_BRICK, 0, 0);
    WallZ(m, -12.2f, -6.0f, 14.2f, 6, 0.4f, TEX_BRICK, 0, 0);
    WallZ(m, -2.0f, 10.8f, 14.2f, 6, 0.4f, TEX_BRICK, 0, 0);

    DC(m, -14.2f, 9.9f, -26.2f, 20.4f, 10.2f, 24.5f, TEX_VELVET);

    // wall buys / perks
    WallBuyAdd(m, W_OLYMPIA, -8.9f, 1.5f, 23.6f, 90);
    WallBuyAdd(m, W_MP5K, -13.7f, 1.5f, 2.0f, 90);
    WallBuyAdd(m, W_STAKEOUT, 7.0f, 1.5f, -25.9f, 0);
    WallBuyAdd(m, W_AK74U, 19.9f, 1.5f, 4.0f, 270);
    WallBuyAdd(m, W_M16, -13.7f, 5.7f, 8.4f, 90);
    PerkAdd(m, PERK_REVIVE, 8.2f, 22.8f, 270);
    PerkAdd(m, PERK_RAPID, -12.9f, -3.0f, 90);
    PerkAdd(m, PERK_SWIFT, -6.8f, -25.4f, 0);
    PerkAdd(m, PERK_REGEN, 19.4f, -10.8f, 0);

    // spawns
    SpawnAdd(m, 0, 12.6f, -1, false, 0);
    SpawnAdd(m, 8.6f, 12.6f, -1, false, 0);
    SpawnAdd(m, -13.2f, -6.0f, -1, false, 0);
    SpawnAdd(m, -13.2f, 4.0f, -1, false, 0);
    SpawnAdd(m, 6.0f, -21.4f, 1, false, 0);
    SpawnAdd(m, -6.0f, -21.4f, 1, false, 0);
    SpawnAdd(m, 19.2f, -8.0f, 2, false, 0);
    SpawnAdd(m, 19.2f, 8.0f, 2, false, 0);
    SpawnAdd(m, 19.2f, 2.0f, 2, false, 0);
}

// ============================================================================
// MAP 2: BUNKER 44
// ============================================================================
static void BuildBunker(MapDef *m) {
    m->name = "Bunker 44";
    m->desc = "Holding position in the ruins of the final offensive.";
    m->difficulty = 3;
    m->skyTop = (Color){ 60, 55, 48 };
    m->skyHorizon = (Color){ 110, 88, 60 };
    m->fogColor = (Color){ 96, 82, 62 };
    m->fogDensity = 0.020f;
    m->sunDir = Vec3Norm(Vec3(0.5f, -0.7f, -0.3f));
    m->sunColor = (Color){ 190, 165, 120 };
    m->ambient = (Color){ 95, 85, 70 };
    m->playerStart = Vec3(0, 0, 7);
    m->playerYaw = 180.0f;
    m->boxPos = Vec3(-27, 0, 4);

    B(m, -32, -1, -28, 32, 0, 28, TEX_DIRT);
    // perimeter sandbag walls
    for (int i = 0; i < 32; i++) {
        float x = -32 + i*2;
        B(m, x, 0, -28, x+2, 1.7f, -27.6f, TEX_SANDBAG);
        B(m, x, 0, 27.6f, x+2, 1.7f, 28, TEX_SANDBAG);
    }
    for (int i = 0; i < 28; i++) {
        float z = -28 + i*2;
        B(m, -32, 0, z, -31.6f, 1.7f, z+2, TEX_SANDBAG);
        B(m, 31.6f, 0, z, 32, 1.7f, z+2, TEX_SANDBAG);
    }

    // ---- Command bunker (spawn, center-south) ----
    B(m, -6, -0.2f, 2, 6, 0, 12, TEX_CONCRETE);
    WallX(m, -6, -1.5f, 2.2f, 3.2f, 0.5f, TEX_CONCRETE, 0, 0);
    WallX(m, 1.5f, 6, 2.2f, 2.6f, 0.5f, TEX_CONCRETE, 0, 0);
    B(m, -1.5f, 2.4f, 1.95f, 1.5f, 3.2f, 2.45f, TEX_CONCRETE);
    B(m, 1.5f, 2.4f, 1.95f, 6.0f, 3.2f, 2.45f, TEX_CONCRETE);
    WallX(m, -6, 6, 11.8f, 3.2f, 0.5f, TEX_CONCRETE, 0, 0);
    WallZ(m, 2.2f, 11.8f, -5.8f, 3.2f, 0.5f, TEX_CONCRETE, 0, 0);
    WallZ(m, 2.2f, 11.8f, 5.8f, 3.2f, 0.05f, TEX_CONCRETE, 0, 0);
    B(m, -6, 3.2f, 2, 6, 3.5f, 12, TEX_CONCRETE);
    DC(m, -5.2f, 0.75f, 10.4f, -3.4f, 1.05f, 11.4f, TEX_WOOD);
    DC(m, -4.9f, 1.05f, 10.6f, -4.0f, 1.35f, 11.2f, TEX_METAL);

    // ---- north trench: z from 2 down to -20, walls at x ±1.9 ----
    for (int i = 0; i < 11; i++) {
        float z = 2 - i*2;
        B(m, -2.2f, 0, z-2, -1.8f, 1.6f, z, TEX_SANDBAG);
        B(m, 1.8f, 0, z-2, 2.2f, 1.6f, z, TEX_SANDBAG);
    }
    // east trench: x from 2 to 26, z centered -4
    for (int i = 0; i < 12; i++) {
        float x = 2 + i*2;
        B(m, x, 0, -6.2f, x+2, 1.6f, -5.8f, TEX_SANDBAG);
        B(m, x, 0, -2.2f, x+2, 1.6f, -1.8f, TEX_SANDBAG);
    }
    // west trench: x from -2 to -26, z centered 5.5
    for (int i = 0; i < 12; i++) {
        float x = -2 - i*2;
        B(m, x-2, 0, 3.5f, x, 1.6f, 3.9f, TEX_SANDBAG);
        B(m, x-2, 0, 7.1f, x, 1.6f, 7.5f, TEX_SANDBAG);
    }
    // south trench: z from 12 to 24, x ±1.9
    for (int i = 0; i < 6; i++) {
        float z = 12 + i*2;
        B(m, -2.2f, 0, z, -1.8f, 1.6f, z+2, TEX_SANDBAG);
        B(m, 1.8f, 0, z, 2.2f, 1.6f, z+2, TEX_SANDBAG);
    }

    // ---- Bunker A (west, door 0) ----
    DoorAdd(m, "West Bunker Door", 750, -27.9f, 1.2f, 5.5f);
    BDoor(m, 0, -28.3f, 0, 4.1f, -27.9f, 2.4f, 6.9f, TEX_METAL);
    B(m, -30, -0.2f, -2, -24, 0, 10, TEX_CONCRETE);
    WallX(m, -30, -24, -2.2f, 3.4f, 0.5f, TEX_CONCRETE, 0, 0);
    WallX(m, -30, -28.3f, 9.8f, 3.4f, 0.5f, TEX_CONCRETE, 0, 0);
    WallX(m, -27.9f, -24, 9.8f, 3.4f, 0.5f, TEX_CONCRETE, 0, 0);
    B(m, -28.3f, 2.4f, 9.55f, -27.9f, 3.4f, 10.05f, TEX_CONCRETE);
    WallZ(m, -2.2f, 9.8f, -30.2f, 3.4f, 0.5f, TEX_CONCRETE, 0, 0);
    WallZ(m, -2.2f, 4.1f, -24.2f, 3.4f, 0.5f, TEX_CONCRETE, 0, 0);
    WallZ(m, 6.9f, 9.8f, -24.2f, 3.4f, 0.5f, TEX_CONCRETE, 0, 0);
    B(m, -30, 3.4f, -2, -24, 3.7f, 10, TEX_CONCRETE);
    DC(m, -29.6f, 0, 8.6f, -29.0f, 1.6f, 9.4f, TEX_SANDBAG);

    // ---- Bunker B (north, door 1) ----
    DoorAdd(m, "North Bunker Door", 1000, 0, 1.2f, -20.3f);
    BDoor(m, 1, -1.4f, 0, -20.6f, 1.4f, 2.4f, -19.9f, TEX_METAL);
    B(m, -10, -0.2f, -26, 10, 0, -20, TEX_CONCRETE);
    WallX(m, -10, -1.5f, -26.2f, 3.4f, 0.5f, TEX_CONCRETE, 0, 0);
    WallX(m, 1.5f, 10, -26.2f, 3.4f, 0.5f, TEX_CONCRETE, 0, 0);
    B(m, -1.5f, 2.4f, -26.45f, 1.5f, 3.4f, -25.95f, TEX_CONCRETE);
    WallZ(m, -26.2f, -20.2f, -10.2f, 3.4f, 0.5f, TEX_CONCRETE, 0, 0);
    WallZ(m, -26.2f, -20.2f, 10.2f, 3.4f, 0.5f, TEX_CONCRETE, 0, 0);
    B(m, -10, 3.4f, -26, 10, 3.7f, -20, TEX_CONCRETE);
    // inner armory wall
    WallX(m, -10, -7.8f, -22.4f, 3.4f, 0.4f, TEX_CONCRETE, 0, 0);
    WallX(m, -6.2f, -4, -22.4f, 3.4f, 0.4f, TEX_CONCRETE, 0, 0);
    B(m, -7.8f, 2.4f, -22.6f, -6.2f, 3.4f, -22.2f, TEX_CONCRETE);
    DC(m, -9.6f, 0.8f, -25.4f, -8.4f, 1.1f, -24.6f, TEX_WOOD);
    DC(m, -8.0f, 0, -25.6f, -6.8f, 1.3f, -24.4f, TEX_WOOD);

    // ---- Ammo depot (east, door 2) ----
    DoorAdd(m, "Ammo Depot Door", 1250, 25.9f, 1.2f, -4.0f);
    BDoor(m, 2, 25.8f, 0, -6.1f, 26.2f, 2.4f, -4.4f, TEX_METAL);
    BDoor(m, 2, 25.8f, 0, -3.6f, 26.2f, 2.4f, -1.9f, TEX_METAL);
    B(m, 20, -0.2f, -12, 30, 0, 4, TEX_CONCRETE);
    WallX(m, 20, 30, -12.2f, 3.4f, 0.5f, TEX_CONCRETE, 0, 0);
    WallX(m, 20, 30, 4.2f, 3.4f, 0.5f, TEX_CONCRETE, 0, 0);
    WallZ(m, -12.2f, 4.2f, 30.2f, 3.4f, 0.5f, TEX_CONCRETE, 0, 0);
    WallZ(m, -12.2f, -6.1f, 19.8f, 3.4f, 0.5f, TEX_CONCRETE, 0, 0);
    WallZ(m, -1.9f, 4.2f, 19.8f, 2.6f, 0.05f, TEX_CONCRETE, 0, 0);
    B(m, 20, 3.4f, -12, 30, 3.7f, 4, TEX_CONCRETE);
    for (int i = 0; i < 4; i++) DC(m, 21+i*2.2f, 0, -10.5f, 22.4f+i*2.2f, 1.4f, -9.1f, TEX_WOOD);

    // craters + tank decor
    static const float craters[][2] = {{-14,-18},{10,-16},{14,8},{-18,14},{8,18},{-22,-10},{16,-22},{24,14},{-8,-24},{20,22}};
    for (unsigned i = 0; i < sizeof(craters)/sizeof(craters[0]); i++) {
        float cx = craters[i][0], cz = craters[i][1];
        DC(m, cx-1.2f, 0, cz-1.2f, cx+1.2f, 0.18f, cz+1.2f, TEX_DIRT);
    }
    DC(m, 8, 0, -14, 12, 1.5f, -11, TEX_RUST);
    DC(m, 9, 1.5f, -13.4f, 11, 2.3f, -11.6f, TEX_RUST);
    DC(m, 11.6f, 0.8f, -13.0f, 14.2f, 1.1f, -12.2f, TEX_RUST);

    WallBuyAdd(m, W_MP5K, -1.9f, 1.4f, 11.5f, 0);
    WallBuyAdd(m, W_MPL, -29.9f, 1.4f, -1.0f, 0);
    WallBuyAdd(m, W_HS10, 9.5f, 1.4f, -25.9f, 0);
    WallBuyAdd(m, W_ENFIELD, 29.9f, 1.4f, -11.5f, 0);
    WallBuyAdd(m, W_CZ75, 1.9f, 1.4f, 13.5f, 0);
    PerkAdd(m, PERK_REVIVE, 4.8f, 10.8f, 180);
    PerkAdd(m, PERK_SWIFT, -28.8f, 8.6f, 180);
    PerkAdd(m, PERK_RAPID, 8.6f, -25.4f, 180);
    PerkAdd(m, PERK_REGEN, 28.8f, 2.8f, 180);

    SpawnAdd(m, -30.5f, -0.5f, -1, false, 0);
    SpawnAdd(m, -30.5f, 6.5f, -1, false, 0);
    SpawnAdd(m, 0, -26.8f, 1, false, 0);
    SpawnAdd(m, 6, -26.8f, 1, false, 0);
    SpawnAdd(m, 30.5f, -9.0f, 2, false, 0);
    SpawnAdd(m, 30.5f, 1.0f, 2, false, 0);
    SpawnAdd(m, 0, 26.5f, -1, false, 0);
    SpawnAdd(m, -3.5f, 26.5f, -1, false, 0);
    SpawnAdd(m, 3.5f, 26.5f, -1, false, 0);
    SpawnAdd(m, -25.0f, 26.5f, -1, false, 0);
    SpawnAdd(m, 25.0f, 26.5f, -1, false, 0);
}

// ============================================================================
// MAP 3: GRAVEYARD HOLLOW
// ============================================================================
static void BuildGraveyard(MapDef *m) {
    m->name = "Graveyard Hollow";
    m->desc = "The dead were never resting. The church is your only refuge.";
    m->difficulty = 4;
    m->skyTop = (Color){  6,  8, 16 };
    m->skyHorizon = (Color){ 24, 28, 40 };
    m->fogColor = (Color){ 16, 20, 28 };
    m->fogDensity = 0.030f;
    m->sunDir = Vec3Norm(Vec3(0.2f, -0.85f, -0.4f));
    m->sunColor = (Color){ 70, 85, 120 };
    m->ambient = (Color){ 40, 46, 62 };
    m->playerStart = Vec3(0, 0, 26);
    m->playerYaw = 0.0f;
    m->boxPos = Vec3(0, 0.1f, -8.5f);   // inside the church, before the altar

    B(m, -38, -1, -34, 38, 0, 34, TEX_GRASS);
    WallX(m, -38, 38, -33.8f, 2.6f, 0.6f, TEX_STONE, 0, 0);
    WallX(m, -38, 38, 33.8f, 2.6f, 0.6f, TEX_STONE, 0, 0);
    WallZ(m, -33.8f, 33.8f, -37.8f, 2.6f, 0.6f, TEX_STONE, 0, 0);
    WallZ(m, -33.8f, 33.8f, 37.8f, 2.6f, 0.6f, TEX_STONE, 0, 0);
    DC(m, -2.2f, 0.012f, 6.0f, 2.2f, 0.03f, 33.5f, TEX_DIRT);

    // ---- Church (center) ----
    B(m, -7.4f, -0.2f, -10.4f, 7.4f, 0, 6.4f, TEX_TILE);
    WallX(m, -7.4f, -2.2f, -10.6f, 6, 0.5f, TEX_STUCCO, 0, 0);
    WallX(m, 2.2f, 7.4f, -10.6f, 6, 0.5f, TEX_STUCCO, 0, 0);
    B(m, -2.2f, 2.6f, -10.85f, 2.2f, 6.0f, -10.35f, TEX_STUCCO);
    WallZ(m, -10.6f, 6.6f, -7.2f, 6, 0.5f, TEX_STUCCO, -2.0f, 1.3f);
    WallZ(m, -10.6f, 6.6f, 7.2f, 6, 0.5f, TEX_STUCCO, -2.0f, 1.3f);
    B(m, -7.4f, 6.0f, -10.4f, 7.4f, 6.3f, 6.4f, TEX_ROOF);
    DC(m, -7.6f, 5.0f, -10.8f, 0, 8.2f, -10.2f, TEX_ROOF);
    DC(m, 0, 5.0f, -10.8f, 7.6f, 8.2f, -10.2f, TEX_ROOF);
    DC(m, -7.6f, 5.0f, 6.2f, 0, 8.2f, 6.8f, TEX_ROOF);
    DC(m, 0, 5.0f, 6.2f, 7.6f, 8.2f, 6.8f, TEX_ROOF);
    // steeple tower (south, x -3..3, z 6.6..12.2)
    B(m, -3.2f, -0.2f, 6.6f, 3.2f, 0, 12.2f, TEX_TILE);
    WallX(m, -3.2f, -1.1f, 12.4f, 9, 0.5f, TEX_STUCCO, 0, 0);
    WallX(m, 1.1f, 3.2f, 12.4f, 9, 0.5f, TEX_STUCCO, 0, 0);
    B(m, -1.1f, 2.6f, 12.15f, 1.1f, 9.0f, 12.65f, TEX_STUCCO);
    WallZ(m, 6.6f, 12.4f, -3.0f, 9, 0.5f, TEX_STUCCO, 7.5f, 1.3f);
    WallZ(m, 6.6f, 12.4f, 3.0f, 9, 0.5f, TEX_STUCCO, 7.5f, 1.3f);
    DC(m, -3.2f, 9.0f, 6.6f, 3.2f, 14.0f, 12.2f, TEX_ROOF);
    DC(m, -1.0f, 14.0f, 9.0f, 1.0f, 17.0f, 11.0f, TEX_ROOF);
    // pews
    for (int r = 0; r < 4; r++) {
        float z = 3.4f - r*1.7f;
        DC(m, -6.2f, 0, z, -1.4f, 0.5f, z+0.9f, TEX_PLANK);
        DC(m, 1.4f, 0, z, 6.2f, 0.5f, z+0.9f, TEX_PLANK);
    }
    B(m, -1.6f, 0, -9.9f, 1.6f, 0.5f, -8.7f, TEX_STONE);
    DC(m, -0.3f, 0.5f, -9.6f, 0.3f, 1.6f, -9.0f, TEX_METAL);

    // ---- Crypt (door 0, NE) ----
    DoorAdd(m, "Crypt Gate", 750, 20.0f, 1.2f, -14.0f);
    BDoor(m, 0, 19.8f, 0, -14.7f, 20.2f, 2.4f, -13.3f, TEX_METAL);
    B(m, 14, -0.2f, -20, 26, 0, -8, TEX_STONE);
    WallX(m, 14, 19.8f, -20.2f, 3.6f, 0.5f, TEX_STONE, 0, 0);
    WallX(m, 20.2f, 26, -20.2f, 3.6f, 0.5f, TEX_STONE, 0, 0);
    B(m, 19.8f, 2.4f, -20.45f, 20.2f, 3.6f, -19.95f, TEX_STONE);
    WallX(m, 14, 26, -7.8f, 3.6f, 0.5f, TEX_STONE, 0, 0);
    WallZ(m, -20.2f, -14.7f, 14.2f, 3.6f, 0.5f, TEX_STONE, 0, 0);
    WallZ(m, -13.3f, -7.8f, 14.2f, 3.6f, 0.5f, TEX_STONE, 0, 0);
    WallZ(m, -20.2f, -7.8f, 25.8f, 3.6f, 0.5f, TEX_STONE, 0, 0);
    B(m, 14, 3.6f, -20, 26, 3.9f, -8, TEX_STONE);
    DC(m, 15.2f, 0, -18.6f, 16.4f, 1.1f, -17.4f, TEX_GRAVE);
    DC(m, 23.6f, 0, -18.6f, 24.8f, 1.1f, -17.4f, TEX_GRAVE);

    // ---- Mausoleum (door 1, SW) ----
    DoorAdd(m, "Mausoleum Gate", 1000, -26.0f, 1.2f, 24.4f);
    BDoor(m, 1, -26.4f, 0, 23.7f, -26.0f, 2.4f, 25.1f, TEX_METAL);
    B(m, -30, -0.2f, 12, -22, 0, 26, TEX_STONE);
    WallX(m, -30, -22, 11.8f, 3.6f, 0.5f, TEX_STONE, 0, 0);
    WallX(m, -30, -26.4f, 26.2f, 3.6f, 0.5f, TEX_STONE, 0, 0);
    WallX(m, -26.0f, -22, 26.2f, 3.6f, 0.5f, TEX_STONE, 0, 0);
    B(m, -26.4f, 2.4f, 25.85f, -26.0f, 3.6f, 26.35f, TEX_STONE);
    WallZ(m, 11.8f, 26.2f, -30.2f, 3.6f, 0.5f, TEX_STONE, 0, 0);
    WallZ(m, 11.8f, 26.2f, -22.2f, 3.6f, 0.5f, TEX_STONE, 0, 0);
    B(m, -30, 3.6f, 12, -22, 3.9f, 26, TEX_STONE);
    DC(m, -29.2f, 0, 13.2f, -28.0f, 1.2f, 14.4f, TEX_GRAVE);
    DC(m, -24.0f, 0, 13.2f, -22.8f, 1.2f, 14.4f, TEX_GRAVE);

    // ---- graves + rising spawns ----
    static const float graveSpots[][2] = {
        {-14,20},{-18,18},{-22,22},{-26,16},{-30,20},{-12,24},{-16,28},{-20,26},{-10,16},
        {-14,10},{-18,6},{-22,2},{-26,-2},{-30,6},{-12,-4},{-16,-8},{-20,-14},{-24,-18},
        { 14,20},{ 18,24},{ 22,20},{ 26,24},{ 30,20},{ 12,16},{ 16,12},{ 20,8},{ 24,4},
        { 28,8},{ 12,6},{ 16,2},{ 20,-2},{ 24,-8},{ 28,-4},{ 14,-4},{ 18,-16},{ 22,-20},
        { 26,-24},{ 30,-16},{ 10,-14},{ 6,-18},{ 2,-22},{-4,-18},{-8,-24},{ 8,-24},{-6,-28},
        {-12,-20},{-16,-26},{-20,-24},{-24,-26},{-28,-22},{ 4,-26},{ 8,-28},{-2,-30},{-6,-24},
    };
    for (unsigned i = 0; i < sizeof(graveSpots)/sizeof(graveSpots[0]); i++) {
        float gx = graveSpots[i][0], gz = graveSpots[i][1];
        if (gx > -8.5f && gx < 8.5f && gz > -11.5f && gz < 12.8f) continue;
        if (gx > 13.5f && gx < 26.5f && gz > -20.5f && gz < -7.5f) continue;   // crypt
        if (gx > -30.5f && gx < -21.5f && gz > 11.5f && gz < 26.5f) continue;  // mausoleum
        DC(m, gx-0.45f, 0, gz-0.15f, gx+0.45f, 1.05f, gz+0.15f, TEX_GRAVE);
        DC(m, gx-0.7f, 0.012f, gz-1.1f, gx+0.7f, 0.05f, gz+1.1f, TEX_DIRT);
        SpawnAdd(m, gx, gz, -1, true, 0);
    }
    // dead trees
    static const float trees[][2] = {{-10,10},{10,-10},{-32,-8},{32,-12},{-8,30},{12,30},{-34,26},{34,26},{-34,-30},{34,-30}};
    for (unsigned i = 0; i < sizeof(trees)/sizeof(trees[0]); i++) {
        float tx = trees[i][0], tz = trees[i][1];
        DC(m, tx-0.25f, 0, tz-0.25f, tx+0.25f, 4.5f, tz+0.25f, TEX_WOOD);
        DC(m, tx-1.6f, 3.6f, tz-0.1f, tx+1.6f, 3.9f, tz+0.1f, TEX_WOOD);
        DC(m, tx-0.1f, 3.9f, tz-1.4f, tx+0.1f, 4.2f, tz+1.4f, TEX_WOOD);
    }

    WallBuyAdd(m, W_OLYMPIA, -7.0f, 1.4f, 22.0f, 90);
    WallBuyAdd(m, W_STAKEOUT, 14.5f, 1.4f, -19.5f, 90);
    WallBuyAdd(m, W_M16, -29.5f, 1.4f, 20.5f, 90);
    WallBuyAdd(m, W_CZ75, 6.8f, 1.4f, 0.0f, 270);
    PerkAdd(m, PERK_REVIVE, 5.8f, 30.5f, 180);
    PerkAdd(m, PERK_REGEN, -5.8f, 30.5f, 180);
    PerkAdd(m, PERK_SWIFT, 24.8f, -18.5f, 180);
    PerkAdd(m, PERK_RAPID, -28.8f, 22.5f, 180);

    SpawnAdd(m, 0, -33.0f, -1, false, 0);
    SpawnAdd(m, -36.0f, -33.0f, -1, false, 0);
    SpawnAdd(m, 36.0f, -33.0f, -1, false, 0);
    SpawnAdd(m, 25.0f, -21.0f, 0, false, 0);
    SpawnAdd(m, -26.0f, 27.0f, 1, false, 0);
    SpawnAdd(m, 0, -11.0f, -1, false, 0);
}

// ============================================================================
// MAP 4: DIE ARISE
// ============================================================================
static void BuildDieArise(MapDef *m) {
    m->name = "Die Arise";
    m->desc = "The city is gone. Only the rooftops remain - and they go up.";
    m->difficulty = 5;
    m->skyTop = (Color){ 18, 22, 40 };
    m->skyHorizon = (Color){ 90, 60, 70 };
    m->fogColor = (Color){ 40, 34, 52 };
    m->fogDensity = 0.008f;
    m->sunDir = Vec3Norm(Vec3(-0.4f, -0.75f, 0.3f));
    m->sunColor = (Color){ 200, 150, 130 };
    m->ambient = (Color){ 80, 70, 90 };
    m->playerStart = Vec3(0, 0, 2);
    m->playerYaw = 90.0f;
    m->boxPos = Vec3(-26, 0.1f, 10);

    // ---- Tower A (spawn): x -8..8, z -8..8, roof y=0 ----
    DC(m, -8, -40, -8, 8, -0.6f, 8, TEX_CONCRETE);
    B(m, -8, -1, -8, 8, 0, 8, TEX_CONCRETE);
    B(m, -8, 0, -8, 8, 0.9f, -7.6f, TEX_CONCRETE);
    B(m, -8, 0, 7.6f, 8, 0.9f, 8, TEX_CONCRETE);
    B(m, -8, 0, -8, -7.6f, 0.9f, 8, TEX_CONCRETE);
    B(m, 7.6f, 0, -8, 8, 0.9f, 8, TEX_CONCRETE);
    B(m, -2, 0, 5.6f, 2, 2.6f, 7.6f, TEX_BRICK);
    B(m, -2, 2.6f, 5.6f, 2, 2.9f, 7.6f, TEX_CONCRETE);
    DC(m, -6.5f, 0, -6.5f, -4.5f, 1.0f, -4.5f, TEX_METAL);
    DC(m, 4.5f, 0, -6.8f, 6.5f, 1.2f, -4.8f, TEX_METAL);

    // ---- plank bridge A -> B (east), door 0 ----
    B(m, 8, -0.25f, -1.0f, 24, 0, 1.0f, TEX_PLANK);
    B(m, 8, 0.45f, -1.3f, 24, 0.75f, -1.0f, TEX_PLANK);
    B(m, 8, 0.45f, 1.0f, 24, 0.75f, 1.3f, TEX_PLANK);
    DoorAdd(m, "Bridge Barricade", 750, 16.0f, 1.2f, 0);
    BDoor(m, 0, 15.6f, 0, -1.3f, 16.4f, 2.2f, 1.3f, TEX_PLANK);

    // ---- Tower B: x 24..38, z -7..7 ----
    DC(m, 24, -40, -7, 38, -0.6f, 7, TEX_CONCRETE);
    B(m, 24, -1, -7, 38, 0, 7, TEX_CONCRETE);
    B(m, 24, 0, -7, 38, 0.9f, -6.6f, TEX_CONCRETE);
    B(m, 24, 0, 6.6f, 38, 0.9f, 7, TEX_CONCRETE);
    B(m, 24, 0, -7, 24.4f, 0.9f, 7, TEX_CONCRETE);
    B(m, 37.6f, 0, -7, 38, 0.9f, 7, TEX_CONCRETE);
    // raised deck on B (y 2.4): x 26..38, z -6.5..4 with stairs from roof
    Stairs(m, 30.0f, 3.6f, 1, 0, 2.0f, 2.4f, 5, TEX_METAL, 0);
    B(m, 26, 2.4f, -6.5f, 38, 2.55f, 3.2f, TEX_METAL);
    B(m, 26, 2.55f, -6.5f, 38, 3.3f, -6.1f, TEX_METAL);
    B(m, 26, 2.55f, 2.9f, 38, 3.3f, 3.2f, TEX_METAL);
    B(m, 25.6f, 2.55f, -6.5f, 26, 3.3f, 3.2f, TEX_METAL);
    B(m, 37.6f, 2.55f, -6.5f, 38, 3.3f, 3.2f, TEX_METAL);
    DC(m, 36.0f, 2.55f, -5.5f, 37.5f, 3.5f, -4.0f, TEX_METAL);

    // ---- Tower C (west): x -32..-20, z 4..16 ----
    DC(m, -32, -40, 4, -20, -0.6f, 16, TEX_CONCRETE);
    B(m, -32, -1, 4, -20, 0, 16, TEX_CONCRETE);
    B(m, -32, 0, 4, -20, 0.9f, 4.4f, TEX_CONCRETE);
    B(m, -32, 0, 15.6f, -20, 0.9f, 16, TEX_CONCRETE);
    B(m, -32, 0, 4, -31.6f, 0.9f, 16, TEX_CONCRETE);
    B(m, -20.4f, 0, 4, -20, 0.9f, 16, TEX_CONCRETE);
    DC(m, -30.5f, 0, 5.5f, -29.5f, 3.2f, 6.5f, TEX_RUST);
    DC(m, -31.2f, 3.2f, 4.8f, -28.8f, 5.6f, 7.2f, TEX_RUST);
    DC(m, -27, 0, 13.5f, -25, 5.0f, 13.8f, TEX_METAL);
    DC(m, -29.5f, 4.6f, 11.0f, -22.5f, 6.6f, 11.4f, TEX_RUST);

    // ---- A <-> C walkway: south plank + corner platform ----
    // plank from A south edge to corner platform at (-13.5, 5)
    B(m, -13.5f, -0.25f, 2.0f, -8, 0, 4.0f, TEX_PLANK);
    B(m, -13.5f, 0.45f, 1.7f, -8, 0.75f, 2.0f, TEX_PLANK);
    B(m, -13.5f, 0.45f, 4.0f, -8, 0.75f, 4.3f, TEX_PLANK);
    // corner platform
    B(m, -16, -0.25f, 2.0f, -13.5f, 0, 7.0f, TEX_PLANK);
    B(m, -16, 0.45f, 1.7f, -13.5f, 0.75f, 2.0f, TEX_PLANK);
    B(m, -16, 0.45f, 7.0f, -13.5f, 0.75f, 7.3f, TEX_PLANK);
    // plank north from corner to C east edge
    B(m, -20, -0.25f, 5.0f, -16, 0, 7.0f, TEX_PLANK);
    B(m, -20, 0.45f, 4.7f, -16, 0.75f, 5.0f, TEX_PLANK);
    B(m, -20, 0.45f, 7.0f, -16, 0.75f, 7.3f, TEX_PLANK);
    // zipline C -> A (fast return)
    ZipAdd(m, Vec3(-20.5f, 3.5f, 10.0f), Vec3(-8.5f, 1.2f, 2.0f));

    // ---- Spire tower (north): x 8..16, z -30..-22, top y=8 ----
    DC(m, 8, -40, -30, 16, 7.4f, -22, TEX_BRICK);
    B(m, 8, 7.4f, -30, 16, 8.0f, -22, TEX_CONCRETE);
    B(m, 8, 8.0f, -30, 16, 8.9f, -29.6f, TEX_CONCRETE);
    B(m, 8, 8.0f, -22.4f, 16, 8.9f, -22, TEX_CONCRETE);
    B(m, 8, 8.0f, -30, 8.4f, 8.9f, -22, TEX_CONCRETE);
    B(m, 15.6f, 8.0f, -30, 16, 8.9f, -22, TEX_CONCRETE);
    DC(m, 11.5f, 8.9f, -27.0f, 12.5f, 15.0f, -26.0f, TEX_METAL);
    // jump pad on tower B roof launches to the spire roof (player-only route)
    PadAdd(m, 34.0f, -3.0f, -9.0f, 16.5f, -13.0f);
    // zipline from spire back to A
    ZipAdd(m, Vec3(12.0f, 9.2f, -22.5f), Vec3(4.0f, 1.5f, 6.5f));

    // ---- Pit tier (y=-8) under the A-C walkway ----
    DC(m, -22, -48, -14, -8, -8.6f, 0, TEX_BRICK);
    B(m, -22, -9, -14, -8, -8, 0, TEX_CONCRETE);
    B(m, -22, -8, -14, -8, -7.1f, -13.6f, TEX_CONCRETE);
    B(m, -22, -8, 0.4f, -8, -7.1f, 0, TEX_CONCRETE);
    B(m, -22, -8, -14, -21.6f, -7.1f, 0, TEX_CONCRETE);
    B(m, -8.4f, -8, -14, -8, -7.1f, 0, TEX_CONCRETE);
    // stairwell out of the pit: climbs along the east wall (x -10..-8.5),
    // from the pit floor (y -8) south to z -3.2, exiting beside tower A's roof
    Stairs(m, -9.25f, -12.0f, 0, 1, 1.5f, 8.0f, 16, TEX_RUST, -8.0f);
    DC(m, -20.5f, -8, -12.5f, -19.0f, -6.6f, -11.0f, TEX_WOOD);
    DC(m, -18.5f, -8, -12.2f, -17.0f, -7.0f, -10.7f, TEX_WOOD);
    PadAdd(m, -18.0f, -12.0f, 0, 16.0f, 0);
    KillAdd(m, -60, -60, 60, 60, -25.0f);

    WallBuyAdd(m, W_OLYMPIA, -7.4f, 1.4f, -4.0f, 90);
    WallBuyAdd(m, W_AK74U, 37.4f, 1.4f, 2.0f, 270);
    WallBuyAdd(m, W_GALIL, -31.4f, 1.4f, 8.0f, 90);
    WallBuyAdd(m, W_PYTHON, 12.0f, 9.3f, -29.4f, 0);
    WallBuyAdd(m, W_MP5K, -21.4f, -6.6f, -2.0f, 90);
    PerkAdd(m, PERK_REVIVE, 6.0f, -6.0f, 180);
    PerkAdd(m, PERK_SWIFT, 36.0f, 5.0f, 180);
    PerkAdd(m, PERK_REGEN, -30.0f, 14.0f, 180);
    PerkAdd(m, PERK_RAPID, 5.5f, 2.5f, 180);

    SpawnAdd(m, -13.5f, 6.4f, -1, false, 0);   // corner platform (always)
    SpawnAdd(m, -26.0f, 15.0f, -1, false, 0);   // tower C (always)
    SpawnAdd(m, -30.0f, 15.0f, -1, false, 0);
    SpawnAdd(m, -15.0f, -1.2f, -1, false, -8);  // pit floor (always)
    SpawnAdd(m, -11.0f, -1.2f, -1, false, -8);
    SpawnAdd(m, 31.0f, 6.0f, 0, false, 0);      // tower B (behind bridge door)
    SpawnAdd(m, 25.0f, 6.0f, 0, false, 0);
    SpawnAdd(m, 36.0f, 6.0f, 0, false, 0);
}

// ============================================================================
// Map loading
// ============================================================================
static MapDef sMaps[MAP_COUNT];

void MapLoad(MapId id) {
    MapDef *m = &sMaps[id];
    memset(m, 0, sizeof(MapDef));
    switch (id) {
        case MAP_THEATER:   BuildTheater(m);   break;
        case MAP_BUNKER:    BuildBunker(m);    break;
        case MAP_GRAVEYARD: BuildGraveyard(m); break;
        case MAP_DIE_ARISE: BuildDieArise(m);  break;
        default: break;
    }
    g.map = *m;
    Gfx_MB_Reset();
    for (int i = 0; i < g.map.boxCount; i++) {
        if (g.map.boxes[i].doorId != 0) continue;
        Gfx_MB_Box(g.map.boxes[i].tex, g.map.boxes[i].aabb, 2.0f);
    }
    Gfx_MB_Build();
    NavRebuild();
}

void MapUnload(void) {
    Gfx_MB_Reset();
    Gfx_MB_Build();
}

// ============================================================================
// Collision
// ============================================================================
static bool SolidAt(MapDef *m, int idx) {
    if (m->boxes[idx].noCollide) return false;
    if (m->boxes[idx].doorId > 0 && m->doorOpen[m->boxes[idx].doorId-1]) return false;
    return true;
}

bool CollideMove(Vector3 *pos, Vector3 *vel, float radius, float height, float *groundYOut) {
    MapDef *m = &g.map;
    float groundY = -1e9;
    bool hitGround = false;
    Vector3 p = *pos;

    // Y axis
    p.y += vel->y;
    {
        AABB body = AABB_Make(Vec3(p.x-radius, p.y, p.z-radius), Vec3(p.x+radius, p.y+height, p.z+radius));
        for (int i = 0; i < m->boxCount; i++) {
            if (!SolidAt(m, i)) continue;
            AABB bb = m->boxes[i].aabb;
            if (!AABB_Overlap(body, bb)) continue;
            if (vel->y <= 0 && p.y < bb.max.y && p.y > bb.max.y - 1.2f) {
                p.y = bb.max.y; vel->y = 0; hitGround = true;
                if (bb.max.y > groundY) groundY = bb.max.y;
            } else if (vel->y > 0 && p.y+height > bb.min.y && p.y+height < bb.min.y + 0.6f) {
                p.y = bb.min.y - height; vel->y = 0;
            }
            body = AABB_Make(Vec3(p.x-radius, p.y, p.z-radius), Vec3(p.x+radius, p.y+height, p.z+radius));
        }
    }

    #define BLOCKED_AT(nx, nz, atY) ({ \
        AABB _b = AABB_Make(Vec3((nx)-radius, (atY)+0.05f, (nz)-radius), Vec3((nx)+radius, (atY)+height, (nz)+radius)); \
        bool _blk = false; \
        for (int i = 0; i < m->boxCount && !_blk; i++) { \
            if (!SolidAt(m, i)) continue; \
            if (AABB_Overlap(_b, m->boxes[i].aabb)) _blk = true; \
        } \
        _blk; \
    })

    float atY = p.y;
    bool stepped = false;
    float dx = vel->x, dz = vel->z;
    int steps = (int)ceilf(fmaxf(fabsf(dx), fabsf(dz)) / 0.2f);
    if (steps < 1) steps = 1;
    for (int s = 0; s < steps; s++) {
        float sx = dx/steps, sz = dz/steps;
        // X
        if (sx != 0) {
            if (!BLOCKED_AT(p.x+sx, p.z, atY)) p.x += sx;
            else if (!BLOCKED_AT(p.x+sx, p.z, atY+STEP_HEIGHT)) { p.x += sx; atY += STEP_HEIGHT; stepped = true; }
        }
        // Z
        if (sz != 0) {
            if (!BLOCKED_AT(p.x, p.z+sz, atY)) p.z += sz;
            else if (!BLOCKED_AT(p.x, p.z+sz, atY+STEP_HEIGHT)) { p.z += sz; atY += STEP_HEIGHT; stepped = true; }
        }
    }
    #undef BLOCKED_AT
    if (stepped) { p.y = atY; if (vel->y < 0) vel->y = 0; }

    // ground check
    {
        AABB probe = AABB_Make(Vec3(p.x-radius, p.y-0.08f, p.z-radius), Vec3(p.x+radius, p.y, p.z+radius));
        for (int i = 0; i < m->boxCount; i++) {
            if (!SolidAt(m, i)) continue;
            if (AABB_Overlap(probe, m->boxes[i].aabb)) {
                if (m->boxes[i].aabb.max.y > groundY) groundY = m->boxes[i].aabb.max.y;
                hitGround = true;
            }
        }
    }

    *pos = p;
    if (groundYOut) *groundYOut = hitGround ? groundY : -1e9;
    return hitGround;
}

float GroundHeightAt(float x, float z, float fromY) {
    MapDef *m = &g.map;
    float best = -1e9;
    for (int i = 0; i < m->boxCount; i++) {
        if (!SolidAt(m, i)) continue;
        AABB bb = m->boxes[i].aabb;
        if (x >= bb.min.x && x <= bb.max.x && z >= bb.min.z && z <= bb.max.z) {
            if (bb.max.y <= fromY && bb.max.y > best) best = bb.max.y;
        }
    }
    return best;
}

bool MapRayHitWalls(Vector3 origin, Vector3 dir, float maxDist, float *tOut) {
    MapDef *m = &g.map;
    float best = maxDist;
    bool hit = false;
    for (int i = 0; i < m->boxCount; i++) {
        if (m->boxes[i].noCollide) continue;
        if (m->boxes[i].doorId > 0 && m->doorOpen[m->boxes[i].doorId-1]) continue;
        float t = AABB_RayHit(m->boxes[i].aabb, origin, dir, best);
        if (t >= 0 && t < best) { best = t; hit = true; }
    }
    if (tOut) *tOut = best;
    return hit;
}

bool InKillzone(Vector3 pos) {
    for (int i = 0; i < g.map.killzoneCount; i++)
        if (AABB_Overlap(AABB_Make(Vec3(pos.x-0.05f,pos.y-0.05f,pos.z-0.05f), Vec3(pos.x+0.05f,pos.y+0.05f,pos.z+0.05f)), g.map.killzones[i]))
            return true;
    return false;
}

// ============================================================================
// Doors
// ============================================================================
bool DoorTryOpen(int doorIdx) {
    if (doorIdx < 0 || doorIdx >= g.map.doorCount) return false;
    if (g.map.doorOpen[doorIdx]) return false;
    if (g.player.points < g.map.doors[doorIdx].price) return false;
    g.player.points -= g.map.doors[doorIdx].price;
    g.map.doorOpen[doorIdx] = true;
    g.player.doorsOpened++;
    NavRebuild();
    return true;
}

// ============================================================================
// Navigation grid
// ============================================================================
#define NAV_MAX 112
#define NAV_LEVELS 2
typedef struct NavCell { float y[NAV_LEVELS]; int8_t count; } NavCell;
static NavCell sNav[NAV_MAX][NAV_MAX];
static float sNavMinX, sNavMinZ;

static void NavSample(void) {
    float mnX = 1e9, mxX = -1e9, mnZ = 1e9, mxZ = -1e9;
    for (int i = 0; i < g.map.boxCount; i++) {
        AABB b = g.map.boxes[i].aabb;
        mnX = fminf(mnX, b.min.x); mxX = fmaxf(mxX, b.max.x);
        mnZ = fminf(mnZ, b.min.z); mxZ = fmaxf(mxZ, b.max.z);
    }
    sNavMinX = mnX; sNavMinZ = mnZ;
    memset(sNav, 0, sizeof(sNav));
    for (int cz = 0; cz < NAV_MAX; cz++) {
        for (int cx = 0; cx < NAV_MAX; cx++) {
            float x = sNavMinX + cx + 0.5f;
            float z = sNavMinZ + cz + 0.5f;
            if (x > mxX+1 || z > mxZ+1) continue;
            NavCell *c = &sNav[cz][cx];
            float top = GroundHeightAt(x, z, 14.0f);
            if (top < -50) continue;
            bool headroom = true;
            MapDef *m = &g.map;
            for (int i = 0; i < m->boxCount && headroom; i++) {
                if (!SolidAt(m, i)) continue;
                AABB bb = m->boxes[i].aabb;
                if (x >= bb.min.x && x <= bb.max.x && z >= bb.min.z && z <= bb.max.z &&
                    bb.min.y > top + 0.05f && bb.min.y < top + 1.9f) headroom = false;
            }
            if (!headroom) { c->count = 0; continue; }
            c->y[0] = top; c->count = 1;
            float lower = GroundHeightAt(x, z, top - 1.6f);
            if (lower > -50 && top - lower > 2.2f) { c->y[1] = lower; c->count = 2; }
        }
    }
}

void NavRebuild(void) { NavSample(); }

typedef struct BFSNode { int16_t cx, cz; int8_t lvl; int16_t prev; } BFSNode;
static BFSNode sQueue[NAV_MAX*NAV_MAX*NAV_LEVELS];
static bool sVisited[NAV_MAX][NAV_MAX][NAV_LEVELS];

bool NavFind(Vector3 from, Vector3 to, Vector3 *path, int *pathLen, int maxLen) {
    memset(sVisited, 0, sizeof(sVisited));
    int cx0 = (int)floorf(from.x - sNavMinX), cz0 = (int)floorf(from.z - sNavMinZ);
    int cx1 = (int)floorf(to.x - sNavMinX), cz1 = (int)floorf(to.z - sNavMinZ);
    int lvl0 = -1;
    if (cx0 >= 0 && cx0 < NAV_MAX && cz0 >= 0 && cz0 < NAV_MAX) {
        for (int l = 0; l < sNav[cz0][cx0].count; l++)
            if (fabsf(sNav[cz0][cx0].y[l] - from.y) < 1.5f) lvl0 = l;
        if (lvl0 < 0 && sNav[cz0][cx0].count > 0) lvl0 = 0;
    }
    if (lvl0 < 0) return false;
    int lvl1 = -1;
    if (cx1 >= 0 && cx1 < NAV_MAX && cz1 >= 0 && cz1 < NAV_MAX) {
        for (int l = 0; l < sNav[cz1][cx1].count; l++)
            if (fabsf(sNav[cz1][cx1].y[l] - to.y) < 1.5f) lvl1 = l;
        if (lvl1 < 0 && sNav[cz1][cx1].count > 0) lvl1 = 0;
    }
    if (lvl1 < 0) return false;

    int head = 0, tail = 0, found = -1;
    sQueue[tail++] = (BFSNode){ (int16_t)cx0, (int16_t)cz0, (int8_t)lvl0, -1 };
    sVisited[cz0][cx0][lvl0] = true;
    while (head < tail && found < 0) {
        BFSNode n = sQueue[head];
        if (n.cx == cx1 && n.cz == cz1 && n.lvl == lvl1) { found = head; break; }
        static const int DIRS[8][2] = {{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
        for (int d = 0; d < 8; d++) {
            int nx = n.cx + DIRS[d][0], nz = n.cz + DIRS[d][1];
            if (nx < 0 || nx >= NAV_MAX || nz < 0 || nz >= NAV_MAX) continue;
            float curY = sNav[n.cz][n.cx].y[n.lvl];
            if (sNav[nz][nx].count == 0) continue;
            for (int l = 0; l < sNav[nz][nx].count; l++) {
                float dy = sNav[nz][nx].y[l] - curY;
                if (dy > 0.65f || dy < -3.5f) continue;
                if (sVisited[nz][nx][l]) continue;
                if (d >= 4) {
                    bool okA = false, okB = false;
                    if (sNav[n.cz][nx].count > 0)
                        for (int la = 0; la < sNav[n.cz][nx].count; la++) {
                            float dya = sNav[n.cz][nx].y[la] - curY;
                            if (dya <= 0.65f && dya >= -3.5f) okA = true;
                        }
                    if (sNav[nz][n.cx].count > 0)
                        for (int lb = 0; lb < sNav[nz][n.cx].count; lb++) {
                            float dyb = sNav[nz][n.cx].y[lb] - curY;
                            if (dyb <= 0.65f && dyb >= -3.5f) okB = true;
                        }
                    if (!okA || !okB) continue;
                }
                sVisited[nz][nx][l] = true;
                sQueue[tail++] = (BFSNode){ (int16_t)nx, (int16_t)nz, (int8_t)l, (int16_t)head };
                if (nx == cx1 && nz == cz1 && l == lvl1) { found = tail-1; break; }
            }
            if (found >= 0) break;
        }
        head++;
    }
    if (found < 0) return false;

    BFSNode chain[NAV_MAX*NAV_LEVELS];
    int n = 0;
    for (int i = found; i >= 0 && n < NAV_MAX*NAV_LEVELS; i = sQueue[i].prev)
        chain[n++] = sQueue[i];
    Vector3 pts[NAV_MAX*NAV_LEVELS];
    for (int i = 0; i < n; i++) {
        NavCell *c = &sNav[chain[i].cz][chain[i].cx];
        float y = chain[i].lvl < c->count ? c->y[chain[i].lvl] : 0;
        pts[i] = Vec3(sNavMinX + chain[i].cx + 0.5f, y, sNavMinZ + chain[i].cz + 0.5f);
    }
    // greedy line-of-sight simplification
    Vector3 out[NAV_MAX*NAV_LEVELS];
    int m2 = 0;
    out[m2++] = pts[0];
    int i = 0;
    while (i < n-1 && m2 < maxLen-1) {
        int j = n-1;
        for (; j > i+1; j--)
            if (NavWalkableLine(pts[i], pts[j])) break;
        if (j <= i) j = i+1;
        out[m2++] = pts[j];
        i = j;
    }
    out[m2-1] = to;
    int copy = m2 < maxLen ? m2 : maxLen;
    for (int k = 0; k < copy; k++) path[k] = out[k];
    *pathLen = copy;
    return copy > 0;
}

bool NavWalkableLine(Vector3 a, Vector3 b) {
    Vector3 d = Vec3Sub(b, a);
    float len = Vec3Len(d);
    if (len < 1e-4f) return true;
    int steps = (int)ceilf(len / 0.5f);
    float curY = a.y;
    for (int i = 1; i <= steps; i++) {
        Vector3 nxt = Vec3Add(a, Vec3Scale(d, (float)i/steps));
        float gy = GroundHeightAt(nxt.x, nxt.z, curY + 1.0f);
        if (gy < -50) return false;
        float dy = gy - curY;
        if (dy > 0.65f || dy < -3.5f) return false;
        curY = gy;
    }
    return true;
}

// ============================================================================
// Selftest validation
// ============================================================================
bool MapValidate(MapId id, char *err, int errSize) {
    MapDef saved = g.map;
    MapDef *m = &sMaps[id];
    memset(m, 0, sizeof(MapDef));
    switch (id) {
        case MAP_THEATER:   BuildTheater(m);   break;
        case MAP_BUNKER:    BuildBunker(m);    break;
        case MAP_GRAVEYARD: BuildGraveyard(m); break;
        case MAP_DIE_ARISE: BuildDieArise(m);  break;
        default: break;
    }
    g.map = *m;
    #define FAIL(...) do { snprintf(err, errSize, __VA_ARGS__); g.map = saved; return false; } while (0)
    if (m->boxCount == 0) FAIL("no boxes");
    if (m->spawnCount == 0) FAIL("no spawns");
    if (m->doorCount == 0) FAIL("no doors");
    if (m->perkCount < 4) FAIL("needs 4 perk stations");
    if (m->wallbuyCount < 4) FAIL("needs >=4 wall buys");
    float gy = GroundHeightAt(m->playerStart.x, m->playerStart.z, m->playerStart.y + 0.6f);
    if (gy < -50) FAIL("player start has no ground");
    if (fabsf(gy - m->playerStart.y) > 2.0f) FAIL("player start ground mismatch (gy=%.2f)", gy);
    for (int i = 0; i < m->spawnCount; i++) {
        float sy = GroundHeightAt(m->spawns[i].pos.x, m->spawns[i].pos.z, m->spawns[i].pos.y + 6.0f);
        if (sy < -50) FAIL("spawn %d floats", i);
    }
    for (int i = 0; i < m->wallbuyCount; i++)
        if (m->wallbuys[i].widx < 0 || m->wallbuys[i].widx >= GUN_COUNT_TOTAL) FAIL("wallbuy %d bad weapon", i);
    float by = GroundHeightAt(m->boxPos.x, m->boxPos.z, m->boxPos.y + 6.0f);
    if (by < -50) FAIL("mystery box floats");
    NavRebuild();
    Vector3 path[48]; int pathLen = 0;
    Vector3 to = m->spawns[0].pos; to.y = GroundHeightAt(to.x, to.z, to.y + 6.0f);
    if (to.y < -50) to.y = 0;
    if (!NavFind(m->playerStart, to, path, &pathLen, 48))
        FAIL("no nav path from start to spawn 0");
    g.map = saved;
    if (err) err[0] = 0;
    return true;
    #undef FAIL
}
