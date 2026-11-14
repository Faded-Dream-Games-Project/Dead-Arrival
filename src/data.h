// ============================================================================
//  DEAD ARRIVAL - data.h
//  Data-driven content: guns, perks, skins, gameplay config, map registry.
//  All content lives in assets/*.json; the structs below are the runtime
//  representation the engine consumes. Counts are dynamic (bounded by the
//  slot maximums) - indices are NOT compile-time constants anymore; use
//  GunIndex("id") / PerkIndex("id") to find things.
// ============================================================================
#ifndef DATA_H
#define DATA_H

#include "raylib.h"
#include <stdbool.h>

// ---------------------------------------------------------------------------
// Fire modes (shared by guns.json entries and the engine)
// ---------------------------------------------------------------------------
typedef enum {
    FIRE_SEMI = 0,
    FIRE_AUTO,
    FIRE_BURST,      // 3-round burst
    FIRE_BOLT,       // bolt-action sniper
    FIRE_PUMP,       // pump shotgun
    FIRE_MELEE
} FireMode;

#ifndef CLAMP
#define CLAMP(v,a,b)      ((v)<(a)?(a):((v)>(b)?(b):(v)))
#endif

// ---------------------------------------------------------------------------
// Guns
// ---------------------------------------------------------------------------
#define NUM_GUN_SLOTS_MAX 64

typedef struct WeaponDef {
    const char *name;
    FireMode    mode;
    int         damage;        // per bullet
    int         pellets;       // bullets per trigger pull (shotguns)
    int         mag;           // magazine size
    int         reserve;       // reserve ammo when bought
    int         rpm;           // rounds per minute
    float       reloadTime;    // seconds
    float       spread;        // hip-fire spread (rad)
    float       adsSpread;     // aimed spread (rad)
    float       recoil;        // view kick strength
    int         wallPrice;     // >0: available as wall buy for this price
    int         boxWeight;     // relative weight in mystery box (0 = not in box)
    const char *modelId;       // assets/guns/<modelId>.obj viewmodel
    float       adsFov;        // FOV while aiming (0 = default 55)
    Color       accent;        // default accent (skin overrides)
    float       papDamageMul;  // Pack-a-Punch damage multiplier
    int         papMagBonus;   // Pack-a-Punch extra mag capacity
    float       muzzleZ;       // muzzle point, gun-space meters (+Z forward)
} WeaponDef;

extern WeaponDef gWeapons[NUM_GUN_SLOTS_MAX];
extern int       gGunCount;
extern char      gGunIds[NUM_GUN_SLOTS_MAX][32];

int GunIndex(const char *id);
int GunKnifeIndex(void);

// ---------------------------------------------------------------------------
// Perks
// ---------------------------------------------------------------------------
#define PERK_SLOTS_MAX 12

typedef struct { const char *name; const char *desc; int price; Color color; } PerkDef;

extern PerkDef gPerks[PERK_SLOTS_MAX];
extern int     gPerkCount;
extern char    gPerkIds[PERK_SLOTS_MAX][32];

int PerkIndex(const char *id);
int PerkReviveIndex(void);
int PerkRegenIndex(void);
int PerkSwiftIndex(void);
int PerkRapidIndex(void);
int PerkStaminIndex(void);
int PerkJuggIndex(void);

// ---------------------------------------------------------------------------
// Skins
// ---------------------------------------------------------------------------
#define SKIN_SLOTS_MAX 24

typedef struct SkinDef {
    char  id[32];
    char  name[32];
    Color body;      // receiver tint (WHITE = keep model color)
    Color dark;      // barrel / dark parts tint
    Color accent;    // mag / stock / grip tint
} SkinDef;

extern SkinDef gSkins[SKIN_SLOTS_MAX];
extern int     gSkinCount;
extern int     gSkinSel;

int SkinIndex(const char *id);
const SkinDef *SkinCurrent(void);
void SkinApply(const SkinDef *s, Color *body, Color *dark, Color *accent);

// ---------------------------------------------------------------------------
// Gameplay config (gameplay.json)
// ---------------------------------------------------------------------------
typedef struct GameplayConfig {
    int   pointsStart, pointsPerHit, pointsPerKill;
    int   boxPrice, papPrice;
    float papDamageMul;
    float zombieHitDmg;
    float regenDelay, regenRate;
    // zombie health curve
    float hpBase;            // round 1
    float hpRound2;          // round 2
    float hpLinearPerRound;  // rounds 3..linearUntil
    int   hpLinearUntil;
    float hpGrowth;          // compounding per round after linearUntil
    // zombie speed
    float speedBase, speedPerRound, speedMin, speedMax;
    int   runnerChanceBase, runnerFromRound;
    // round quota
    float quotaBase, quotaGrowth;
    int   quotaCap;
    // hellhounds
    int   dogEvery, dogFirstRound;
    float dogHp, dogSpeed;
    int   dogQuotaMin, dogQuotaMax;
    // max ammo drop
    int   maxAmmoChance;     // percent chance per zombie kill
    float maxAmmoLife;       // seconds before despawn
} GameplayConfig;

extern GameplayConfig gGameplay;

// ---------------------------------------------------------------------------
// Map registry (assets/maps.json -> assets/maps/<file>.json per map)
// ---------------------------------------------------------------------------
#define MAP_SLOTS_MAX 12

typedef struct MapEntry {
    char file[48];      // relative path under assets/
    char name[48];
    char desc[96];
    int  difficulty;
} MapEntry;

extern MapEntry gMaps[MAP_SLOTS_MAX];
extern int      gMapCount;

// ---------------------------------------------------------------------------
// Built-in default gun table (fallback when guns.json is missing)
// ---------------------------------------------------------------------------
typedef struct GunDefDefault {
    const char *id, *name, *mode;
    int damage, pellets, mag, rsv, rpm;
    float rel, sprd, adsSpr, rec;
    int wall, boxW;
    const char *model;
    Color accent;
} GunDefDefault;

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------
void DataSetDir(const char *dir);
const char *DataDir(void);
bool DataLoadAll(void);
int  DataGunCount(void);
int  DataPerkCount(void);
int  DataSkinCount(void);
int  DataMapCount(void);

#endif
