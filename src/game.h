// ============================================================================
//  Dead Arrival - a round-based survival FPS inspired by classic Nazi Zombies
//  Central header: shared types, constants and module interfaces.
//
//  Everything data-driven: maps, weapons, textures and sounds are loaded at
//  runtime from ./assets (see docs/MAP_FORMAT.md and docs/MODDING.md).
//
//  Engine: raylib 6.2 (https://www.raylib.com)
// ============================================================================
#ifndef GAME_H
#define GAME_H

#include "raylib.h"
#include "raymath.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// ---------------------------------------------------------------------------
// Tiny INI reader (config.c) - shared by weapons.cfg and map loader
// ---------------------------------------------------------------------------
int  cfg_read(FILE *f, char *section, size_t sec_size,
              char *key, size_t key_size, char *value, size_t val_size);
int  cfg_floats(const char *v, float *out, int max);
bool cfg_bool(const char *v);

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
#define APP_NAME             "Dead Arrival"
// no version numbers on the dev branch: only GitHub Releases carry versions
#define APP_VERSION          "dev"
#define APP_TAGLINE          "A round-based survival FPS"
#define ASSETS_DIR           "assets"

#define MAX_MAP_BOXES        512
#define MAX_BARRIERS         32
#define MAX_ROOMS            16
#define MAX_DOORS            8
#define MAX_PERKS            8
#define MAX_GRENADES         8
#define MAX_SPAWN_POINTS     64
#define MAX_WALLBUYS         32
#define MAX_MYSTERY_BOXES    4
#define MAX_ZOMBIES          48
#define MAX_PARTICLES        640
#define MAX_WEAPONS          40
#define MAX_NAME             64
#define MAX_TEXNAME          32
#define MAX_MAPS             32
#define MAX_SOUNDS           64

#define PLAYER_RADIUS        0.38f
#define PLAYER_HEIGHT        1.75f
#define PLAYER_EYE           1.62f
#define PLAYER_WALK_SPEED    3.6f
#define PLAYER_SPRINT_MULT   1.55f
#define PLAYER_JUMP_SPEED    5.2f
#define PLAYER_GRAVITY       14.0f
#define PLAYER_STEP_HEIGHT   0.55f     // auto step-up (curbs, duckboards, mounds)
#define PLAYER_MAX_HP        100
#define PLAYER_REGEN_DELAY   5.0f
#define PLAYER_REGEN_RATE    35.0f

#define ZOMBIE_RADIUS        0.40f
#define ZOMBIE_HEIGHT        1.72f
#define ZOMBIE_STEP_HEIGHT   0.62f
#define ZOMBIE_GRAVITY       14.0f
#define ZOMBIE_ATTACK_RANGE  1.35f
#define ZOMBIE_ATTACK_DMG    22
#define ZOMBIE_ATTACK_CD     0.85f
#define ZOMBIE_TEARS_PER_S   0.75f     // boards torn per second at a barrier
#define BARRIER_MAX_BOARDS   6
#define BARRIER_REPAIR_CD    0.55f
#define BARRIER_REPAIR_PTS   10

#define MYSTERY_BOX_COST     950       // classic price
#define MYSTERY_ROLL_TIME    4.0f

// ---- Pack-a-Punch ----
#define PAP_COST             5000
#define PAP_DAMAGE_MULT      2.5f
#define PAP_MAG_BONUS_DIV    2         // +50% mag size (mag + mag/2)

// ---- perks ----
#define PERK_COST_QUICK      500
#define PERK_COST_DOUBLE     2000
#define PERK_COST_SPEED      3000
#define PERK_COST_JUGG       2500
#define PERK_JUGG_BONUS      100       // extra max HP
#define PERK_REGEN_DELAY_MULT 0.5f     // quick revive: regen starts twice as fast
#define PERK_REGEN_RATE_MULT  2.0f
#define PERK_DOUBLE_RPM_MULT  1.3f     // double tap: +30% fire rate
#define PERK_SPEED_RELOAD_MULT 0.55f   // speed cola: ~half reload time

// ---- grenades ----
#define GRENADE_START        2
#define GRENADE_MAX          4
#define GRENADE_COST         450       // wall-buy refill
#define GRENADE_FUSE         2.2f
#define GRENADE_RADIUS       5.0f
#define GRENADE_DAMAGE       1200.0f   // at the center, falls off to the edge
#define GRENADE_THROW_CD     0.6f
#define MYSTERY_OFFER_TIME   10.0f

#define PTS_PER_HIT          10
#define PTS_PER_KILL         50

#define KNIFE_RANGE          1.9f
#define KNIFE_DAMAGE         60
#define KNIFE_CD             0.6f

#define ROUND_BREAK_TIME     8.0f
#define ROUND_SPAWN_BASE     6
#define ROUND_SPAWN_PER_RND  3
#define ROUND_MAX_ACTIVE     24
#define ROUND_MAX_TO_SPAWN   60

#define HIT_HEAD_MULT_FALLBACK 2.5f

#define CAMERA_MIN_PITCH     (-1.45f)
#define CAMERA_MAX_PITCH     ( 1.45f)

#define WALLBUY_ICON_SIZE    0.9f

// ---------------------------------------------------------------------------
// Utility helpers
// ---------------------------------------------------------------------------
float clampf(float v, float lo, float hi);
float lerpf(float a, float b, float t);
float frand_range(float lo, float hi);        // random float in [lo,hi]
float v3_dist_xz(Vector3 a, Vector3 b);       // horizontal distance
float angle_lerp(float a, float b, float t);  // shortest-path yaw lerp

// bounded string copy (always NUL-terminated, no format-truncation noise)
// note: src must be a regular NUL-terminated C string (all callers pass one)
static inline void str_copy(char *dst, size_t dstlen, const char *src)
{
    if (dstlen == 0) return;
    size_t n = strlen(src);
    if (n >= dstlen) n = dstlen - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

// ---------------------------------------------------------------------------
// Settings (settings.ini, portable next to the game)
// ---------------------------------------------------------------------------
typedef struct Settings {
    float sensitivity;      // 0.5 .. 4.0
    float fov;              // 60 .. 100
    bool  invert_y;
    float volume_master;    // 0..1
    float volume_sfx;       // 0..1
    bool  fullscreen;
    bool  show_fps;
} Settings;

void settings_load(Settings *s);
void settings_save(const Settings *s);

// ---------------------------------------------------------------------------
// Weapons (assets/weapons.cfg)
// ---------------------------------------------------------------------------
typedef enum {
    WCLASS_PISTOL = 0,
    WCLASS_SMG,
    WCLASS_RIFLE,
    WCLASS_AR,
    WCLASS_SHOTGUN,
    WCLASS_SNIPER,
    WCLASS_LMG,
    WCLASS_SPECIAL,
    WCLASS_COUNT
} WeaponClass;

typedef struct WeaponDef {
    char  id[MAX_NAME];
    char  name[48];
    char  sound[32];
    WeaponClass cls;
    float damage;           // per bullet / per pellet
    float head_mult;        // headshot multiplier
    int   mag;              // magazine size
    int   reserve;          // spare ammo on buy
    int   rpm;              // rounds per minute
    float reload_time;      // seconds
    float spread;           // base cone radians
    float ads_spread_mult;  // spread multiplier while aiming
    float recoil;           // camera kick degrees
    float ads_zoom;         // fov while aiming (0 = no ads zoom)
    int   pellets;          // bullets per shot (shotguns)
    int   penetrate;        // zombies a bullet can pass through (1 = one body)
    int   is_auto;          // hold to fire
    float range;            // bullet max travel
    int   wallbuyable;      // can appear as a wall buy
    int   price;            // default wall price / ammo refill is half
    int   box_weight;       // mystery box weight (0 = never from box)
    int   start_weapon;     // player starts with this
    Color tint;             // viewmodel tint
    float vm_len;           // viewmodel length (m)
    float vm_barrel;        // barrel thickness
} WeaponDef;

typedef struct WeaponSlot {
    int   def;              // index into weapon defs, -1 = empty
    int   ammo;             // in magazine
    int   reserve;          // spare
    float cooldown;         // time until next shot
    float reload_t;         // >0 while reloading
    float switch_t;         // raise/lower animation timer
    bool  upgraded;         // Pack-a-Punch
} WeaponSlot;

int  weapons_load(const char *path);                  // returns count
int  weapons_find(const char *id);                    // index or -1
const WeaponDef *weapons_get(int idx);
int  weapons_random_box_pick(void);                   // weighted mystery box roll
void weapons_log_summary(void);

// ---------------------------------------------------------------------------
// Map (assets/maps/*.map)
// ---------------------------------------------------------------------------
typedef enum {
    SPAWN_BARRIERS = 0,    // classic boarded windows (theater)
    SPAWN_GRAVES,          // rise from grave mounds (cemetery)
    SPAWN_DROP             // fall in from above (trenches)
} SpawnMode;

typedef struct MapBox {
    Vector3 pos;           // center of the box
    Vector3 size;          // full size (w, h, d)
    char    tex[MAX_TEXNAME];
} MapBox;

typedef struct Barrier {
    Vector3 pos;           // center of the opening
    Vector2 size;          // opening size (w, d) - height is fixed ~2.1
    int     face;          // 0=+Z 1=-Z 2=+X 3=-X (side zombies come from)
    int     boards;        // 0..BARRIER_MAX_BOARDS
    float   tear_accum;    // accumulated tear progress
    float   repair_cd;
    int     used;          // spawn bookkeeping
    int     room;          // room index this barrier belongs to (-1 = any)
} Barrier;

typedef struct SpawnPoint { Vector3 pos; int used; } SpawnPoint;

typedef struct WallBuy {
    Vector3 pos;           // where the chalk outline is drawn
    int     face;          // 0=+Z 1=-Z 2=+X 3=-X (normal of the wall)
    int     weapon;        // weapon def index
    int     price;         // overrides weapon default if > 0
} WallBuy;

typedef struct MysteryBoxLoc { Vector3 pos; } MysteryBoxLoc;

// named rectangular area of the map (rooms drive zombie spawning: barriers
// in the room the player currently stands in are preferred spawn points)
typedef struct Room {
    char    name[24];
    Vector3 min, max;
    bool    visited;          // set once the player has been inside
} Room;

// buyable door: blocks the opening until bought with F
typedef struct Door {
    Vector3 pos;             // center of the opening
    int     face;            // 0=+Z 1=-Z 2=+X 3=-X (outward normal)
    float   width;           // opening width along the wall
    int     cost;
    bool    open;
} Door;

// perk vending machines
typedef enum {
    PERK_QUICK = 0,          // faster health regen
    PERK_DOUBLE,             // faster fire rate
    PERK_SPEED,              // faster reloads
    PERK_JUGG,               // more health
    PERK_TYPE_COUNT
} PerkType;

typedef struct PerkMachine { Vector3 pos; PerkType type; } PerkMachine;

typedef struct Grenade {
    bool    active;
    Vector3 pos, vel;
    float   fuse;            // seconds until detonation
    bool    on_ground;
} Grenade;

#define WALLBUY_GRENADES   (-2)  // special wallbuy: grenade refill

typedef struct GameMap {
    char  file[MAX_NAME];
    char  name[48];
    char  desc[128];
    char  author[32];
    SpawnMode spawn_mode;
    Color ambient;         // texture tint (scene lighting mood)
    Color sky;             // clear color
    Vector3 player_spawn;
    float player_yaw;      // degrees

    MapBox boxes[MAX_MAP_BOXES];        int box_count;
    Barrier barriers[MAX_BARRIERS];     int barrier_count;
    SpawnPoint spawns[MAX_SPAWN_POINTS]; int spawn_count;
    WallBuy wallbuys[MAX_WALLBUYS];     int wallbuy_count;
    MysteryBoxLoc mboxes[MAX_MYSTERY_BOXES]; int mbox_count;
    Room rooms[MAX_ROOMS];              int room_count;
    Door doors[MAX_DOORS];              int door_count;
    PerkMachine perks[MAX_PERKS];       int perk_count;
    bool has_pap;                       // Pack-a-Punch machine present
    Vector3 pap_pos;

    // runtime mesh cache (one mesh per box, tiled UVs)
    Mesh  meshes[MAX_MAP_BOXES];
    bool  mesh_built;
} GameMap;

bool map_load(GameMap *m, const char *path);
void map_unload(GameMap *m);
void map_build_meshes(GameMap *m);
int  map_list_dir(char out[][MAX_NAME], int max);     // scans assets/maps/*.map
void map_peek(const char *file, char *name, int name_len, char *desc, int desc_len);
int  map_room_of(const GameMap *m, Vector3 p);        // room index or -1

// collision helpers
bool map_move_aabb(GameMap *m, Vector3 *pos, Vector3 *vel, float dt,
                   float radius, float height, float step_height, bool *on_ground);
bool map_body_fits(GameMap *m, Vector3 pos, float radius, float height);
RayCollision map_raycast(GameMap *m, Ray ray, float max_dist);
Vector3 map_box_center(const MapBox *b);
BoundingBox barrier_bb(const Barrier *b);
BoundingBox door_bb(const Door *d);
Vector3 barrier_outward(const Barrier *b);

// ---------------------------------------------------------------------------
// Assets: textures & sounds loaded by name from ./assets
// ---------------------------------------------------------------------------
typedef struct AssetBank {
    Texture2D textures[MAX_SOUNDS];
    char      tex_names[MAX_SOUNDS][MAX_TEXNAME];
    int       tex_count;
    Sound     sounds[MAX_SOUNDS];
    char      snd_names[MAX_SOUNDS][MAX_TEXNAME];
    int       snd_count;
    Music     menu_music;
    bool      has_menu_music;
    // fallbacks
    Texture2D tex_fallback;
} AssetBank;

void assets_load_all(void);          // scans assets/textures + assets/sounds
void assets_unload_all(void);
Texture2D *assets_tex(const char *name);   // never NULL (fallback texture)
void sfx_play(const char *name, float volume, float pitch);
void sfx_play_at(const char *name, Vector3 at, float base_vol);
void assets_menu_music(bool play);
void assets_menu_music_update(void);

// ---------------------------------------------------------------------------
// Zombies
// ---------------------------------------------------------------------------
typedef enum {
    ZSTATE_INACTIVE = 0,
    ZSTATE_AT_BARRIER,    // tearing boards (theater)
    ZSTATE_RISING,        // climbing out of a grave (cemetery)
    ZSTATE_FALLING,       // dropping in from above (trenches)
    ZSTATE_CHASE,
    ZSTATE_ATTACK,
    ZSTATE_DYING
} ZombieState;

typedef struct Zombie {
    bool   active;
    ZombieState state;
    Vector3 pos;           // feet position
    Vector3 vel;
    float  yaw;            // facing
    float  hp, maxhp;
    float  speed;
    float  state_t;        // time in current state
    float  anim_t;
    float  attack_cd;
    float  hit_flash;
    int    at_barrier;     // barrier index or -1
    bool   runner;
    // steering / anti-stuck (nav-guided chase)
    Vector3 last_pos;      // where we were last frame (stuck detection)
    float  stuck_t;        // seconds without making progress
    float  detour_t;       // >0 while taking a detour
    float  detour_x, detour_z;
    bool   detour_slide;   // detour is a wall-slide (full override), not a
                           // random unstick lunge
} Zombie;

void zombies_reset(void);
void zombies_update_round(float dt);            // spawning + round flow
void zombies_update(float dt, bool player_alive);
void zombies_draw(void);
Zombie *zombies_spawn(int barrier_or_spawn_idx, bool at_barrier);
int  zombies_alive_count(void);
int  zombies_pending_count(void);
int  zombies_total_spawned(void);
bool zombies_slot_pos(int slot, Vector3 *out);  // pathtest per-slot query
bool zombies_slot_debug(int slot, Vector3 *out, int *state, float *stuck_t,
                        float *detour_t);
bool zombies_nearest(Vector3 to, Vector3 *out);   // nearest live zombie center
// hitscan query: returns zombie hit info, fills hit_zombie + headshot flag
typedef struct ZombieHit { int index; float dist; bool head; } ZombieHit;
int  zombies_raycast(Ray ray, float max_dist, ZombieHit *out, int max_out);
void zombies_damage(int index, float dmg, Vector3 at, bool headshot,
                    int *killed_out, Vector3 *kill_pos);

// ---------------------------------------------------------------------------
// Navigation (nav.c): walkability grid + BFS flow field for zombie AI
// ---------------------------------------------------------------------------
void nav_build(const GameMap *m);                 // after map_load
void nav_unload(void);
void nav_update(const GameMap *m, Vector3 player_pos, float dt);  // 1x/frame
bool nav_steer(const GameMap *m, Vector3 pos, Vector3 *out_target);
void nav_debug_dump(void);                        // --navdump (ASCII field)
void nav_debug_cell(float x, float z);                 // --navcell (levels+dist)

// ---------------------------------------------------------------------------
// Player
// ---------------------------------------------------------------------------
typedef struct Player {
    Vector3 pos;           // feet position
    Vector3 vel;
    float   yaw, pitch;    // radians
    bool    on_ground;
    int     hp;
    float   hurt_t;        // >0 while screen blood visible
    float   regen_t;       // time since last damage
    int     points;
    int     kills, hits;
    WeaponSlot slots[2];
    int     cur_slot;      // 0/1
    float   bob_t;
    float   recoil;        // decaying camera kick
    float   knife_t;
    float   interact_cd;
    bool    ads;           // aiming down sights
    float   dead_t;
    bool    perks[PERK_TYPE_COUNT];
    int     grenades;      // throwable grenades left
    float   grenade_cd;
} Player;

void player_reset(Vector3 spawn, float yaw);
void player_update(float dt, bool input_enabled);
void player_damage(int amount);
void player_draw_viewmodel(float dt);
int  player_give_weapon(int def_idx, bool from_box);
int  player_ammo_refill(int def_idx);
bool player_try_spend(int amount);
void player_fire_current(void);
void player_reload_current(void);
void player_switch_slot(int slot);
void player_knife(void);
const char *player_interact_prompt(void);       // "" if nothing in range
void player_try_interact(void);
void player_throw_grenade(void);
void player_update_grenades(float dt);
void player_draw_grenades(void);
void player_reset_grenades(void);
int  player_max_hp(void);
int  weapon_mag_size(const WeaponSlot *s);
float weapon_damage(const WeaponSlot *s);
float weapon_reload_time(const WeaponSlot *s);
float weapon_cooldown_time(const WeaponSlot *s);
const char *weapon_display_name(const WeaponSlot *s, char *buf, int buflen);
void game_explode_at(Vector3 pos);              // grenade detonation (AoE)
void zombies_apply_explosion(Vector3 pos, float radius, float max_damage);

// ---------------------------------------------------------------------------
// Particles
// ---------------------------------------------------------------------------
typedef enum {
    P_BLOOD = 0,
    P_SPARK,
    P_TRACER,
    P_MUZZLE,
    P_DEBRIS
} ParticleKind;

typedef struct Particle {
    bool  active;
    ParticleKind kind;
    Vector3 pos, vel, prev;
    float  life, max_life;
    float  size;
    Color  color;
} Particle;

void particles_reset(void);
void particles_update(float dt);
void particles_draw(void);
void particles_spawn_blood(Vector3 at, int n);
void particles_spawn_sparks(Vector3 at, int n);
void particles_spawn_tracer(Vector3 from, Vector3 to);
void particles_spawn_muzzle(Vector3 at);
void particles_spawn_debris(Vector3 at, int n);

// ---------------------------------------------------------------------------
// World rendering (boxes, barriers, buys, mystery boxes, sky)
// ---------------------------------------------------------------------------
void world_init(void);
void world_unload(void);
void world_draw(const GameMap *m);
void world_update(float dt);

// mystery box state machine
typedef struct MysteryBoxState {
    float roll_t;          // >0 while rolling
    float offer_t;         // >0 while weapon on offer
    int   offer_weapon;
    float lid_angle;
} MysteryBoxState;

extern MysteryBoxState g_mbox[MAX_MYSTERY_BOXES];

// ---------------------------------------------------------------------------
// UI & game flow
// ---------------------------------------------------------------------------
typedef enum {
    GS_SPLASH = 0,
    GS_MENU,
    GS_MAPSELECT,
    GS_SETTINGS,
    GS_EXTRAS,
    GS_PLAYING,
    GS_PAUSED,
    GS_DEAD
} GameState;

typedef struct MapEntry { char file[MAX_NAME]; char name[48]; char desc[128]; } MapEntry;

typedef struct Game {
    GameState state;
    GameMap   map;
    bool      map_loaded;
    char      cur_map_file[MAX_NAME];

    int   round;
    float round_break_t;   // >0 during the pause between rounds
    bool  round_active;

    MapEntry map_entries[MAX_MAPS];
    int      map_count;
    int      map_selected;

    int   best_round;      // overall, kept for the session
    float frame_time;
    float time;            // total elapsed
    bool  autotest;
    bool  pathtest;        // --pathtest: stationary player, reachability run
    bool  want_quit;
} Game;

extern Game g;
extern Player g_player;
extern Settings g_settings;
extern Camera3D g_camera;

void ui_update(void);
void ui_draw(void);
void ui_draw_hud(void);
void ui_point_popup(Vector3 world_pos, int amount);

// game flow helpers (main.c)
void game_start_map(const char *file);
void game_end_to_menu(void);
void game_update_playing(float dt);
void game_draw_playing(void);
const char *game_asset_path(char *out, int outlen, const char *fmt, ...);

// zombie economy tuning (zombies.c)
int  round_zombie_count(int round);
float round_zombie_hp(int round);
float round_zombie_speed(int round, bool *runner);
float round_spawn_interval(int round);

#endif // GAME_H
