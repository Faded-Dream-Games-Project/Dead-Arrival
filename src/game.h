// ============================================================================
//  DEAD ARRIVAL - game.h
//  Single-header shared definitions for the whole game.
//  A round-based zombies FPS in pure C on raylib.
// ============================================================================
#ifndef GAME_H
#define GAME_H

#include "raylib.h"
#include "raymath.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// ---------------------------------------------------------------------------
// Version / identity
// ---------------------------------------------------------------------------
#define GAME_TITLE        "Dead Arrival"
#define GAME_VERSION      "dev"
#define ORG_NAME          "Faded Dream Games Project"

// ---------------------------------------------------------------------------
// Tunable gameplay constants
// ---------------------------------------------------------------------------
#define PLAYER_HEIGHT     1.70f
#define PLAYER_EYE        1.62f
#define PLAYER_RADIUS     0.34f
#define PLAYER_WALK       3.60f
#define PLAYER_SPRINT     5.60f
#define PLAYER_CRAWL      1.20f
#define PLAYER_JUMP       4.80f
#define GRAVITY           13.0f
#define STEP_HEIGHT       0.55f

#define PLAYER_MAX_HP     100.0f
#define ZOMBIE_HIT_DMG    50.0f        // 2 hits before going down (4 with Jugg)
#define BLEEDOUT_TIME     30.0f
#define REVIVE_TIME       5.0f
#define REGEN_DELAY       10.0f        // Regen Soda: 10 s after last hit
#define REGEN_RATE        25.0f        // hp per second once regen kicks in

#define POINTS_START      500
#define POINTS_PER_HIT    10
#define POINTS_PER_KILL   50
#define BOX_PRICE         950
#define PAP_PRICE         5000         // Pack-a-Punch upgrade cost

#define MAX_BOXES         640
#define MAX_DOORS         16
#define MAX_SPAWNS        56
#define MAX_WALLBUYS      12
#define MAX_PERKSTATIONS  6
#define MAX_ZIPLINES      4
#define MAX_JUMPPADS      10
#define MAX_KILLZONES     8

#define MAX_ZOMBIES       48
#define MAX_PARTICLES     512
#define WEAPON_SLOTS      3             // primary, secondary, knife
#define SLOT_KNIFE        2

#define NUM_GUNS          20            // guns, knife NOT counted (user spec)
#define GUN_COUNT_TOTAL   (NUM_GUNS+1)  // + knife

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
#define CLAMP(v,a,b)      ((v)<(a)?(a):((v)>(b)?(b):(v)))
#define LERP(a,b,t)       ((a)+((b)-(a))*(t))
#define ARRAY_LEN(a)      (sizeof(a)/sizeof((a)[0]))

typedef struct AABB {
    Vector3 min, max;
} AABB;

float   AABB_RayHit(AABB b, Vector3 origin, Vector3 dir, float maxDist); // t or -1
bool    AABB_Overlap(AABB a, AABB b);
AABB    AABB_Make(Vector3 min, Vector3 max);

// xorshift rng (deterministic for selftest)
void    SeedRng(uint32_t seed);
uint32_t RngNext(void);
float   RngRange(float a, float b);
int     RngInt(int n);                  // [0, n)

// ---------------------------------------------------------------------------
// Weapons
// ---------------------------------------------------------------------------
typedef enum {
    FIRE_SEMI = 0,
    FIRE_AUTO,
    FIRE_BURST,      // 3-round burst
    FIRE_BOLT,       // bolt-action sniper
    FIRE_PUMP,       // pump shotgun
    FIRE_MELEE
} FireMode;

typedef struct WeaponDef {
    const char *name;
    FireMode    mode;
    int         damage;      // per bullet
    int         pellets;     // bullets per trigger pull (shotguns)
    int         mag;         // magazine size
    int         reserve;     // reserve ammo when bought
    int         rpm;         // rounds per minute
    float       reloadTime;  // seconds
    float       spread;      // hip-fire spread (rad)
    float       adsSpread;   // aimed spread (rad)
    float       recoil;      // view kick strength
    int         wallPrice;   // >0: available as wall buy for this price
    int         boxWeight;   // relative weight in mystery box (0 = not in box)
    int         shape;       // viewmodel variant (GUNSHAPE_*): extra parts drawn
    // low-poly viewmodel shape (meters, gun-space: +Z forward)
    float       bodyLen, bodyW, bodyH;   // receiver
    float       barrelLen, barrelR;      // barrel
    float       magW, magH;              // magazine drop below receiver
    float       stockLen;                // stock behind grip
    float       sightH;                  // front sight post height
    float       adsFov;                  // FOV while aiming (0 = default 55)
    Color       accent;                  // wood / polymer accent color
} WeaponDef;

// viewmodel shape variants (guns share dims; these add distinguishing parts)
enum {
    GUNSHAPE_BASIC = 0,     // receiver+barrel+grip+stock (pistols, most rifles)
    GUNSHAPE_REVOLVER,      // cylinder + longer curved barrel
    GUNSHAPE_BANANA,        // curved banana magazine (AK family)
    GUNSHAPE_DRUM,          // drum magazine (RPK, HK21 - tied to their dims)
    GUNSHAPE_WINGS,         // barrel heat-shield wings (Spectre)
    GUNSHAPE_FOLDSTOCK,     // folding stock strut (MP5K, PM63)
    GUNSHAPE_DOUBLEBARREL,  // two side-by-side barrels (Olympia)
    GUNSHAPE_TUBEMAG,       // under-barrel tube magazine (Stakeout)
    GUNSHAPE_UNDERSLUNG,    // chunky underbarrel pump/grenade tube (China Lake)
    GUNSHAPE_CARRYHANDLE,   // carry handle + comp sight (M16, FAMAS bullpup)
    GUNSHAPE_BULLPUP,       // mag behind the grip (FAMAS)
    GUNSHAPE_SNIPERBOLT,    // bolt handle + scope rings (L96A1)
};

extern const WeaponDef WEAPONS[GUN_COUNT_TOTAL];

enum { W_M1911 = 0, W_PYTHON, W_CZ75, W_MP5K, W_AK74U, W_SPECTRE, W_PM63,
       W_MPL, W_OLYMPIA, W_STAKEOUT, W_HS10, W_CHINALAKE, W_M16, W_ENFIELD,
       W_COMMANDO, W_GALIL, W_FAMAS, W_RPK, W_HK21, W_L96A1, W_KNIFE };

typedef struct WeaponSlot {
    int widx;            // index into WEAPONS, -1 = empty
    int magAmmo;
    int reserveAmmo;
    bool upgraded;       // Pack-a-Punched
} WeaponSlot;

// ---------------------------------------------------------------------------
// Perks
// ---------------------------------------------------------------------------
typedef enum {
    PERK_REVIVE = 0,     // Self Revive - get back up when downed (consumed)
    PERK_REGEN,          // Regen Soda - auto regen 10 s after last hit
    PERK_SWIFT,          // Swift Soda - 40% faster reloads
    PERK_RAPID,          // Rapid Cola - 25% faster fire rate
    PERK_STAMIN,         // Stamin-Up - move & sprint faster
    PERK_JUGG,           // Juggernaut Soda - survive 4 hits instead of 2
    PERK_COUNT
} PerkType;

typedef struct { const char *name; const char *desc; int price; Color color; } PerkDef;
extern const PerkDef PERKS[PERK_COUNT];

// ---------------------------------------------------------------------------
// Textures (procedurally generated PNGs in textures/, with in-code fallback)
// ---------------------------------------------------------------------------
enum {
    TEX_BRICK = 0, TEX_WOOD, TEX_PLANK, TEX_STONE, TEX_MARBLE, TEX_VELVET,
    TEX_CURTAIN, TEX_CONCRETE, TEX_METAL, TEX_RUST, TEX_SANDBAG, TEX_GRASS,
    TEX_DIRT, TEX_GRAVE, TEX_GLASS, TEX_ROOF, TEX_STUCCO, TEX_TILE,
    TEX_ASPHALT, TEX_ZOMBIE, TEX_BOX, TEX_PARTICLE, TEX_FLASH,
    TEX_BLOODVIGN, TEX_LOGO, TEX_ICON, TEX_COUNT
};

// ---------------------------------------------------------------------------
// Sounds
// ---------------------------------------------------------------------------
enum {
    SND_SHOT_LIGHT = 0, SND_SHOT_HEAVY, SND_SHOT_SHOTGUN, SND_SHOT_SNIPER,
    SND_KNIFE, SND_HIT, SND_SQUISH, SND_GROAN1, SND_GROAN2, SND_GROAN3,
    SND_ZATTACK, SND_HURT, SND_DOWN, SND_DOOR, SND_BUY, SND_DENIED,
    SND_BOX_OPEN, SND_BOX_SPIN, SND_BOX_WIN, SND_ROUND_START, SND_ROUND_END,
    SND_RELOAD, SND_DRY, SND_STEP1, SND_STEP2, SND_HEARTBEAT, SND_ZRISE,
    SND_JUMP_PAD, SND_ZIPLINE, SND_PERK, SND_COUNT
};

enum { MUS_AMBIENT = 0, MUS_COUNT };

// ---------------------------------------------------------------------------
// Maps
// ---------------------------------------------------------------------------
typedef enum { MAP_THEATER = 0, MAP_BUNKER, MAP_GRAVEYARD, MAP_DIE_ARISE, MAP_DEAD_CON_5, MAP_COUNT } MapId;

typedef struct WorldBox {
    AABB    aabb;
    int     tex;          // TEX_*
    bool    noCollide;    // decor only
    int     doorId;       // 0 = permanent, else door index+1 (removed when opened)
} WorldBox;

typedef struct DoorDef {
    const char *name;
    int       price;
    Vector3    center;      // interaction point
} DoorDef;

typedef struct SpawnPoint {
    Vector3 pos;
    int     doorId;         // -1 = active from round 1, else (door idx)
    bool    grave;          // rise from the ground (graveyard graves)
} SpawnPoint;

typedef struct WallBuy {
    int      widx;
    Vector3  pos;
    float    yaw;           // facing
} WallBuy;

typedef struct PerkStation { PerkType type; Vector3 pos; float yaw; } PerkStation;
typedef struct JumpPad     { Vector3 pos; float radius; Vector3 impulse; } JumpPad;
typedef struct Zipline     { Vector3 a, b; } Zipline;

typedef struct MapDef {
    const char *name;
    const char *desc;
    const char *filename;   // asset-free id
    int         difficulty; // 1..5
    WorldBox    boxes[MAX_BOXES]; int boxCount;
    DoorDef     doors[MAX_DOORS]; int doorCount;
    bool        doorOpen[MAX_DOORS];
    SpawnPoint  spawns[MAX_SPAWNS]; int spawnCount;
    WallBuy     wallbuys[MAX_WALLBUYS]; int wallbuyCount;
    PerkStation perks[MAX_PERKSTATIONS]; int perkCount;
    Vector3     boxPos;               // mystery box
    Vector3     powerPos;             // power switch (0,0,0 = none on this map)
    bool        powerAtStart;         // power already on (map design)
    Vector3     papPos;               // Pack-a-Punch machine (0,0,0 = none)
    JumpPad     pads[MAX_JUMPPADS];   int padCount;
    Zipline     ziplines[MAX_ZIPLINES]; int ziplineCount;
    AABB        killzones[MAX_KILLZONES]; int killzoneCount;
    Color       skyTop, skyHorizon, fogColor;
    float       fogDensity;           // exp2 density
    Vector3     sunDir;               // normalized
    Color       sunColor, ambient;
    Vector3     fillDir;              // hemisphere/fill light direction
    Color       fillColor;            // fill light color
    Vector3     playerStart;
    float       playerYaw;
} MapDef;

// ---------------------------------------------------------------------------
// Zombies
// ---------------------------------------------------------------------------
typedef enum { ZS_RISE = 0, ZS_CHASE, ZS_ATTACK, ZS_DEAD } ZState;

typedef struct Zombie {
    bool     alive;
    ZState   state;
    Vector3  pos;            // feet position
    float    yaw;
    float    hp, maxHp;
    float    speed;
    float    animPhase;
    float    stateTime;      // time in current state
    float    attackCd;
    float    hitFlash;       // >0 = red flash
    int      variant;        // skin/cloth tint
    Vector3  path[48]; int pathLen, pathIdx;
    float    pathTimer;
    float    groundY;
    bool     runner;
    // stuck detection: last distance to player + timer since it last shrank
    float    lastDist;
    float    stuckTimer;
} Zombie;

// ---------------------------------------------------------------------------
// Rebindable key actions (settings)
// ---------------------------------------------------------------------------
typedef enum {
    ACT_FWD = 0, ACT_BACK, ACT_LEFT, ACT_RIGHT, ACT_JUMP, ACT_SPRINT, ACT_CROUCH,
    ACT_USE, ACT_RELOAD, ACT_KNIFE, ACT_WEP1, ACT_WEP2, ACT_WEP3, ACT_SWAP,
    ACT_SHAKE,                       // hard mode: mash to shake the flashlight
    ACT_COUNT
} KeyAction;

// ---------------------------------------------------------------------------
// Particles
// ---------------------------------------------------------------------------
typedef struct Particle {
    bool    alive;
    Vector3 pos, vel;
    Color   color;
    float   size, life, maxLife;
    bool    grav;
} Particle;

// ---------------------------------------------------------------------------
// Player
// ---------------------------------------------------------------------------
typedef struct Player {
    Vector3     pos;          // feet
    float       yaw, pitch;
    Vector3     vel;
    bool        grounded;
    float       hp;
    float       lastHitTime;  // for regen perk
    int         points;
    bool        perks[PERK_COUNT];
    WeaponSlot  weapons[WEAPON_SLOTS];
    int         curSlot;
    // weapon runtime
    float       fireTimer;    // seconds until next shot allowed
    float       reloadTimer;  // >0 = reloading
    int         burstShots;
    float       knifeTimer;   // >0 = swing anim
    float       knifeHitDone;
    float       raiseTimer;   // weapon raise after switch/sprint
    float       sprintTime;
    float       bobPhase;
    float       recoilKick;
    float       adsBlend;     // 0 hip .. 1 ads
    // downed
    bool        downed;
    float       downTimer;    // bleedout or revive countdown
    int         reviveUsedRound;
    // stats
    int         kills, shots, hits, earned, doorsOpened, revives;
    float       stepTimer;
} Player;

// ---------------------------------------------------------------------------
// Game states
// ---------------------------------------------------------------------------
typedef enum {
    STATE_TITLE = 0, STATE_MAPSELECT, STATE_SETTINGS, STATE_PLAYING,
    STATE_PAUSED, STATE_GAMEOVER
} GameState;

typedef enum { OVERLAY_NONE = 0, OVERLAY_COMINGSOON } OverlayId;

typedef struct Game {
    GameState   state;
    OverlayId   overlay;
    int         mapId;
    MapDef      map;
    Player      player;
    Zombie      zombies[MAX_ZOMBIES];
    Particle    particles[MAX_PARTICLES];
    int         round;
    float       roundIntermission;   // >0 = countdown to next round
    int         roundKills, roundQuota, roundSpawned;
    float       spawnTimer;
    bool        roundBannerActive;
    float       roundBannerTime;
    // mystery box runtime
    bool        boxInUse;
    float       boxSpinTimer;
    int         boxResultWeapon;     // -1 while spinning
    int         boxUses;
    // power + Pack-a-Punch
    bool        powerOn;             // power switch flipped this game
    float       papAnim;             // Pack-a-Punch cycling animation timer
    // view/hud fx
    float       hitMarker;
    float       damageFlash;
    float       muzzleFlash;
    Vector3     muzzlePos;
    float       shakeTime, shakeAmp;
    float       fovCur;
    float       ziplineT;            // >0 while riding (index in ziplineRide)
    int         ziplineRide;
    // input helpers
    bool        ePressedThisFrame;
    float       menuBlur;
    int         bestRound[MAP_COUNT];
    // settings (persisted)
    float       sens;                  // 0.2 .. 10.0 (slider maps 0..1 -> exponential)
    float       volMaster, volMusic, volSfx;
    float       brightness;            // 0.5 .. 1.5
    float       fovSetting;            // 60 .. 120
    bool        invertY;
    bool        fullscreen;
    bool        vsync;
    bool        hardMode;              // play-menu toggle (not persisted)
    bool        shakeByMouse;          // hard mode: mouse-move vs key-mash shake
    int         keys[ACT_COUNT];       // KeyboardKey per action
    // torch (hard mode flashlight)
    float       torchCharge;           // 0..1, fed by shaking, drains constantly
    float       torchShakeAnim;        // visual jolt
    // runtime flags
    bool        windowShouldClose;
    double      time;
    double      dt;
} Game;

extern Game g;

// ---------------------------------------------------------------------------
// Module: gfx
// ---------------------------------------------------------------------------
void    GfxInit(void);
void    GfxShutdown(void);
Shader* GfxWorldShader(void);
void    GfxBegin(Camera3D cam);            // begin 3D w/ world shader consts
void    GfxSetScene(Vector3 sunDir, Color sunColor, Color ambient, Vector3 fillDir, Color fillColor, Color fogColor, float fogDensity);
void    GfxSetTorch(Vector3 pos, Vector3 dir, float strength);   // hard-mode flashlight spot
void    GfxSetFlash(Vector3 pos, float strength);   // muzzle flash point light
void    GfxDrawSky(Camera3D cam, float playerPitchDeg);
void    GfxDrawStatic(void);               // draw merged static geometry
void    GfxDrawBox(AABB aabb, int tex, Color tint); // world box (dynamic)
void    GfxDrawCubePart(Vector3 pos, Vector3 size, Vector3 rotDeg, Color tint, Texture2D tex);
void    GfxDrawCubeBasis(Vector3 pos, Vector3 right, Vector3 up, Vector3 fwd,
                         float sx, float sy, float sz, Color tint, Texture2D tex);
void    GfxInitMaterials(void);
void    GfxEnd(void);
// mesh-builder (static world geometry accumulation):
void    Gfx_MB_Box(int tex, AABB box, float uvScale);
void    Gfx_MB_Reset(void);
void    Gfx_MB_Build(void);

// ---------------------------------------------------------------------------
// Module: assets
// ---------------------------------------------------------------------------
void    AssetsLoad(void);          // textures + sounds (fallbacks if missing)
void    AssetsUnload(void);
Texture2D AssetsTex(int id);
bool    AssetsHasTex(int id);
void    SfxPlay(int id, float pitch, float vol);
void    SfxSetVolume(float master, float sfx);
void    SfxSetMusicVolume(float v);
void    MusicStart(int mapId);
void    MusicStop(void);
void    MusicUpdate(void);

// ---------------------------------------------------------------------------
// Module: maps
// ---------------------------------------------------------------------------
void    MapLoad(MapId id);
void    MapUnload(void);
bool    MapValidate(MapId id, char *err, int errSize);  // selftest helper
bool    CollideMove(Vector3 *pos, Vector3 *vel, float radius, float height, float dt, float *groundYOut);
float   GroundHeightAt(float x, float z, float fromY);
bool    NavFind(Vector3 from, Vector3 to, Vector3 *path, int *pathLen, int maxLen);
bool    NavWalkableLine(Vector3 a, Vector3 b);
void    NavRebuild(void);
bool    DoorTryOpen(int doorIdx);               // checks points, opens
bool    InKillzone(Vector3 pos);
bool    MapRayHitWalls(Vector3 origin, Vector3 dir, float maxDist, float *tOut);
void    MapDrawInteractables(void);   // perk machines, wall buys, ziplines, pads

// ---------------------------------------------------------------------------
// Module: player
// ---------------------------------------------------------------------------
void    PlayerSpawn(void);
void    PlayerUpdate(void);
void    PlayerDraw(Camera3D cam);
void    PlayerDamage(float dmg);
void    PlayerDownedUpdate(void);
bool    PlayerHasPerk(PerkType p);
Vector3 Vec3Lerp4(Vector3 a, Vector3 b, float t);
// interaction scan for HUD (returns label of thing being looked at)
const char* PlayerInteractLabel(void);

// ---------------------------------------------------------------------------
// Module: weapons
// ---------------------------------------------------------------------------
void    WeaponDrawViewmodel(Camera3D cam);
void    WeaponsUpdate(void);
void    WeaponGive(int widx, bool fromBox);
void    WeaponRefillCurrent(void);
int     WeaponCurrentIndex(void);
float   WeaponSpreadCurrent(void);
const char* WeaponName(int widx);

// mystery box
void    BoxUpdate(void);
void    BoxDrawWorld(void);

// ---------------------------------------------------------------------------
// Module: zombies
// ---------------------------------------------------------------------------
void    ZombiesInitRound(void);
void    ZombiesUpdate(void);
void    ZombiesDraw(Camera3D cam);
void    ZombieDamage(Zombie *z, float dmg, Vector3 hitPos, bool melee);
int     ZombiesAliveCount(void);
float   ZombieHpForRound(int round);
float   ZombieSpeedForRound(int round);
int     ZombieQuotaForRound(int round);
bool    ZombieSpawnReachable(Vector3 from, Vector3 playerPos); // nav path exists
void    ZombieRespawnNear(Zombie *z, Vector3 playerPos);       // stuck rescue

// ---------------------------------------------------------------------------
// Module: hud
// ---------------------------------------------------------------------------
void    HudUpdate(void);
void    HudDraw(void);
void    HudInitFont(void);
void    HudDrawGameplay(void);
void    HudDrawTitle(void);
void    HudDrawMapSelect(void);
void    HudDrawSettings(void);
void    HudDrawPause(void);
void    HudDrawGameOver(void);
void    HudDrawOverlay(void);
bool    HudCapturingKey(void);       // settings key-rebind capture active
void    ParticlesSpawn(Vector3 pos, Vector3 vel, Color c, float size, float life, bool grav);
void    ParticlesUpdate(void);
void    ParticlesDraw(Camera3D cam);
void    ParticlesBurst(Vector3 pos, int n, Color c, float speed, float size, float life, bool grav);
float   HudTextWidth(const char *s, int size);

// ---------------------------------------------------------------------------
// Module: main / shared
// ---------------------------------------------------------------------------
void    GameStart(MapId id);
void    GameReset(void);
void    GameOverSequence(void);
void    ApplyHardModeScene(void);   // dark fog/ambient + torch after GfxSetScene
void    TorchUpdate(void);          // hard-mode flashlight shake/drain logic
void    AddPoints(int n);
void    SaveSettings(void);
void    LoadSettings(void);
void    SaveScores(void);
void    LoadScores(void);
bool    UiButton(Rectangle r, const char *text, int fontSize);
const char *ActName(KeyAction a);   // key-binding display names
bool    ActPressed(KeyAction a);    // rebindable "pressed this frame"
bool    ActDown(KeyAction a);       // rebindable "held down"

// ---------------------------------------------------------------------------
// Utility math on Vector3
// ---------------------------------------------------------------------------
static inline Vector3 Vec3(float x, float y, float z) { return (Vector3){x,y,z}; }
static inline Vector3 Vec3Add(Vector3 a, Vector3 b) { return (Vector3){a.x+b.x,a.y+b.y,a.z+b.z}; }
static inline Vector3 Vec3Sub(Vector3 a, Vector3 b) { return (Vector3){a.x-b.x,a.y-b.y,a.z-b.z}; }
static inline Vector3 Vec3Scale(Vector3 a, float s) { return (Vector3){a.x*s,a.y*s,a.z*s}; }
static inline float   Vec3Dot(Vector3 a, Vector3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
static inline float   Vec3Len(Vector3 a) { return sqrtf(Vec3Dot(a,a)); }
static inline Vector3 Vec3Norm(Vector3 a) { float l=Vec3Len(a); return l>1e-6f?(Vector3){a.x/l,a.y/l,a.z/l}:(Vector3){0,0,0}; }
static inline Vector3 Vec3Cross(Vector3 a, Vector3 b) {
    return (Vector3){ a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x };
}

#endif // GAME_H
