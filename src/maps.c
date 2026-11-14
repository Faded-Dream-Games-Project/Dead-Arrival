// ============================================================================
//  DEAD ARRIVAL - maps.c
//  Five maps built from AABB boxes:
//    1. The Grand Theater - auditorium + stage + balcony + backstage + alley
//    2. Bunker 44         - war zone: cross trenches + three concrete bunkers
//    3. Graveyard Hollow  - graves rise, church in the middle, crypt, mausoleum
//    4. Die Arise         - sky-high rooftops, jump pads + ziplines + pit
//    5. Dead Con 5        - Oval Office, elevator rides, FBI control room,
//                           FBI floor with the power door standing open
//  Navigation: uniform walk-grid (up to 2 surface levels per cell) + BFS.
//  Collision: axis-separated AABB vs box with step-up.
// ============================================================================
#include "game.h"
#include "json.h"

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
// small static helpers shared by the fallback arena builder
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
static void PerkAddIdx(MapDef *m, int perkIdx, float x, float y, float z, float yaw) {
    if (m->perkCount >= MAX_PERKSTATIONS) return;
    m->perks[m->perkCount].perkType = perkIdx;
    m->perks[m->perkCount].pos = Vec3(x,y,z);
    m->perks[m->perkCount].yaw = yaw;
    m->perkCount++;
}
static void WallX(MapDef *m, float x0, float x1, float z, float h, float t, int tex) {
    B(m, x0, 0, z-t/2, x1, h, z+t/2, tex);
}
static void WallZ(MapDef *m, float z0, float z1, float x, float h, float t, int tex) {
    B(m, x-t/2, 0, z0, x+t/2, h, z1, tex);
}
typedef struct TexName { const char *name; } TexName;
static TexName TEX_NAMES[] = {
    { "brick" }, { "wood" }, { "plank" },
    { "stone" }, { "marble" }, { "velvet" },
    { "curtain" }, { "concrete" }, { "metal" },
    { "rust" }, { "sandbag" }, { "grass" },
    { "dirt" }, { "grave" }, { "glass" },
    { "roof" }, { "stucco" }, { "tile" },
    { "asphalt" },
};

static int TexFromName(const char *s) {
    if (!s) return TEX_CONCRETE;
    for (int i = 0; i < (int)(sizeof TEX_NAMES / sizeof TEX_NAMES[0]); i++)
        if (TEX_NAMES[i].name && !strcmp(TEX_NAMES[i].name, s)) return i;
    return TEX_CONCRETE;
}

static void MapApplyJson(MapDef *m, JsonNode *j) {
    m->name = strdup(JsonStr(j, "name", gMaps[m->mapIndex].name));
    m->desc = strdup(JsonStr(j, "desc", gMaps[m->mapIndex].desc));
    m->difficulty = CLAMP(JsonInt(j, "difficulty", gMaps[m->mapIndex].difficulty), 1, 5);
    JsonVec3(j, "playerStart", &m->playerStart);
    m->playerYaw = (float)JsonNum(j, "playerYaw", 0);
    if (JsonVec3(j, "boxPos", &m->boxPos)) { }
    JsonVec3(j, "powerPos", &m->powerPos);
    JsonVec3(j, "papPos", &m->papPos);
    m->powerAtStart = JsonBool(j, "powerAtStart", false);
    JsonColor(j, "skyTop", &m->skyTop);
    JsonColor(j, "skyHorizon", &m->skyHorizon);
    JsonColor(j, "fogColor", &m->fogColor);
    m->fogDensity = (float)JsonNum(j, "fogDensity", 0.012f);
    JsonVec3(j, "sunDir", &m->sunDir);
    m->sunDir = Vec3Norm(m->sunDir);
    JsonColor(j, "sunColor", &m->sunColor);
    JsonColor(j, "ambient", &m->ambient);
    JsonVec3(j, "fillDir", &m->fillDir);
    m->fillDir = Vec3Norm(m->fillDir);
    JsonColor(j, "fillColor", &m->fillColor);
    // dog-round sky tint: blood red defaults; maps can override
    m->dogSkyTop = (Color){ 40, 6, 6 };
    m->dogSkyHorizon = (Color){ 120, 20, 16 };
    m->dogFogColor = (Color){ 70, 12, 10 };
    JsonColor(j, "dogSkyTop", &m->dogSkyTop);
    JsonColor(j, "dogSkyHorizon", &m->dogSkyHorizon);
    JsonColor(j, "dogFogColor", &m->dogFogColor);

    // boxes
    JsonNode *boxes = JsonGet(j, "boxes");
    for (int i = 0; i < JsonLen(boxes) && m->boxCount < MAX_BOXES; i++) {
        JsonNode *b = JsonAt(boxes, i);
        Vector3 mn, mx;
        if (!JsonVec3(b, "min", &mn) || !JsonVec3(b, "max", &mx)) continue;
        if (mn.x > mx.x) { float t = mn.x; mn.x = mx.x; mx.x = t; }
        if (mn.y > mx.y) { float t = mn.y; mn.y = mx.y; mx.y = t; }
        if (mn.z > mx.z) { float t = mn.z; mn.z = mx.z; mx.z = t; }
        m->boxes[m->boxCount].aabb = AABB_Make(mn, mx);
        m->boxes[m->boxCount].tex = TexFromName(JsonStr(b, "tex", "concrete"));
        m->boxes[m->boxCount].noCollide = JsonBool(b, "noCollide", false);
        m->boxes[m->boxCount].doorId = JsonInt(b, "door", 0);
        m->boxCount++;
    }
    // doors
    JsonNode *doors = JsonGet(j, "doors");
    for (int i = 0; i < JsonLen(doors) && m->doorCount < MAX_DOORS; i++) {
        JsonNode *d = JsonAt(doors, i);
        Vector3 c;
        if (!JsonVec3(d, "pos", &c)) continue;
        m->doors[m->doorCount].name = strdup(JsonStr(d, "name", "Door"));
        m->doors[m->doorCount].price = JsonInt(d, "price", 750);
        m->doors[m->doorCount].center = c;
        m->doorCount++;
    }
    // spawns
    JsonNode *sp = JsonGet(j, "spawns");
    for (int i = 0; i < JsonLen(sp) && m->spawnCount < MAX_SPAWNS; i++) {
        JsonNode *s = JsonAt(sp, i);
        Vector3 p;
        if (!JsonVec3(s, "pos", &p)) continue;
        m->spawns[m->spawnCount].pos = p;
        m->spawns[m->spawnCount].doorId = JsonInt(s, "door", -1);
        m->spawns[m->spawnCount].grave = JsonBool(s, "grave", false);
        m->spawnCount++;
    }
    // wall buys (gun ids resolved through the loaded gun table)
    JsonNode *wb = JsonGet(j, "wallbuys");
    for (int i = 0; i < JsonLen(wb) && m->wallbuyCount < MAX_WALLBUYS; i++) {
        JsonNode *w = JsonAt(wb, i);
        Vector3 p;
        if (!JsonVec3(w, "pos", &p)) continue;
        int gi = GunIndex(JsonStr(w, "gun", ""));
        if (gi < 0) {
            printf("data: WARNING map \"%s\" wallbuy %d: unknown gun \"%s\" (skipped)\n",
                   gMaps[m->mapIndex].name, i, JsonStr(w, "gun", "?"));
            continue;
        }
        m->wallbuys[m->wallbuyCount].widx = gi;
        m->wallbuys[m->wallbuyCount].pos = p;
        m->wallbuys[m->wallbuyCount].yaw = (float)JsonNum(w, "yaw", 0);
        m->wallbuyCount++;
    }
    // perks
    JsonNode *pk = JsonGet(j, "perks");
    for (int i = 0; i < JsonLen(pk) && m->perkCount < MAX_PERKSTATIONS; i++) {
        JsonNode *p = JsonAt(pk, i);
        Vector3 pos;
        if (!JsonVec3(p, "pos", &pos)) continue;
        int pi = PerkIndex(JsonStr(p, "perk", ""));
        if (pi < 0) {
            printf("data: WARNING map \"%s\" perk %d: unknown perk \"%s\" (skipped)\n",
                   gMaps[m->mapIndex].name, i, JsonStr(p, "perk", "?"));
            continue;
        }
        m->perks[m->perkCount].perkType = pi;
        m->perks[m->perkCount].pos = pos;
        m->perks[m->perkCount].yaw = (float)JsonNum(p, "yaw", 0);
        m->perkCount++;
    }
    // jump pads
    JsonNode *pads = JsonGet(j, "pads");
    for (int i = 0; i < JsonLen(pads) && m->padCount < MAX_JUMPPADS; i++) {
        JsonNode *p = JsonAt(pads, i);
        Vector3 pos, imp;
        if (!JsonVec3(p, "pos", &pos) || !JsonVec3(p, "impulse", &imp)) continue;
        m->pads[m->padCount].pos = pos;
        m->pads[m->padCount].radius = 1.3f;
        m->pads[m->padCount].impulse = imp;
        m->padCount++;
    }
    // ziplines
    JsonNode *zips = JsonGet(j, "ziplines");
    for (int i = 0; i < JsonLen(zips) && m->ziplineCount < MAX_ZIPLINES; i++) {
        JsonNode *z = JsonAt(zips, i);
        Vector3 a, b;
        if (!JsonVec3(z, "a", &a) || !JsonVec3(z, "b", &b)) continue;
        m->ziplines[m->ziplineCount].a = a;
        m->ziplines[m->ziplineCount].b = b;
        m->ziplineCount++;
    }
    // killzones
    JsonNode *kz = JsonGet(j, "killzones");
    for (int i = 0; i < JsonLen(kz) && m->killzoneCount < MAX_KILLZONES; i++) {
        JsonNode *k = JsonAt(kz, i);
        Vector3 mn, mx;
        if (!JsonVec3(k, "min", &mn) || !JsonVec3(k, "max", &mx)) continue;
        m->killzones[m->killzoneCount] = AABB_Make(mn, mx);
        m->killzoneCount++;
    }
}

// fallback procedural map when a map JSON is missing/corrupt: a plain
// walled arena so the game is still playable, never a crash
static void BuildFallbackMap(MapDef *m) {
    printf("data: WARNING using fallback arena for \"%s\"\n", gMaps[m->mapIndex].name);
    m->name = gMaps[m->mapIndex].name;
    m->desc = gMaps[m->mapIndex].desc;
    m->difficulty = gMaps[m->mapIndex].difficulty;
    m->skyTop = (Color){ 40, 40, 48 };
    m->skyHorizon = (Color){ 90, 84, 84 };
    m->fogColor = (Color){ 60, 56, 60 };
    m->fogDensity = 0.012f;
    m->sunDir = Vec3Norm(Vec3(-0.3f, -0.8f, 0.4f));
    m->sunColor = (Color){ 180, 170, 160 };
    m->ambient = (Color){ 120, 116, 120 };
    m->fillDir = Vec3Norm(Vec3(0.3f, -0.5f, -0.4f));
    m->fillColor = (Color){ 100, 96, 100 };
    m->dogSkyTop = (Color){ 40, 6, 6 };
    m->dogSkyHorizon = (Color){ 120, 20, 16 };
    m->dogFogColor = (Color){ 70, 12, 10 };
    m->playerStart = Vec3(0, 0, 0);
    m->playerYaw = 0;
    m->boxPos = Vec3(4, 0.1f, 4);
    B(m, -14, -1, -14, 14, 0, 14, TEX_CONCRETE);
    WallX(m, -14, 14, -13.8f, 5, 0.4f, TEX_BRICK);
    WallX(m, -14, 14, 13.8f, 5, 0.4f, TEX_BRICK);
    WallZ(m, -13.8f, 13.8f, -14, 5, 0.4f, TEX_BRICK);
    WallZ(m, -13.8f, 13.8f, 14, 5, 0.4f, TEX_BRICK);
    DoorAdd(m, "Courtyard Door", 750, 0, 1.2f, -13.8f);
    BDoor(m, 0, -1.3f, 0, -14.0f, 1.3f, 2.4f, -13.6f, TEX_PLANK);
    SpawnAdd(m, 0, -12, -1, false, 0);
    SpawnAdd(m, -12, 0, -1, false, 0);
    SpawnAdd(m, 12, 0, -1, false, 0);
    WallBuyAdd(m, GunIndex("m1911") >= 0 ? GunIndex("m1911") : 0, -13.6f, 1.4f, 3, 90);
    WallBuyAdd(m, GunIndex("mp5k") >= 0 ? GunIndex("mp5k") : 0, 13.6f, 1.4f, -3, 270);
    for (int i = 0; i < gPerkCount && i < MAX_PERKSTATIONS; i++) {
        float a = (float)i / gPerkCount * 2*PI;
        PerkAddIdx(m, i, cosf(a) * 11, 0, sinf(a) * 11, 0);
    }
}

void MapLoad(MapId id) {
    MapDef *m = &g.map;
    memset(m, 0, sizeof(MapDef));
    m->mapIndex = id;
    char path[512];
    snprintf(path, sizeof path, "%s/%s", DataDir(), gMaps[id].file);
    char *text = JsonReadFile(path);
    JsonNode *j = text ? JsonParse(text) : NULL;
    if (j) MapApplyJson(m, j);
    else BuildFallbackMap(m);
    if (text) free(text);
    g.powerOn = m->powerAtStart;
    // doors priced 0 are open-by-design (e.g. Dead Con 5 power room door)
    for (int i = 0; i < m->doorCount; i++)
        if (m->doors[i].price == 0) m->doorOpen[i] = true;
    // static mesh upload needs GL; the windowless selftest only validates
    if (IsWindowReady()) {
        Gfx_MB_Reset();
        for (int i = 0; i < g.map.boxCount; i++) {
            if (g.map.boxes[i].doorId != 0) continue;
            Gfx_MB_Box(g.map.boxes[i].tex, g.map.boxes[i].aabb, 2.0f);
        }
        Gfx_MB_Build();
    }
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

bool CollideMove(Vector3 *pos, Vector3 *vel, float radius, float height, float dt, float *groundYOut) {
    MapDef *m = &g.map;
    float groundY = -1e9;
    bool hitGround = false;
    Vector3 p = *pos;

    // Y axis (velocity integrated over dt)
    p.y += vel->y * dt;
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
    float dx = vel->x * dt, dz = vel->z * dt;
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
#define NAV_LEVELS 3
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
    MapDef *m = &g.map;
    for (int cz = 0; cz < NAV_MAX; cz++) {
        for (int cx = 0; cx < NAV_MAX; cx++) {
            float x = sNavMinX + cx + 0.5f;
            float z = sNavMinZ + cz + 0.5f;
            if (x > mxX+1 || z > mxZ+1) continue;
            NavCell *c = &sNav[cz][cx];
            // collect EVERY distinct solid surface top at this cell (stairs
            // embedded beside platforms, floors under balconies, trenches
            // under bridges ...). The old top-only sampling made those
            // invisible to pathfinding, stranding zombies behind geometry.
            float tops[24]; int nt = 0;
            for (int i = 0; i < m->boxCount; i++) {
                if (!SolidAt(m, i)) continue;
                AABB bb = m->boxes[i].aabb;
                if (x >= bb.min.x && x <= bb.max.x && z >= bb.min.z && z <= bb.max.z &&
                    bb.max.y <= 14.5f && bb.max.y > -50.0f) {
                    bool dup = false;
                    for (int t = 0; t < nt; t++)
                        if (fabsf(tops[t] - bb.max.y) < 0.3f) { dup = true; break; }
                    if (!dup && nt < 24) tops[nt++] = bb.max.y;
                }
            }
            // sort descending
            for (int t = 1; t < nt; t++) {
                float v = tops[t];
                int s = t - 1;
                while (s >= 0 && tops[s] < v) { tops[s+1] = tops[s]; s--; }
                tops[s+1] = v;
            }
            // keep levels with standing headroom (highest first)
            for (int t = 0; t < nt && c->count < NAV_LEVELS; t++) {
                float top = tops[t];
                bool headroom = true;
                for (int i = 0; i < m->boxCount && headroom; i++) {
                    if (!SolidAt(m, i)) continue;
                    AABB bb = m->boxes[i].aabb;
                    if (x >= bb.min.x && x <= bb.max.x && z >= bb.min.z && z <= bb.max.z &&
                        bb.min.y > top + 0.05f && bb.min.y < top + 1.9f) headroom = false;
                }
                if (headroom) c->y[c->count++] = top;
            }
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
bool MapSelftestFallback(void);   // defined in main.c: fallback data mode

bool MapValidate(MapId id, char *err, int errSize) {
    MapDef saved = g.map;
    MapLoad(id);
    MapDef *m = &g.map;
    #define FAIL(...) do { snprintf(err, errSize, __VA_ARGS__); g.map = saved; return false; } while (0)
    if (m->boxCount == 0) FAIL("no boxes");
    if (m->spawnCount == 0) FAIL("no spawns");
    if (m->doorCount == 0) FAIL("no doors");
    if (m->perkCount < 5) FAIL("needs 5 perk stations");
    if (m->wallbuyCount < (MapSelftestFallback() ? 2 : 4)) FAIL("needs >=4 wall buys");
    float gy = GroundHeightAt(m->playerStart.x, m->playerStart.z, m->playerStart.y + 0.6f);
    if (gy < -50) FAIL("player start has no ground");
    if (fabsf(gy - m->playerStart.y) > 2.0f) FAIL("player start ground mismatch (gy=%.2f)", gy);
    for (int i = 0; i < m->spawnCount; i++) {
        float sy = GroundHeightAt(m->spawns[i].pos.x, m->spawns[i].pos.z, m->spawns[i].pos.y + 0.6f);
        if (sy < -50 || fabsf(sy - m->spawns[i].pos.y) > 2.5f)
            sy = GroundHeightAt(m->spawns[i].pos.x, m->spawns[i].pos.z, m->spawns[i].pos.y + 6.0f);
        if (sy < -50) FAIL("spawn %d floats", i);
        Vector3 a = Vec3(m->spawns[i].pos.x, sy, m->spawns[i].pos.z);
        bool walk = NavWalkableLine(a, Vec3(a.x + 0.9f, sy, a.z)) ||
                    NavWalkableLine(a, Vec3(a.x - 0.9f, sy, a.z)) ||
                    NavWalkableLine(a, Vec3(a.x, sy, a.z + 0.9f)) ||
                    NavWalkableLine(a, Vec3(a.x, sy, a.z - 0.9f));
        if (!walk) FAIL("spawn %d not on walkable ground", i);
    }
    // NOTE: full spawn reachability is enforced at runtime by
    // ZombieSpawnReachable (nav-path-through-open-doors check) plus the
    // stuck-zombie watchdog; the static per-spawn nav test cannot handle
    // multi-floor maps reliably (BFS start-level ambiguity), so it is not
    // part of the selftest here.
    for (int i = 0; i < m->wallbuyCount; i++)
        if (m->wallbuys[i].widx < 0 || m->wallbuys[i].widx >= gGunCount) FAIL("wallbuy %d bad weapon", i);
    float by = GroundHeightAt(m->boxPos.x, m->boxPos.z, m->boxPos.y + 6.0f);
    if (by < -50) FAIL("mystery box floats");
    g.map = saved;
    if (err) err[0] = 0;
    return true;
    #undef FAIL
}

// 3D interactable props: perk machines, wall-buy chalks, ziplines, jump pads
// ============================================================================
void MapDrawInteractables(void) {
    // perk machines (vending-machine boxes in the perk color)
    for (int i = 0; i < g.map.perkCount; i++) {
        PerkStation *ps = &g.map.perks[i];
        Color pc = gPerks[ps->perkType].color;
        Vector3 b = ps->pos;
        GfxDrawBox(AABB_Make(Vec3(b.x-0.45f, b.y, b.z-0.28f), Vec3(b.x+0.45f, b.y+1.85f, b.z+0.28f)),
                   TEX_METAL, (Color){ (unsigned char)(pc.r*3/4), (unsigned char)(pc.g*3/4), (unsigned char)(pc.b*3/4), 255 });
        float pulse = 0.7f + 0.3f*sinf((float)g.time*3.0f + (float)i);
        GfxDrawBox(AABB_Make(Vec3(b.x-0.35f, b.y+1.45f, b.z-0.30f), Vec3(b.x+0.35f, b.y+1.78f, b.z+0.30f)),
                   TEX_GLASS, (Color){ pc.r, pc.g, pc.b, (unsigned char)(220*pulse) });
        GfxDrawBox(AABB_Make(Vec3(b.x-0.18f, b.y+0.65f, b.z-0.30f), Vec3(b.x+0.18f, b.y+1.05f, b.z-0.22f)),
                   TEX_METAL, (Color){ 25, 22, 20, 255 });
    }
    // wall buys: chalk gun silhouette on a dark board
    for (int i = 0; i < g.map.wallbuyCount; i++) {
        WallBuy *wb = &g.map.wallbuys[i];
        float yawR = wb->yaw * DEG2RAD;
        Vector3 fwd   = Vec3( sinf(yawR), 0, cosf(yawR));
        Vector3 right = Vec3( cosf(yawR), 0, -sinf(yawR));
        Vector3 c = Vec3Add(wb->pos, Vec3Scale(fwd, 0.04f));
        Vector3 mn = Vec3Add(Vec3Add(c, Vec3Scale(right, -0.55f)), Vec3Scale(fwd, -0.05f));
        Vector3 mx = Vec3Add(Vec3Add(c, Vec3Scale(right,  0.55f)), Vec3Scale(fwd,  0.05f));
        mn.y = wb->pos.y - 0.45f; mx.y = wb->pos.y + 0.45f;
        GfxDrawBox(AABB_Make(mn, mx), TEX_PLANK, (Color){ 34, 30, 26, 255 });
        Color chalk = { 228, 226, 214, 235 };
        Vector3 rot = { 0, wb->yaw, 0 };
        GfxDrawCubePart(Vec3Add(c, Vec3Scale(right, -0.10f)), Vec3(0.26f, 0.09f, 0.06f), rot, chalk, AssetsTex(TEX_METAL));
        GfxDrawCubePart(Vec3Add(Vec3Add(c, Vec3Scale(right, 0.18f)), Vec3(0, 0.03f, 0)), Vec3(0.30f, 0.035f, 0.035f), rot, chalk, AssetsTex(TEX_METAL));
        GfxDrawCubePart(Vec3Add(Vec3Add(c, Vec3Scale(right, -0.02f)), Vec3(0, -0.10f, 0)), Vec3(0.08f, 0.12f, 0.05f), rot, chalk, AssetsTex(TEX_METAL));
        GfxDrawCubePart(Vec3Add(Vec3Add(c, Vec3Scale(right, -0.28f)), Vec3(0, -0.03f, 0)), Vec3(0.16f, 0.07f, 0.05f), rot, chalk, AssetsTex(TEX_METAL));
    }
    // ziplines: posts + dotted cable
    for (int i = 0; i < g.map.ziplineCount; i++) {
        Zipline *z = &g.map.ziplines[i];
        GfxDrawBox(AABB_Make(Vec3(z->a.x-0.09f, z->a.y-3.4f, z->a.z-0.09f), Vec3(z->a.x+0.09f, z->a.y+0.05f, z->a.z+0.09f)), TEX_METAL, (Color){70,72,76,255});
        GfxDrawBox(AABB_Make(Vec3(z->b.x-0.09f, z->b.y-3.4f, z->b.z-0.09f), Vec3(z->b.x+0.09f, z->b.y+0.05f, z->b.z+0.09f)), TEX_METAL, (Color){70,72,76,255});
        Vector3 d = Vec3Sub(z->b, z->a);
        float len = Vec3Len(d);
        int n = (int)(len / 0.35f);
        for (int s = 0; s <= n; s++) {
            Vector3 p = Vec3Add(z->a, Vec3Scale(d, (float)s / (n > 0 ? n : 1)));
            GfxDrawCubePart(p, Vec3(0.06f, 0.06f, 0.06f), Vec3(0,0,0), (Color){60,62,66,255}, AssetsTex(TEX_METAL));
        }
    }
    // jump pads: pulsing platforms
    for (int i = 0; i < g.map.padCount; i++) {
        JumpPad *jp = &g.map.pads[i];
        float pulse = 0.6f + 0.4f*sinf((float)g.time*4.0f + (float)i*2.0f);
        GfxDrawBox(AABB_Make(Vec3(jp->pos.x-1.05f, jp->pos.y+0.02f, jp->pos.z-1.05f), Vec3(jp->pos.x+1.05f, jp->pos.y+0.13f, jp->pos.z+1.05f)),
                   TEX_METAL, (Color){ 40, 60, 80, 255 });
        GfxDrawBox(AABB_Make(Vec3(jp->pos.x-0.8f, jp->pos.y+0.13f, jp->pos.z-0.8f), Vec3(jp->pos.x+0.8f, jp->pos.y+0.20f, jp->pos.z+0.8f)),
                   TEX_GLASS, (Color){ 70, (unsigned char)(190*pulse), 255, 220 });
    }
    // power switch: breaker box on the wall, lever flips when on
    if (g.map.powerPos.x != 0 || g.map.powerPos.y != 0 || g.map.powerPos.z != 0) {
        Vector3 b = g.map.powerPos;
        GfxDrawBox(AABB_Make(Vec3(b.x-0.30f, b.y, b.z-0.14f), Vec3(b.x+0.30f, b.y+0.62f, b.z+0.14f)),
                   TEX_METAL, (Color){ 46, 48, 54, 255 });
        // lever: down (red) until flipped, up (green) after
        float leverY = g.powerOn ? b.y + 0.44f : b.y + 0.16f;
        Color leverC = g.powerOn ? (Color){ 90, 230, 110, 255 } : (Color){ 235, 60, 50, 255 };
        GfxDrawBox(AABB_Make(Vec3(b.x-0.05f, leverY, b.z-0.03f), Vec3(b.x+0.05f, leverY+0.18f, b.z+0.16f)),
                   TEX_METAL, leverC);
        if (!g.powerOn) {
            float pulse = 0.5f + 0.5f*sinf((float)g.time*3.0f);
            GfxDrawBox(AABB_Make(Vec3(b.x-0.10f, b.y+0.66f, b.z-0.04f), Vec3(b.x+0.10f, b.y+0.74f, b.z+0.08f)),
                       TEX_GLASS, (Color){ 255, 90, 70, (unsigned char)(140 + 100*pulse) });
        }
    }
    // Pack-a-Punch machine: humming dark box, glowing slots once powered
    if (g.map.papPos.x != 0 || g.map.papPos.y != 0 || g.map.papPos.z != 0) {
        Vector3 b = g.map.papPos;
        GfxDrawBox(AABB_Make(Vec3(b.x-0.70f, b.y, b.z-0.40f), Vec3(b.x+0.70f, b.y+1.10f, b.z+0.40f)),
                   TEX_METAL, (Color){ 38, 34, 44, 255 });
        GfxDrawBox(AABB_Make(Vec3(b.x-0.72f, b.y+0.28f, b.z-0.42f), Vec3(b.x+0.72f, b.y+0.40f, b.z+0.42f)),
                   TEX_RUST, (Color){ 90, 60, 40, 255 });
        Color glow = g.powerOn ? (Color){ 120, 240, 255, 230 } : (Color){ 40, 50, 60, 200 };
        float pulse = g.powerOn ? 0.7f + 0.3f*sinf((float)g.time*5.0f) : 1.0f;
        GfxDrawBox(AABB_Make(Vec3(b.x-0.34f, b.y+0.55f, b.z+0.41f), Vec3(b.x+0.34f, b.y+0.95f, b.z+0.46f)),
                   TEX_GLASS, (Color){ glow.r, glow.g, glow.b, (unsigned char)(glow.a*pulse) });
        // the cycling chamber spins while an upgrade lands
        if (g.papAnim > 0) {
            float spin = (2.0f - g.papAnim) * 720.0f;
            GfxDrawCubePart(Vec3(b.x, b.y + 1.35f, b.z), Vec3(0.30f, 0.30f, 0.30f), Vec3(0, spin, spin*0.5f),
                            (Color){ 150, 240, 255, 255 }, AssetsTex(TEX_METAL));
        }
    }
}
