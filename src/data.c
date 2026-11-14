// ============================================================================
//  DEAD ARRIVAL - data.c
//  Content loading: gameplay.json, guns.json, perks.json, skins.json,
//  maps.json (+ maps/<file>.json per map). The C code only contains the
//  engine; all content lives in data files under assets/.
//  Every loader validates what it reads and falls back to the built-in
//  default (the previous hardcoded values) per-field or per-entry, so a
//  missing/corrupt file never crashes the game - at worst one item runs
//  on its default stats.
// ============================================================================
#include "data.h"
#include "json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Built-in defaults (the pre-data-pass values; used when files are missing)
// ---------------------------------------------------------------------------
static const GunDefDefault GUN_DEFAULTS[] = {
//  id               name         mode           dmg pel mag  rsv rpm  rel   sprd  adsSpr rec  wall boxW model               accent
    { "m1911",       "M1911",     "semi",         35, 1,  8,  80,  220, 1.60f, 0.030f, 0.006f, 1.2f,  500,   0, "m1911",     { 72, 66, 58, 255 } },
    { "python",      "Python",    "semi",         95, 1,  6,  60,  160, 2.40f, 0.024f, 0.004f, 2.2f, 1000,  4, "python",    {163,  98, 44, 255 } },
    { "cz75",        "CZ75",      "semi",         40, 1, 16, 128, 320, 1.70f, 0.034f, 0.007f, 1.4f, 1000,  3, "cz75",      { 60, 62, 68, 255 } },
    { "mp5k",        "MP5K",      "auto",         26, 1, 30, 180, 750, 1.90f, 0.050f, 0.013f, 1.0f, 1200,  8, "mp5k",      { 46, 52, 46, 255 } },
    { "ak74u",       "AK-74u",    "auto",         38, 1, 30, 240, 700, 2.30f, 0.055f, 0.012f, 1.7f, 1200, 10, "ak74u",     {120,  78, 36, 255 } },
    { "spectre",     "Spectre",   "auto",         28, 1, 30, 210, 818, 2.00f, 0.048f, 0.012f, 1.1f, 1300,  6, "spectre",   { 38, 40, 44, 255 } },
    { "pm63",        "PM63",      "auto",         24, 1, 32, 192, 850, 1.80f, 0.055f, 0.014f, 1.2f, 1000,  4, "pm63",      { 52, 54, 58, 255 } },
    { "mpl",         "MPL",       "auto",         30, 1, 32, 224, 570, 2.10f, 0.046f, 0.011f, 1.3f, 1000,  5, "mpl",       { 58, 62, 66, 255 } },
    { "olympia",     "Olympia",   "semi",         25, 8,  2,  40,  120, 2.60f, 0.075f, 0.045f, 3.0f,  500,   0, "olympia",   { 92, 70, 40, 255 } },
    { "stakeout",    "Stakeout",  "pump",         24, 8,  6,  54,  150, 2.90f, 0.075f, 0.042f, 3.2f, 1500,  6, "stakeout",  { 88, 66, 38, 255 } },
    { "hs10",        "HS-10",     "semi",         26, 6,  6,  48,  210, 2.60f, 0.080f, 0.048f, 2.8f,    0,  6, "hs10",      { 66, 52, 34, 255 } },
    { "chinalake",   "China Lake","pump",         90, 1,  4,  32,  100, 3.10f, 0.045f, 0.020f, 3.5f,    0,  4, "chinalake", { 74, 84, 60, 255 } },
    { "m16",         "M16",       "burst",        42, 1, 30, 240, 900, 2.20f, 0.035f, 0.008f, 1.6f, 1200,  7, "m16",       { 44, 46, 42, 255 } },
    { "enfield",     "Enfield",   "auto",         44, 1, 30, 240, 725, 2.60f, 0.044f, 0.010f, 1.8f, 1200,  5, "enfield",   { 76, 78, 60, 255 } },
    { "commando",    "Commando",  "auto",         45, 1, 30, 270, 750, 2.60f, 0.042f, 0.009f, 1.8f,    0,  9, "commando",  { 40, 44, 48, 255 } },
    { "galil",       "Galil",     "auto",         44, 1, 35, 315, 700, 2.90f, 0.046f, 0.010f, 1.9f,    0,  8, "galil",     { 84, 70, 34, 255 } },
    { "famas",       "FAMAS",     "burst",        55, 1, 30, 240, 1000,2.40f, 0.040f, 0.008f, 1.5f,    0,  6, "famas",     { 46, 42, 36, 255 } },
    { "rpk",         "RPK",       "auto",         46, 1,100, 400, 650, 5.00f, 0.052f, 0.011f, 2.0f,    0,  7, "rpk",       { 76, 62, 30, 255 } },
    { "hk21",        "HK21",      "auto",         50, 1,125, 375, 600, 5.40f, 0.055f, 0.012f, 2.1f,    0,  5, "hk21",      { 52, 56, 50, 255 } },
    { "l96a1",       "L96A1",     "bolt",        250, 1,  5,  40,   50, 3.60f, 0.028f, 0.001f, 3.4f,    0,  3, "l96a1",     { 58, 62, 44, 255 } },
    { "knife",       "Knife",     "melee",       150, 1,  0,  0,   90, 0.55f, 0.000f, 0.000f, 0.0f,    0,  0, "knife",     {180,180,170, 255 } },
};
#define DEFAULT_GUN_COUNT ((int)(sizeof GUN_DEFAULTS / sizeof GUN_DEFAULTS[0]))

static const struct { const char *id, *name, *desc; int price; Color color; } PERK_DEFAULTS[] = {
    { "revive", "Self Revive",     "Get back up on your own when downed (one use per round)", 500,  { 90, 220, 120, 255 } },
    { "regen",  "Regen Soda",      "Health regenerates 10 s after the last zombie hit",       1500, {230, 120,  60, 255 } },
    { "swift",  "Swift Soda",      "Reload 40% faster",                                      3000, {240, 200,  80, 255 } },
    { "rapid",  "Rapid Cola",      "Fire 25% faster",                                        3000, {220,  60,  60, 255 } },
    { "stamin", "Stamin-Up",       "Move and sprint faster",                                  2000, { 90, 200, 220, 255 } },
    { "jugg",   "Juggernaut Soda", "Survive 4 hits instead of 2 (hits reset after 10 s)",    2500, {200,  50,  50, 255 } },
};
#define DEFAULT_PERK_COUNT ((int)(sizeof PERK_DEFAULTS / sizeof PERK_DEFAULTS[0]))

// default skins (shown when skins.json is missing); WHITE = keep model colors
static const SkinDef SKIN_DEFAULTS[] = {
    { "default", "Default",  {255,255,255,255}, {255,255,255,255}, {255,255,255,255} },
    { "midnight","Midnight", { 52, 54, 58,255}, { 34, 35, 38,255}, {120,130,255,255} },
    { "desert",  "Desert",   {168,142, 90,255}, {120, 98, 60,255}, { 92, 70, 40,255} },
    { "crimson", "Crimson",  {150, 40, 40,255}, { 90, 24, 24,255}, {220,160, 60,255} },
    { "forest",  "Forest",   { 60, 92, 56,255}, { 40, 62, 38,255}, {130,150,110,255} },
};
#define DEFAULT_SKIN_COUNT ((int)(sizeof SKIN_DEFAULTS / sizeof SKIN_DEFAULTS[0]))

static const char *sDataDir = "assets";

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
void DataSetDir(const char *dir) { sDataDir = dir; }
const char *DataDir(void) { return sDataDir; }

// resolve "<rel>" against the data dir and parse it; NULL (with a printed
// warning) when missing or invalid - callers fall back to defaults
static JsonNode *LoadJson(const char *relPath, const char *what) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s", sDataDir, relPath);
    char *text = JsonReadFile(path);
    if (!text) {
        printf("data: WARNING %s missing (%s) - using built-in defaults\n", what, path);
        return NULL;
    }
    JsonNode *root = JsonParse(text);
    free(text);
    if (!root) {
        printf("data: WARNING %s is invalid JSON (%s) - using built-in defaults\n", what, path);
        return NULL;
    }
    printf("data: loaded %s (%s)\n", what, path);
    return root;
}

static FireMode ParseFireMode(const char *s, FireMode def) {
    if (!s) return def;
    if (!strcmp(s, "semi"))  return FIRE_SEMI;
    if (!strcmp(s, "auto"))  return FIRE_AUTO;
    if (!strcmp(s, "burst")) return FIRE_BURST;
    if (!strcmp(s, "bolt"))  return FIRE_BOLT;
    if (!strcmp(s, "pump"))  return FIRE_PUMP;
    if (!strcmp(s, "melee")) return FIRE_MELEE;
    return def;
}

// ---------------------------------------------------------------------------
// Gameplay config
// ---------------------------------------------------------------------------
GameplayConfig gGameplay;

static void GameplayDefaults(void) {
    gGameplay = (GameplayConfig){
        .pointsStart   = 500,
        .pointsPerHit  = 10,
        .pointsPerKill = 50,
        .boxPrice      = 950,
        .papPrice      = 5000,
        .papDamageMul  = 2.5f,
        .zombieHitDmg  = 50.0f,
        .regenDelay    = 10.0f,
        .regenRate     = 25.0f,
        // health curve: R1 100, R2 200, +100/round to R9, +15%/round after
        .hpBase        = 100.0f,
        .hpRound2      = 200.0f,
        .hpLinearPerRound = 100.0f,
        .hpLinearUntil = 9,
        .hpGrowth      = 1.15f,
        // speed: 0.95 + 0.055*(round-1), clamp [0.9, 2.6]
        .speedBase     = 0.95f,
        .speedPerRound = 0.055f,
        .speedMin      = 0.9f,
        .speedMax      = 2.6f,
        .runnerChanceBase = 4,
        .runnerFromRound  = 5,
        // quota: 6 * 1.22^(round-1), cap 120
        .quotaBase     = 6.0f,
        .quotaGrowth   = 1.22f,
        .quotaCap      = 120,
        // hellhounds: every 5th round starting round 5
        .dogEvery      = 5,
        .dogFirstRound = 5,
        .dogHp         = 260.0f,
        .dogSpeed      = 4.6f,
        .dogQuotaMin   = 6,
        .dogQuotaMax   = 12,
        // max ammo drop: 3% of zombie kills, despawns after 30 s
        .maxAmmoChance = 3,
        .maxAmmoLife   = 30.0f,
    };
}

static void LoadGameplay(void) {
    GameplayDefaults();
    JsonNode *root = LoadJson("gameplay.json", "gameplay.json");
    if (!root) return;
    gGameplay.pointsStart   = JsonInt(root, "pointsStart",   gGameplay.pointsStart);
    gGameplay.pointsPerHit  = JsonInt(root, "pointsPerHit",  gGameplay.pointsPerHit);
    gGameplay.pointsPerKill = JsonInt(root, "pointsPerKill", gGameplay.pointsPerKill);
    gGameplay.boxPrice      = JsonInt(root, "boxPrice",      gGameplay.boxPrice);
    gGameplay.papPrice      = JsonInt(root, "papPrice",      gGameplay.papPrice);
    gGameplay.papDamageMul  = (float)JsonNum(root, "papDamageMul", gGameplay.papDamageMul);
    gGameplay.zombieHitDmg  = (float)JsonNum(root, "zombieHitDmg", gGameplay.zombieHitDmg);
    gGameplay.regenDelay    = (float)JsonNum(root, "regenDelay",  gGameplay.regenDelay);
    gGameplay.regenRate     = (float)JsonNum(root, "regenRate",   gGameplay.regenRate);

    JsonNode *hp = JsonGet(root, "zombieHealth");
    if (hp) {
        gGameplay.hpBase           = (float)JsonNum(hp, "base",           gGameplay.hpBase);
        gGameplay.hpRound2         = (float)JsonNum(hp, "round2",         gGameplay.hpRound2);
        gGameplay.hpLinearPerRound = (float)JsonNum(hp, "linearPerRound", gGameplay.hpLinearPerRound);
        gGameplay.hpLinearUntil    = JsonInt  (hp, "linearUntil",         gGameplay.hpLinearUntil);
        gGameplay.hpGrowth         = (float)JsonNum(hp, "growthPerRound", gGameplay.hpGrowth);
    }
    JsonNode *sp = JsonGet(root, "zombieSpeed");
    if (sp) {
        gGameplay.speedBase     = (float)JsonNum(sp, "base",     gGameplay.speedBase);
        gGameplay.speedPerRound = (float)JsonNum(sp, "perRound", gGameplay.speedPerRound);
        gGameplay.speedMin      = (float)JsonNum(sp, "min",      gGameplay.speedMin);
        gGameplay.speedMax      = (float)JsonNum(sp, "max",      gGameplay.speedMax);
    }
    gGameplay.runnerChanceBase = JsonInt(root, "runnerChanceBase", gGameplay.runnerChanceBase);
    gGameplay.runnerFromRound  = JsonInt(root, "runnerFromRound",  gGameplay.runnerFromRound);
    JsonNode *q = JsonGet(root, "zombieQuota");
    if (q) {
        gGameplay.quotaBase   = (float)JsonNum(q, "base",   gGameplay.quotaBase);
        gGameplay.quotaGrowth = (float)JsonNum(q, "growth", gGameplay.quotaGrowth);
        gGameplay.quotaCap    = JsonInt  (q, "cap",         gGameplay.quotaCap);
    }
    JsonNode *dog = JsonGet(root, "hellhounds");
    if (dog) {
        gGameplay.dogEvery      = JsonInt  (dog, "every",      gGameplay.dogEvery);
        gGameplay.dogFirstRound = JsonInt  (dog, "firstRound", gGameplay.dogFirstRound);
        gGameplay.dogHp         = (float)JsonNum(dog, "hp",    gGameplay.dogHp);
        gGameplay.dogSpeed      = (float)JsonNum(dog, "speed", gGameplay.dogSpeed);
        gGameplay.dogQuotaMin   = JsonInt  (dog, "quotaMin",   gGameplay.dogQuotaMin);
        gGameplay.dogQuotaMax   = JsonInt  (dog, "quotaMax",   gGameplay.dogQuotaMax);
    }
    JsonNode *ma = JsonGet(root, "maxAmmoDrop");
    if (ma) {
        gGameplay.maxAmmoChance = JsonInt  (ma, "chancePercent", gGameplay.maxAmmoChance);
        gGameplay.maxAmmoLife   = (float)JsonNum(ma, "lifeSeconds", gGameplay.maxAmmoLife);
    }
}

// ---------------------------------------------------------------------------
// Guns
// ---------------------------------------------------------------------------
WeaponDef gWeapons[NUM_GUN_SLOTS_MAX];
int       gGunCount = 0;
char      gGunIds[NUM_GUN_SLOTS_MAX][32];

int GunIndex(const char *id) {
    if (!id) return -1;
    for (int i = 0; i < gGunCount; i++)
        if (!strcmp(gGunIds[i], id)) return i;
    return -1;
}

int GunKnifeIndex(void) {
    int i = GunIndex("knife");
    return i >= 0 ? i : gGunCount - 1;   // knife is last in the defaults
}

static void ApplyGunDefaults(WeaponDef *w, char *idOut, const GunDefDefault *d) {
    memset(w, 0, sizeof *w);
    snprintf(idOut, 32, "%s", d->id);
    w->name      = d->name;
    w->mode      = ParseFireMode(d->mode, FIRE_SEMI);
    w->damage    = d->damage;
    w->pellets   = d->pellets;
    w->mag       = d->mag;
    w->reserve   = d->rsv;
    w->rpm       = d->rpm;
    w->reloadTime= d->rel;
    w->spread    = d->sprd;
    w->adsSpread = d->adsSpr;
    w->recoil    = d->rec;
    w->wallPrice = d->wall;
    w->boxWeight = d->boxW;
    w->modelId   = d->model;
    w->accent    = d->accent;
    // ADS FOV defaults preserved from the old table
    w->adsFov    = 50.0f;
    if (!strcmp(d->id, "l96a1"))  w->adsFov = 30.0f;
    if (!strcmp(d->id, "python")) w->adsFov = 48.0f;
    if (!strcmp(d->id, "m1911") || !strcmp(d->id, "cz75")) w->adsFov = 52.0f;
    if (strstr(d->id, "olympia") || strstr(d->id, "stakeout") ||
        strstr(d->id, "hs10") || strstr(d->id, "chinalake")) w->adsFov = 55.0f;
    if (!strcmp(d->id, "ak74u") || !strcmp(d->id, "mpl") || !strcmp(d->id, "rpk") ||
        !strcmp(d->id, "hk21")) w->adsFov = 50.0f;
    w->papDamageMul = 2.5f;
    w->papMagBonus  = 0;
}

static void LoadGuns(void) {
    gGunCount = 0;
    JsonNode *root = LoadJson("guns.json", "guns.json");
    if (!root || root->type != JSON_OBJECT) {
        if (root) printf("data: WARNING guns.json is not an object - using built-in defaults\n");
        for (int i = 0; i < DEFAULT_GUN_COUNT; i++)
            ApplyGunDefaults(&gWeapons[i], gGunIds[i], &GUN_DEFAULTS[i]);
        gGunCount = DEFAULT_GUN_COUNT;
        return;
    }
    // entries keyed by id: { "m1911": {...}, ... }
    for (int i = 0; i < root->size && gGunCount < NUM_GUN_SLOTS_MAX; i++) {
        JsonNode *e = root->fields[i].val;
        const char *id = root->fields[i].key;
        if (!e || e->type != JSON_OBJECT) continue;
        WeaponDef w;
        char idBuf[32];
        // start from the built-in default with the same id when present, so
        // a partial JSON entry still yields a complete, playable gun
        const GunDefDefault *def = NULL;
        for (int d = 0; d < DEFAULT_GUN_COUNT; d++)
            if (!strcmp(GUN_DEFAULTS[d].id, id)) { def = &GUN_DEFAULTS[d]; break; }
        if (def) {
            ApplyGunDefaults(&w, idBuf, def);
        } else {
            // brand-new gun (zero C changes): sane generic defaults
            ApplyGunDefaults(&w, idBuf, &GUN_DEFAULTS[0]);
            w.name = id; w.damage = 30; w.wallPrice = 0; w.boxWeight = 0;
            w.mode = FIRE_SEMI; w.adsFov = 50.0f;
        }
        snprintf(idBuf, sizeof idBuf, "%s", id);
        // per-field overrides
        w.name      = JsonStr(e, "name", w.name);
        w.mode      = ParseFireMode(JsonStr(e, "mode", NULL), w.mode);
        w.damage    = JsonInt(e, "damage", w.damage);
        w.pellets   = JsonInt(e, "pellets", w.pellets);
        w.mag       = JsonInt(e, "mag", w.mag);
        w.reserve   = JsonInt(e, "reserve", w.reserve);
        w.rpm       = JsonInt(e, "rpm", w.rpm);
        w.reloadTime= (float)JsonNum(e, "reloadTime", w.reloadTime);
        w.spread    = (float)JsonNum(e, "spread", w.spread);
        w.adsSpread = (float)JsonNum(e, "adsSpread", w.adsSpread);
        w.recoil    = (float)JsonNum(e, "recoil", w.recoil);
        w.wallPrice = JsonInt(e, "wallPrice", w.wallPrice);
        w.boxWeight = JsonInt(e, "boxWeight", w.boxWeight);
        w.modelId   = JsonStr(e, "model", w.modelId);
        w.adsFov    = (float)JsonNum(e, "adsFov", w.adsFov);
        w.papDamageMul = (float)JsonNum(e, "papDamageMul", w.papDamageMul);
        w.papMagBonus  = JsonInt(e, "papMagBonus", w.papMagBonus);
        w.muzzleZ   = (float)JsonNum(e, "muzzleZ", w.muzzleZ);
        JsonColor(e, "accent", &w.accent);
        // sanity clamps - never let a data typo make an unusable gun
        if (w.damage < 1) w.damage = 1;
        if (w.pellets < 1) w.pellets = 1;
        if (w.mag < 0) w.mag = 0;
        if (w.rpm < 1) w.rpm = 1;
        if (w.mode != FIRE_MELEE && w.mag == 0) w.mag = 8;
        gWeapons[gGunCount] = w;
        snprintf(gGunIds[gGunCount], 32, "%s", idBuf);
        gGunCount++;
    }
    if (gGunCount == 0) {
        printf("data: WARNING guns.json had no entries - using built-in defaults\n");
        for (int i = 0; i < DEFAULT_GUN_COUNT; i++)
            ApplyGunDefaults(&gWeapons[i], gGunIds[i], &GUN_DEFAULTS[i]);
        gGunCount = DEFAULT_GUN_COUNT;
    }
}

// ---------------------------------------------------------------------------
// Perks
// ---------------------------------------------------------------------------
PerkDef gPerks[PERK_SLOTS_MAX];
int     gPerkCount = 0;
char    gPerkIds[PERK_SLOTS_MAX][32];

int PerkIndex(const char *id) {
    if (!id) return -1;
    for (int i = 0; i < gPerkCount; i++)
        if (!strcmp(gPerkIds[i], id)) return i;
    return -1;
}

static void PerkDefaults(void) {
    for (int i = 0; i < DEFAULT_PERK_COUNT; i++) {
        snprintf(gPerkIds[i], 32, "%s", PERK_DEFAULTS[i].id);
        gPerks[i] = (PerkDef){ PERK_DEFAULTS[i].name, PERK_DEFAULTS[i].desc,
                               PERK_DEFAULTS[i].price, PERK_DEFAULTS[i].color };
    }
    gPerkCount = DEFAULT_PERK_COUNT;
}

static void LoadPerks(void) {
    gPerkCount = 0;
    JsonNode *root = LoadJson("perks.json", "perks.json");
    if (!root || root->type != JSON_OBJECT) {
        if (root) printf("data: WARNING perks.json is not an object - using built-in defaults\n");
        PerkDefaults();
        return;
    }
    for (int i = 0; i < root->size && gPerkCount < PERK_SLOTS_MAX; i++) {
        JsonNode *e = root->fields[i].val;
        const char *id = root->fields[i].key;
        if (!e || e->type != JSON_OBJECT) continue;
        // default with same id when present (keeps engine-side perk hooks
        // - revive/regen/... - attached to the right entry)
        const char *name = id, *desc = "";
        int price = 1000;
        Color color = { 200, 200, 200, 255 };
        for (int d = 0; d < DEFAULT_PERK_COUNT; d++)
            if (!strcmp(PERK_DEFAULTS[d].id, id)) {
                name = PERK_DEFAULTS[d].name; desc = PERK_DEFAULTS[d].desc;
                price = PERK_DEFAULTS[d].price; color = PERK_DEFAULTS[d].color;
                break;
            }
        name  = JsonStr(e, "name", name);
        desc  = JsonStr(e, "desc", desc);
        price = JsonInt(e, "price", price);
        JsonColor(e, "color", &color);
        gPerks[gPerkCount] = (PerkDef){ name, desc, price, color };
        snprintf(gPerkIds[gPerkCount], 32, "%s", id);
        gPerkCount++;
    }
    if (gPerkCount == 0) PerkDefaults();
}

// engine-side perk hooks resolve by id once at load (fallback 0 = first perk,
// which keeps old saves valid when perks.json is edited)
int PerkReviveIndex(void) { int i = PerkIndex("revive"); return i >= 0 ? i : 0; }
int PerkRegenIndex(void)  { int i = PerkIndex("regen");  return i >= 0 ? i : 0; }
int PerkSwiftIndex(void)  { int i = PerkIndex("swift");  return i >= 0 ? i : 0; }
int PerkRapidIndex(void)  { int i = PerkIndex("rapid");  return i >= 0 ? i : 0; }
int PerkStaminIndex(void) { int i = PerkIndex("stamin"); return i >= 0 ? i : 0; }
int PerkJuggIndex(void)   { int i = PerkIndex("jugg");   return i >= 0 ? i : 0; }

// ---------------------------------------------------------------------------
// Skins
// ---------------------------------------------------------------------------
SkinDef gSkins[SKIN_SLOTS_MAX];
int     gSkinCount = 0;
int     gSkinSel   = 0;     // chosen skin index (persisted in settings)

int SkinIndex(const char *id) {
    if (!id) return -1;
    for (int i = 0; i < gSkinCount; i++)
        if (!strcmp(gSkins[i].id, id)) return i;
    return -1;
}

const SkinDef *SkinCurrent(void) {
    if (gSkinCount == 0) return &SKIN_DEFAULTS[0];
    return &gSkins[CLAMP(gSkinSel, 0, gSkinCount - 1)];
}

void SkinApply(const SkinDef *s, Color *body, Color *dark, Color *accent) {
    if (!s) return;
    // WHITE (255,255,255) = "leave the model's own MTL colors"
    *body   = s->body;
    *dark   = s->dark;
    *accent = s->accent;
}

static void SkinDefaults(void) {
    for (int i = 0; i < DEFAULT_SKIN_COUNT; i++) gSkins[i] = SKIN_DEFAULTS[i];
    gSkinCount = DEFAULT_SKIN_COUNT;
}

static void LoadSkins(void) {
    gSkinCount = 0;
    JsonNode *root = LoadJson("skins.json", "skins.json");
    if (!root || root->type != JSON_OBJECT) {
        if (root) printf("data: WARNING skins.json is not an object - using built-in defaults\n");
        SkinDefaults();
        return;
    }
    for (int i = 0; i < root->size && gSkinCount < SKIN_SLOTS_MAX; i++) {
        JsonNode *e = root->fields[i].val;
        const char *id = root->fields[i].key;
        if (!e || e->type != JSON_OBJECT) continue;
        SkinDef s = { "", "", {255,255,255,255}, {255,255,255,255}, {255,255,255,255} };
        snprintf(s.id, sizeof s.id, "%s", id);
        snprintf(s.name, sizeof s.name, "%s", JsonStr(e, "name", id));
        // "extends": copy another skin, then override the colors given here
        const char *base = JsonStr(e, "extends", NULL);
        if (base) {
            int src = SkinIndex(base);
            if (src >= 0) s = gSkins[src];
            snprintf(s.id, sizeof s.id, "%s", id);
            snprintf(s.name, sizeof s.name, "%s", JsonStr(e, "name", gSkins[src >= 0 ? src : 0].name));
        }
        JsonColor(e, "body",   &s.body);
        JsonColor(e, "dark",   &s.dark);
        JsonColor(e, "accent", &s.accent);
        gSkins[gSkinCount++] = s;
    }
    if (gSkinCount == 0) SkinDefaults();
    if (gSkinSel >= gSkinCount) gSkinSel = 0;
}

// ---------------------------------------------------------------------------
// Map registry (assets/maps.json -> assets/maps/<file>.json per map)
// ---------------------------------------------------------------------------
MapEntry gMaps[MAP_SLOTS_MAX];
int      gMapCount = 0;

static const struct { const char *file, *name, *desc; int diff; } MAP_DEFAULTS[] = {
    { "theater",  "The Grand Theater", "The curtain rose on the end of the world. Hold the stage.", 2 },
    { "bunker",   "Bunker 44",         "Holding position in the ruins of the final offensive.",     3 },
    { "graveyard","Graveyard Hollow",  "The dead were already here. Now they're awake.",            3 },
    { "diearise", "Die Arise",         "The rooftops of a dead city. Mind the gap.",                4 },
    { "deadcon5", "Dead Con 5",        "The Oval Office first. The FBI's secrets are upstairs.",    4 },
};
#define DEFAULT_MAP_COUNT ((int)(sizeof MAP_DEFAULTS / sizeof MAP_DEFAULTS[0]))

static void MapListDefaults(void) {
    for (int i = 0; i < DEFAULT_MAP_COUNT; i++) {
        snprintf(gMaps[i].file, sizeof gMaps[i].file, "maps/%s.json", MAP_DEFAULTS[i].file);
        snprintf(gMaps[i].name, sizeof gMaps[i].name, "%s", MAP_DEFAULTS[i].name);
        snprintf(gMaps[i].desc, sizeof gMaps[i].desc, "%s", MAP_DEFAULTS[i].desc);
        gMaps[i].difficulty = MAP_DEFAULTS[i].diff;
    }
    gMapCount = DEFAULT_MAP_COUNT;
}

static void LoadMapList(void) {
    gMapCount = 0;
    JsonNode *root = LoadJson("maps.json", "maps.json (registry)");
    if (!root || root->type != JSON_ARRAY) {
        if (root) printf("data: WARNING maps.json is not an array - using built-in registry\n");
        MapListDefaults();
        return;
    }
    for (int i = 0; i < JsonLen(root) && gMapCount < MAP_SLOTS_MAX; i++) {
        JsonNode *e = JsonAt(root, i);
        if (!e || e->type != JSON_OBJECT) continue;
        MapEntry *m = &gMaps[gMapCount];
        snprintf(m->file, sizeof m->file, "maps/%s.json", JsonStr(e, "file", "theater"));
        snprintf(m->name, sizeof m->name, "%s", JsonStr(e, "name", JsonStr(e, "file", "Map")));
        snprintf(m->desc, sizeof m->desc, "%s", JsonStr(e, "desc", ""));
        m->difficulty = CLAMP(JsonInt(e, "difficulty", 3), 1, 5);
        gMapCount++;
    }
    if (gMapCount == 0) MapListDefaults();
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
bool DataLoadAll(void) {
    JsonInit();
    LoadGameplay();
    LoadGuns();
    LoadPerks();
    LoadSkins();
    LoadMapList();
    return true;
}

int DataGunCount(void)  { return gGunCount; }
int DataPerkCount(void) { return gPerkCount; }
int DataSkinCount(void) { return gSkinCount; }
int DataMapCount(void)  { return gMapCount; }
