// player.c - first person controller: movement, camera, weapons handling,
// economy (points), interaction (wall buys, mystery box, barrier repair)
// and the procedurally drawn viewmodel.
#include "game.h"

Player g_player;
Camera3D g_camera;

static bool fire_pressed_edge = false;   // consumed by semi-auto weapons

// ---------------------------------------------------------------------------
// Reset / spawn
// ---------------------------------------------------------------------------
void player_reset(Vector3 spawn, float yaw)
{
    memset(&g_player, 0, sizeof(g_player));
    g_player.pos = spawn;
    g_player.yaw = yaw * DEG2RAD;
    g_player.pitch = 0;
    g_player.hp = PLAYER_MAX_HP;
    g_player.cur_slot = 0;
    g_player.grenades = GRENADE_START;
    player_reset_grenades();

    for (int i = 0; i < 2; i++) g_player.slots[i].def = -1;

    // starting weapon from weapons.cfg (start = 1)
    for (int i = 0; ; i++)
    {
        const WeaponDef *d = weapons_get(i);
        if (!d) break;
        if (d->start_weapon) { player_give_weapon(i, false); break; }
    }
    // no start weapon defined -> first weapon in cfg
    if (g_player.slots[0].def < 0 && weapons_get(0))
        player_give_weapon(0, false);
}

// ---------------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------------
static void player_update_camera(float dt)
{
    const WeaponDef *def = weapons_get(g_player.slots[g_player.cur_slot].def);

    float bob = 0.0f, sway = 0.0f;
    float speed_x = fabsf(g_player.vel.x) + fabsf(g_player.vel.z);
    if (g_player.on_ground && speed_x > 0.5f)
    {
        g_player.bob_t += dt * (speed_x > 5.0f ? 11.0f : 8.0f);
        bob = sinf(g_player.bob_t) * 0.035f * (g_player.ads ? 0.3f : 1.0f);
        sway = cosf(g_player.bob_t * 0.5f) * 0.02f;
    }

    g_camera.target.x = g_player.pos.x + cosf(g_player.yaw) * cosf(g_player.pitch) * -1.0f;
    g_camera.target.y = g_player.pos.y + PLAYER_EYE + sinf(g_player.pitch) + bob;
    g_camera.target.z = g_player.pos.z + sinf(g_player.yaw) * cosf(g_player.pitch) * -1.0f;

    g_camera.position.x = g_player.pos.x;
    g_camera.position.y = g_player.pos.y + PLAYER_EYE + bob;
    g_camera.position.z = g_player.pos.z;

    g_camera.up = (Vector3){ 0, 1, 0 };

    // FOV: sprint/ads zoom
    float target_fov = g_settings.fov;
    if (g_player.ads && def && def->ads_zoom > 0) target_fov = def->ads_zoom;
    g_camera.fovy = lerpf(g_camera.fovy, target_fov, clampf(dt * 12.0f, 0, 1));
    (void)sway;

    // recoil kick applied to pitch, decays
    g_player.pitch = clampf(g_player.pitch, CAMERA_MIN_PITCH, CAMERA_MAX_PITCH);
}

// ---------------------------------------------------------------------------
// Movement
// ---------------------------------------------------------------------------
static void player_move(float dt)
{
    // input direction
    float fwd = 0, strafe = 0;
    bool sprint = false;
    if (g.autotest && !g.pathtest)
    {
        fwd = 1;   // scripted headless walk (yaw is overridden by the caller)
    }
    else
    {
        if (IsKeyDown(KEY_W)) fwd += 1;
        if (IsKeyDown(KEY_S)) fwd -= 1;
        if (IsKeyDown(KEY_D)) strafe += 1;
        if (IsKeyDown(KEY_A)) strafe -= 1;
        sprint = IsKeyDown(KEY_LEFT_SHIFT) && fwd > 0 && !g_player.ads;
    }
    float speed = PLAYER_WALK_SPEED * (sprint ? PLAYER_SPRINT_MULT : 1.0f)
                                 * (g_player.ads ? 0.6f : 1.0f);

    // basis vectors (yaw)
    float sy = sinf(g_player.yaw), cy = cosf(g_player.yaw);
    Vector3 wish = { 0, 0, 0 };
    wish.x = (-cy * fwd + sy * strafe);
    wish.z = (-sy * fwd - cy * strafe);
    float len = sqrtf(wish.x * wish.x + wish.z * wish.z);
    if (len > 0.001f) { wish.x /= len; wish.z /= len; }

    float accel = g_player.on_ground ? 40.0f : 8.0f;
    g_player.vel.x = lerpf(g_player.vel.x, wish.x * speed, clampf(accel * dt, 0, 1));
    g_player.vel.z = lerpf(g_player.vel.z, wish.z * speed, clampf(accel * dt, 0, 1));

    if (!g.autotest && IsKeyPressed(KEY_SPACE) && g_player.on_ground)
        g_player.vel.y = PLAYER_JUMP_SPEED;
    g_player.vel.y -= PLAYER_GRAVITY * dt;

    map_move_aabb(&g.map, &g_player.pos, &g_player.vel, dt,
                  PLAYER_RADIUS, PLAYER_HEIGHT, PLAYER_STEP_HEIGHT, &g_player.on_ground);

    // barriers always block the player (even with all boards torn).
    // Clamp to whichever side of the wall the player is already on - a
    // naive smallest-axis pushout can slide the player through the opening
    // when approaching diagonally.
    for (int i = 0; i < g.map.barrier_count; i++)
    {
        const Barrier *b = &g.map.barriers[i];
        BoundingBox bb = barrier_bb(b);
        BoundingBox e = {
            (Vector3){ g_player.pos.x - PLAYER_RADIUS, g_player.pos.y, g_player.pos.z - PLAYER_RADIUS },
            (Vector3){ g_player.pos.x + PLAYER_RADIUS, g_player.pos.y + PLAYER_HEIGHT, g_player.pos.z + PLAYER_RADIUS }
        };
        if (e.min.x < bb.max.x && e.max.x > bb.min.x &&
            e.min.y < bb.max.y && e.max.y > bb.min.y &&
            e.min.z < bb.max.z && e.max.z > bb.min.z)
        {
            if (b->face <= 1)   // wall runs along X -> clamp along Z
            {
                if (g_player.pos.z < b->pos.z)
                    g_player.pos.z = fminf(g_player.pos.z, bb.min.z - PLAYER_RADIUS);
                else
                    g_player.pos.z = fmaxf(g_player.pos.z, bb.max.z + PLAYER_RADIUS);
            }
            else                // wall runs along Z -> clamp along X
            {
                if (g_player.pos.x < b->pos.x)
                    g_player.pos.x = fminf(g_player.pos.x, bb.min.x - PLAYER_RADIUS);
                else
                    g_player.pos.x = fmaxf(g_player.pos.x, bb.max.x + PLAYER_RADIUS);
            }
        }
    }

    // closed buyable doors block exactly like barriers
    for (int i = 0; i < g.map.door_count; i++)
    {
        const Door *d = &g.map.doors[i];
        if (d->open) continue;
        BoundingBox bb = door_bb(d);
        BoundingBox e = {
            (Vector3){ g_player.pos.x - PLAYER_RADIUS, g_player.pos.y, g_player.pos.z - PLAYER_RADIUS },
            (Vector3){ g_player.pos.x + PLAYER_RADIUS, g_player.pos.y + PLAYER_HEIGHT, g_player.pos.z + PLAYER_RADIUS }
        };
        if (e.min.x < bb.max.x && e.max.x > bb.min.x &&
            e.min.y < bb.max.y && e.max.y > bb.min.y &&
            e.min.z < bb.max.z && e.max.z > bb.min.z)
        {
            if (d->face <= 1)
            {
                if (g_player.pos.z < d->pos.z)
                    g_player.pos.z = fminf(g_player.pos.z, bb.min.z - PLAYER_RADIUS);
                else
                    g_player.pos.z = fmaxf(g_player.pos.z, bb.max.z + PLAYER_RADIUS);
            }
            else
            {
                if (g_player.pos.x < d->pos.x)
                    g_player.pos.x = fminf(g_player.pos.x, bb.min.x - PLAYER_RADIUS);
                else
                    g_player.pos.x = fmaxf(g_player.pos.x, bb.max.x + PLAYER_RADIUS);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Weapons
// ---------------------------------------------------------------------------
int player_give_weapon(int def_idx, bool from_box)
{
    (void)from_box;
    const WeaponDef *def = weapons_get(def_idx);
    if (!def) return -1;

    int slot = -1;
    if (g_player.slots[0].def < 0) slot = 0;
    else if (g_player.slots[1].def < 0) slot = 1;
    else slot = g_player.cur_slot;

    g_player.slots[slot].def = def_idx;
    g_player.slots[slot].ammo = def->mag;
    g_player.slots[slot].reserve = def->reserve;
    g_player.slots[slot].cooldown = 0.3f;
    g_player.slots[slot].reload_t = 0;
    g_player.slots[slot].switch_t = 0.3f;
    g_player.cur_slot = slot;
    return slot;
}

int player_ammo_refill(int def_idx)
{
    const WeaponDef *def = weapons_get(def_idx);
    if (!def) return 0;
    for (int i = 0; i < 2; i++)
    {
        if (g_player.slots[i].def == def_idx)
        {
            g_player.slots[i].reserve = def->reserve;
            return 1;
        }
    }
    return 0;
}

bool player_try_spend(int amount)
{
    if (g_player.points >= amount)
    {
        g_player.points -= amount;
        return true;
    }
    sfx_play("deny", 0.8f, 1.0f);
    return false;
}

// ---------------------------------------------------------------------------
// Perk / Pack-a-Punch aware weapon + health helpers
// ---------------------------------------------------------------------------
int player_max_hp(void)
{
    return g_player.perks[PERK_JUGG] ? PLAYER_MAX_HP + PERK_JUGG_BONUS : PLAYER_MAX_HP;
}

int weapon_mag_size(const WeaponSlot *s)
{
    const WeaponDef *d = weapons_get(s->def);
    if (!d) return 0;
    return s->upgraded ? d->mag + d->mag / PAP_MAG_BONUS_DIV : d->mag;
}

float weapon_damage(const WeaponSlot *s)
{
    const WeaponDef *d = weapons_get(s->def);
    if (!d) return 0.0f;
    return s->upgraded ? d->damage * PAP_DAMAGE_MULT : d->damage;
}

float weapon_reload_time(const WeaponSlot *s)
{
    const WeaponDef *d = weapons_get(s->def);
    if (!d) return 1.0f;
    float t = d->reload_time;
    if (s->upgraded) t *= 0.85f;                          // PaP: slightly faster
    if (g_player.perks[PERK_SPEED]) t *= PERK_SPEED_RELOAD_MULT;  // speed cola
    return t;
}

float weapon_cooldown_time(const WeaponSlot *s)
{
    const WeaponDef *d = weapons_get(s->def);
    if (!d) return 0.5f;
    float t = 60.0f / (float)d->rpm;
    if (g_player.perks[PERK_DOUBLE]) t /= PERK_DOUBLE_RPM_MULT;   // double tap
    return t;
}

const char *weapon_display_name(const WeaponSlot *s, char *buf, int buflen)
{
    const WeaponDef *d = weapons_get(s->def);
    if (!d) return "";
    snprintf(buf, buflen, "%s%s", s->upgraded ? "* " : "", d->name);
    return buf;
}

void player_switch_slot(int slot)
{
    if (slot == g_player.cur_slot) return;
    if (slot < 0 || slot > 1) return;
    if (g_player.slots[slot].def < 0) return;
    g_player.cur_slot = slot;
    g_player.slots[slot].switch_t = 0.35f;
    g_player.slots[slot].reload_t = 0;
    sfx_play("weapon_switch", 0.5f, 1.0f);
}

void player_reload_current(void)
{
    WeaponSlot *s = &g_player.slots[g_player.cur_slot];
    const WeaponDef *def = weapons_get(s->def);
    if (!def || s->reload_t > 0 || s->ammo >= weapon_mag_size(s) || s->reserve <= 0) return;
    s->reload_t = weapon_reload_time(s);
    sfx_play("reload", 0.7f, frand_range(0.95f, 1.05f));
}

static void finish_reload(WeaponSlot *s, const WeaponDef *def)
{
    (void)def;
    int need = weapon_mag_size(s) - s->ammo;
    int take = (s->reserve < need) ? s->reserve : need;
    s->ammo += take;
    s->reserve -= take;
    sfx_play("reload_end", 0.7f, 1.0f);
}

// get world-space muzzle position
static Vector3 muzzle_world_pos(void)
{
    Vector3 f = g_camera.target;
    f.x -= g_camera.position.x; f.y -= g_camera.position.y; f.z -= g_camera.position.z;
    f = Vector3Normalize(f);
    Vector3 r = Vector3Normalize(Vector3CrossProduct(f, (Vector3){ 0, 1, 0 }));
    Vector3 u = Vector3CrossProduct(r, f);
    Vector3 p = g_camera.position;
    p = Vector3Add(p, Vector3Scale(f, 0.85f));
    p = Vector3Add(p, Vector3Scale(r, 0.16f));
    p = Vector3Add(p, Vector3Scale(u, -0.12f));
    return p;
}

void player_fire_current(void)
{
    WeaponSlot *s = &g_player.slots[g_player.cur_slot];
    const WeaponDef *def = weapons_get(s->def);
    if (!def || s->cooldown > 0 || s->reload_t > 0 || s->switch_t > 0) return;
    if (s->ammo <= 0)
    {
        sfx_play("dry_fire", 0.6f, 1.0f);
        s->cooldown = 0.25f;
        return;
    }

    s->ammo--;
    s->cooldown = weapon_cooldown_time(s);
    g_player.recoil += def->recoil * 0.02f;
    g_player.hits += 0; // hit points awarded per landed bullet below

    // sound & flash
    sfx_play(def->sound, 0.9f, frand_range(0.96f, 1.04f));
    Vector3 muzzle = muzzle_world_pos();
    particles_spawn_muzzle(muzzle);

    // direction with spread
    Vector3 dir = Vector3Normalize(Vector3Subtract(g_camera.target, g_camera.position));
    float spread = def->spread * (g_player.ads ? def->ads_spread_mult : 1.0f);

    for (int p = 0; p < def->pellets; p++)
    {
        Vector3 d = dir;
        if (spread > 0)
        {
            Vector3 r = Vector3Normalize(Vector3CrossProduct(d, (Vector3){ 0, 1, 0 }));
            Vector3 u = Vector3CrossProduct(r, d);
            d = Vector3Add(d, Vector3Scale(r, frand_range(-spread, spread)));
            d = Vector3Add(d, Vector3Scale(u, frand_range(-spread, spread)));
            d = Vector3Normalize(d);
        }
        Ray ray = { g_camera.position, d };

        // zombies along the ray (sorted by distance)
        ZombieHit hits[8];
        int nh = zombies_raycast(ray, def->range, hits, 8);

        // where does the map stop the bullet?
        RayCollision rc = map_raycast(&g.map, ray, def->range);
        float wall_dist = rc.hit ? rc.distance : def->range;

        int remaining = def->penetrate;
        float dmg = weapon_damage(s);
        Vector3 end = Vector3Add(ray.position, Vector3Scale(d, wall_dist));
        bool any_hit = false;

        for (int h = 0; h < nh && remaining > 0; h++)
        {
            if (hits[h].dist > wall_dist) break;
            int killed = 0;
            Vector3 kpos = { 0 };
            float final = dmg * (hits[h].head ? def->head_mult : 1.0f);
            zombies_damage(hits[h].index, final,
                           Vector3Add(ray.position, Vector3Scale(d, hits[h].dist)),
                           hits[h].head, &killed, &kpos);
            remaining--;
            dmg *= 0.7f;    // falloff through each body
            any_hit = true;
            g_player.hits++;
            if (killed) break;
        }

        if (any_hit)
        {
            g_player.points += PTS_PER_HIT;
            sfx_play("hitmarker", 0.35f, 1.0f);
        }
        else if (rc.hit)
        {
            particles_spawn_sparks(rc.point, 4);
        }
        particles_spawn_tracer(muzzle, end);
    }
}

void player_knife(void)
{
    if (g_player.knife_t > 0) return;
    g_player.knife_t = KNIFE_CD;
    sfx_play("knife", 0.8f, frand_range(0.9f, 1.1f));

    Vector3 dir = Vector3Normalize(Vector3Subtract(g_camera.target, g_camera.position));
    Ray ray = { g_camera.position, dir };
    ZombieHit hits[2];
    int nh = zombies_raycast(ray, KNIFE_RANGE, hits, 2);
    if (nh > 0)
    {
        int killed = 0;
        Vector3 kpos = { 0 };
        // scales a little with the round so it stays a one-hit kill early on
        float dmg = 130.0f + g.round * 20.0f;
        zombies_damage(hits[0].index, dmg,
                       Vector3Add(ray.position, Vector3Scale(dir, hits[0].dist)),
                       false, &killed, &kpos);
        g_player.points += PTS_PER_HIT;
        sfx_play("knife_hit", 0.8f, frand_range(0.9f, 1.1f));
    }
}

// ---------------------------------------------------------------------------
// Damage / death
// ---------------------------------------------------------------------------
void player_damage(int amount)
{
    if (g_player.hp <= 0) return;
    g_player.hp -= amount;
    g_player.hurt_t = 1.0f;
    g_player.regen_t = 0.0f;
    sfx_play("player_hurt", 0.8f, frand_range(0.9f, 1.1f));
    if (g_player.hp <= 0)
    {
        g_player.hp = 0;
        g_player.dead_t = 0.001f;   // triggers game over in game_update_playing
        sfx_play("player_death", 0.9f, 1.0f);
    }
}

// ---------------------------------------------------------------------------
// Interaction: wall buys, mystery box, barrier repair
// ---------------------------------------------------------------------------
static int nearest_wallbuy(float max_dist)
{
    int best = -1;
    float bd = max_dist;
    for (int i = 0; i < g.map.wallbuy_count; i++)
    {
        float d = Vector3Distance(g.map.wallbuys[i].pos, g_player.pos);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

static int nearest_mbox(float max_dist)
{
    int best = -1;
    float bd = max_dist;
    for (int i = 0; i < g.map.mbox_count; i++)
    {
        float d = Vector3Distance(g.map.mboxes[i].pos, g_player.pos);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

static int nearest_barrier(float max_dist)
{
    int best = -1;
    float bd = max_dist;
    for (int i = 0; i < g.map.barrier_count; i++)
    {
        if (g.map.barriers[i].boards >= BARRIER_MAX_BOARDS) continue;
        float d = Vector3Distance(g.map.barriers[i].pos, g_player.pos);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

static int nearest_door(float max_dist)
{
    int best = -1;
    float bd = max_dist;
    for (int i = 0; i < g.map.door_count; i++)
    {
        if (g.map.doors[i].open) continue;
        float d = Vector3Distance(g.map.doors[i].pos, g_player.pos);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

static int nearest_perk(float max_dist)
{
    int best = -1;
    float bd = max_dist;
    for (int i = 0; i < g.map.perk_count; i++)
    {
        if (g_player.perks[g.map.perks[i].type]) continue;   // already owned
        float d = Vector3Distance(g.map.perks[i].pos, g_player.pos);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

static float pap_dist(void)
{
    if (!g.map.has_pap) return 1e9f;
    return Vector3Distance(g.map.pap_pos, g_player.pos);
}

static int perk_cost(PerkType t)
{
    switch (t)
    {
        case PERK_DOUBLE: return PERK_COST_DOUBLE;
        case PERK_SPEED:  return PERK_COST_SPEED;
        case PERK_JUGG:   return PERK_COST_JUGG;
        default:          return PERK_COST_QUICK;
    }
}

const char *player_interact_prompt(void)
{
    static char prompt[128];

    // 1) mystery box on offer (taking it wins over everything)
    int mb = nearest_mbox(2.6f);
    if (mb >= 0)
    {
        MysteryBoxState *st = &g_mbox[mb];
        if (st->offer_t > 0)
        {
            const WeaponDef *def = weapons_get(st->offer_weapon);
            snprintf(prompt, sizeof(prompt), "[F] Take %s", def ? def->name : "?");
            return prompt;
        }
    }

    // 2) buyable door
    int dr = nearest_door(2.8f);
    if (dr >= 0)
    {
        snprintf(prompt, sizeof(prompt), "[F] Open Door  [%d]", g.map.doors[dr].cost);
        return prompt;
    }

    // 3) perk machine
    int pk = nearest_perk(2.8f);
    if (pk >= 0)
    {
        static const char *perk_names[PERK_TYPE_COUNT] = {
            "Quick Revive", "Double Tap", "Speed Cola", "Juggernog" };
        snprintf(prompt, sizeof(prompt), "[F] Buy %s  [%d]",
                 perk_names[g.map.perks[pk].type], perk_cost(g.map.perks[pk].type));
        return prompt;
    }

    // 4) Pack-a-Punch
    if (pap_dist() < 2.8f)
    {
        WeaponSlot *s = &g_player.slots[g_player.cur_slot];
        if (s->def >= 0 && !s->upgraded)
        {
            char nbuf[64];
            snprintf(prompt, sizeof(prompt), "[F] Pack-a-Punch %s  [%d]",
                     weapon_display_name(s, nbuf, sizeof(nbuf)), PAP_COST);
            return prompt;
        }
    }

    // 5) wall buy (weapons + grenade refills)
    int wb = nearest_wallbuy(2.4f);
    if (wb >= 0)
    {
        WallBuy *w = &g.map.wallbuys[wb];
        if (w->weapon == WALLBUY_GRENADES)
        {
            snprintf(prompt, sizeof(prompt), "[F] Buy Grenades x%d  [%d]",
                     GRENADE_MAX, w->price > 0 ? w->price : GRENADE_COST);
            return prompt;
        }
        const WeaponDef *def = weapons_get(w->weapon);
        int price = (w->price > 0) ? w->price : def->price;
        bool owned = (g_player.slots[0].def == w->weapon || g_player.slots[1].def == w->weapon);
        if (owned)
            snprintf(prompt, sizeof(prompt), "[F] Ammo %s  [%d]", def->name, price / 2);
        else
            snprintf(prompt, sizeof(prompt), "[F] Buy %s  [%d]", def->name, price);
        return prompt;
    }

    // 6) mystery box roll
    if (mb >= 0 && g_mbox[mb].roll_t <= 0)
    {
        snprintf(prompt, sizeof(prompt), "[F] Open Mystery Box  [%d]", MYSTERY_BOX_COST);
        return prompt;
    }

    // 7) barrier repair
    int br = nearest_barrier(2.6f);
    if (br >= 0)
        return "[F] Rebuild barrier";

    return "";
}

void player_try_interact(void)
{
    // 1) mystery box offer
    int mb = nearest_mbox(2.6f);
    if (mb >= 0 && g_mbox[mb].offer_t > 0)
    {
        player_give_weapon(g_mbox[mb].offer_weapon, true);
        g_mbox[mb].offer_t = 0;
        sfx_play("buy", 0.8f, 1.0f);
        return;
    }

    // 2) door
    int dr = nearest_door(2.8f);
    if (dr >= 0)
    {
        Door *d = &g.map.doors[dr];
        if (player_try_spend(d->cost))
        {
            d->open = true;
            sfx_play("door_open", 0.9f, 1.0f);
            particles_spawn_debris(d->pos, 10);
        }
        return;
    }

    // 3) perk machine
    int pk = nearest_perk(2.8f);
    if (pk >= 0)
    {
        PerkMachine *p = &g.map.perks[pk];
        if (player_try_spend(perk_cost(p->type)))
        {
            g_player.perks[p->type] = true;
            if (p->type == PERK_JUGG)
                g_player.hp = fminf(g_player.hp + PERK_JUGG_BONUS, player_max_hp());
            sfx_play("perk_buy", 0.9f, 1.0f);
        }
        return;
    }

    // 4) Pack-a-Punch
    if (pap_dist() < 2.8f)
    {
        WeaponSlot *s = &g_player.slots[g_player.cur_slot];
        if (s->def >= 0 && !s->upgraded && player_try_spend(PAP_COST))
        {
            s->upgraded = true;
            s->ammo = weapon_mag_size(s);       // free full mag on upgrade
            sfx_play("pap_buy", 0.9f, 1.0f);
        }
        return;
    }

    // 5) wall buy
    int wb = nearest_wallbuy(2.4f);
    if (wb >= 0)
    {
        WallBuy *w = &g.map.wallbuys[wb];
        if (w->weapon == WALLBUY_GRENADES)
        {
            if (g_player.grenades < GRENADE_MAX &&
                player_try_spend(w->price > 0 ? w->price : GRENADE_COST))
            {
                g_player.grenades = GRENADE_MAX;
                sfx_play("buy", 0.8f, 1.0f);
            }
            return;
        }
        const WeaponDef *def = weapons_get(w->weapon);
        int price = (w->price > 0) ? w->price : def->price;
        bool owned = (g_player.slots[0].def == w->weapon || g_player.slots[1].def == w->weapon);
        if (owned)
        {
            if (player_try_spend(price / 2))
            {
                player_ammo_refill(w->weapon);
                sfx_play("buy", 0.8f, 1.0f);
            }
        }
        else if (player_try_spend(price))
        {
            player_give_weapon(w->weapon, false);
            sfx_play("buy", 0.8f, 1.0f);
        }
        return;
    }

    // 6) mystery box roll
    if (mb >= 0 && g_mbox[mb].roll_t <= 0 && player_try_spend(MYSTERY_BOX_COST))
    {
        g_mbox[mb].roll_t = MYSTERY_ROLL_TIME;
        g_mbox[mb].offer_weapon = weapons_random_box_pick();
        sfx_play("box_jingle", 0.9f, 1.0f);
        return;
    }

    // 7) barrier repair
    int br = nearest_barrier(2.6f);
    if (br >= 0)
    {
        Barrier *b = &g.map.barriers[br];
        if (b->boards < BARRIER_MAX_BOARDS && b->repair_cd <= 0)
        {
            b->boards++;
            b->repair_cd = BARRIER_REPAIR_CD;
            g_player.points += BARRIER_REPAIR_PTS;
            sfx_play("board_repair", 0.8f, frand_range(0.9f, 1.1f));
        }
    }
}

// ---------------------------------------------------------------------------
// Grenades
// ---------------------------------------------------------------------------
static Grenade s_grenades[MAX_GRENADES];

void player_reset_grenades(void)
{
    memset(s_grenades, 0, sizeof(s_grenades));
}

void player_throw_grenade(void)
{
    if (g_player.grenades <= 0 || g_player.grenade_cd > 0 || g_player.hp <= 0) return;
    Grenade *g = NULL;
    for (int i = 0; i < MAX_GRENADES; i++)
        if (!s_grenades[i].active) { g = &s_grenades[i]; break; }
    if (!g) return;

    g_player.grenades--;
    g_player.grenade_cd = GRENADE_THROW_CD;

    Vector3 f = Vector3Normalize(Vector3Subtract(g_camera.target, g_camera.position));
    g->active = true;
    g->on_ground = false;
    g->fuse = GRENADE_FUSE;
    g->pos = Vector3Add(g_camera.position, Vector3Scale(f, 0.4f));
    g->vel = Vector3Add(Vector3Scale(f, 13.0f), (Vector3){ g_player.vel.x * 0.5f, 3.5f, g_player.vel.z * 0.5f });
    sfx_play("grenade_throw", 0.7f, 1.0f);
}

void player_update_grenades(float dt)
{
    for (int i = 0; i < MAX_GRENADES; i++)
    {
        Grenade *gr = &s_grenades[i];
        if (!gr->active) continue;

        gr->fuse -= dt;
        if (gr->fuse <= 0)
        {
            gr->active = false;
            game_explode_at(gr->pos);
            continue;
        }

        // integrate with map collision; bounce on landing / wall hits
        Vector3 pre = gr->pos;
        bool on_ground = false;
        gr->vel.y -= 20.0f * dt;
        map_move_aabb(&g.map, &gr->pos, &gr->vel, dt, 0.09f, 0.18f, 0.08f, &on_ground);

        if (on_ground && !gr->on_ground && gr->vel.y < -1.5f)
        {
            gr->vel.y = -gr->vel.y * 0.45f;
            gr->vel.x *= 0.65f;
            gr->vel.z *= 0.65f;
            sfx_play("grenade_bounce", 0.5f, frand_range(0.9f, 1.1f));
        }
        else if (on_ground)
        {
            gr->vel.x *= 1.0f - clampf(6.0f * dt, 0, 1);
            gr->vel.z *= 1.0f - clampf(6.0f * dt, 0, 1);
        }
        // stopped by a wall -> reflect roughly
        float moved = Vector3Distance(pre, gr->pos);
        if (moved < Vector3Length(gr->vel) * dt * 0.35f && !on_ground)
        {
            gr->vel.x = -gr->vel.x * 0.45f;
            gr->vel.z = -gr->vel.z * 0.45f;
            sfx_play("grenade_bounce", 0.4f, frand_range(0.9f, 1.1f));
        }
        gr->on_ground = on_ground;
    }
}

void player_draw_grenades(void)
{
    for (int i = 0; i < MAX_GRENADES; i++)
    {
        if (!s_grenades[i].active) continue;
        // blink faster the closer to detonation
        float blink = (sinf(g.time * (12.0f + 40.0f * (1.0f - s_grenades[i].fuse / GRENADE_FUSE))) > 0) ? 1.0f : 0.4f;
        DrawCube(s_grenades[i].pos, 0.14f, 0.16f, 0.14f,
                 (Color){ (unsigned char)(50 * blink), (unsigned char)(80 * blink), 50, 255 });
    }
}

// ---------------------------------------------------------------------------
// Per-frame update
// ---------------------------------------------------------------------------
void player_update(float dt, bool input_enabled)
{
    // hurt flash decays even when dead (otherwise the red overlay sticks)
    g_player.hurt_t = fmaxf(0, g_player.hurt_t - dt * 0.55f);

    if (g_player.hp <= 0) { player_update_camera(dt); return; }

    // ---- look ----
    // Convention: forward = (-cos(yaw)cos(pitch), sin(pitch), -sin(yaw)cos(pitch)),
    // so INCREASING yaw turns the view to the right -> mouse right adds to yaw.
    // Mouse up (md.y < 0) increases pitch -> looks up (standard, non-inverted).
    if (input_enabled && !g.autotest)
    {
        Vector2 md = GetMouseDelta();
        float sens = 0.0022f * g_settings.sensitivity;
        g_player.yaw   += md.x * sens;
        g_player.pitch -= md.y * sens * (g_settings.invert_y ? -1.0f : 1.0f);
        g_player.pitch = clampf(g_player.pitch, CAMERA_MIN_PITCH, CAMERA_MAX_PITCH);
    }
    else if (g.autotest)
    {
        // scripted headless input: slow pan, gentle pitch sway
        g_player.yaw = 1.5708f + sinf(g.time * 0.6f) * 1.6f;
        g_player.pitch = sinf(g.time * 0.27f) * 0.18f;
    }
    g_player.recoil = lerpf(g_player.recoil, 0, clampf(dt * 6.0f, 0, 1));

    // ---- move ----
    if (g.autotest && !g.pathtest)
    {
        // walk forward slowly so the camera tours the map
        float save_yaw = g_player.yaw;
        g_player.yaw = 1.5708f + sinf(g.time * 0.25f) * 2.2f;
        player_move(dt);
        g_player.yaw = save_yaw;
    }
    else player_move(dt);

    // ---- timers ----
    g_player.knife_t    = fmaxf(0, g_player.knife_t - dt);
    g_player.interact_cd= fmaxf(0, g_player.interact_cd - dt);
    g_player.grenade_cd = fmaxf(0, g_player.grenade_cd - dt);
    g_player.regen_t   += dt;
    for (int b = 0; b < g.map.barrier_count; b++)
        g.map.barriers[b].repair_cd = fmaxf(0, g.map.barriers[b].repair_cd - dt);

    // room tracking (drives room-based zombie spawning)
    int room = map_room_of(&g.map, g_player.pos);
    if (room >= 0) g.map.rooms[room].visited = true;

    // health regen (Quick Revive: sooner + faster; Jugg raises the cap)
    float regen_delay = PLAYER_REGEN_DELAY * (g_player.perks[PERK_QUICK] ? PERK_REGEN_DELAY_MULT : 1.0f);
    float regen_rate  = PLAYER_REGEN_RATE * (g_player.perks[PERK_QUICK] ? PERK_REGEN_RATE_MULT : 1.0f);
    if (g_player.regen_t > regen_delay && g_player.hp < player_max_hp())
        g_player.hp = (int)fminf((float)player_max_hp(), g_player.hp + regen_rate * dt);

    // ---- weapon slots ----
    for (int i = 0; i < 2; i++)
    {
        WeaponSlot *s = &g_player.slots[i];
        const WeaponDef *def = weapons_get(s->def);
        if (!def) continue;
        s->cooldown = fmaxf(0, s->cooldown - dt);
        s->switch_t = fmaxf(0, s->switch_t - dt);
        if (s->reload_t > 0)
        {
            s->reload_t -= dt;
            if (s->reload_t <= 0) finish_reload(s, def);
        }
    }

    if (!input_enabled) { player_update_camera(dt); return; }

    // ---- actions ----
    if (g.autotest && !g.pathtest)
    {
        // fire periodically; aim at the nearest zombie so the combat path
        // (hitscan -> damage -> points -> kills) is exercised on every map
        static float at_fire_t = 0;
        at_fire_t += dt;
        if (at_fire_t >= 0.3f)
        {
            at_fire_t = 0;
            g_player.ads = false;
            Vector3 zt;
            if (zombies_nearest(g_camera.position, &zt))
            {
                Vector3 d = Vector3Subtract(zt, g_camera.position);
                g_player.yaw = atan2f(-d.z, -d.x);
                g_player.pitch = asinf(clampf(d.y / fmaxf(Vector3Length(d), 0.01f), -1.0f, 1.0f));
                player_update_camera(dt);   // aim BEFORE firing this frame
            }
            player_fire_current();
            if (g_player.slots[g_player.cur_slot].ammo == 0)
                player_reload_current();
        }
        player_update_camera(dt);
        return;
    }

    g_player.ads = IsMouseButtonDown(MOUSE_BUTTON_RIGHT);

    const WeaponDef *def = weapons_get(g_player.slots[g_player.cur_slot].def);
    if (def)
    {
        if (def->is_auto && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) player_fire_current();
        else
        {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) fire_pressed_edge = true;
            if (fire_pressed_edge)
            {
                player_fire_current();
                fire_pressed_edge = false;
            }
        }
    }

    if (IsKeyPressed(KEY_R)) player_reload_current();
    if (IsKeyPressed(KEY_ONE)) player_switch_slot(0);
    if (IsKeyPressed(KEY_TWO)) player_switch_slot(1);
    if (IsKeyPressed(KEY_Q)) player_switch_slot(1 - g_player.cur_slot);
    if (IsKeyPressed(KEY_V) || IsKeyPressed(KEY_F3)) player_knife();
    if (IsKeyPressed(KEY_F) || IsKeyPressed(KEY_E)) player_try_interact();
    if (IsKeyPressed(KEY_G)) player_throw_grenade();

    player_update_camera(dt);
}

// ---------------------------------------------------------------------------
// Viewmodel (procedural, box-built; silhouette varies by weapon class)
// Drawn in CAMERA space (rotates with the view) via a shared unit cube mesh,
// so the weapon stays locked to the screen like a proper FPS viewmodel.
// ---------------------------------------------------------------------------
static Mesh s_vm_cube = { 0 };
static bool s_vm_ready = false;
static Material s_vm_mat = { 0 };

static void vm_ensure(void)
{
    if (s_vm_ready) return;
    static Vector3 verts[24], norms[24];
    static Vector2 uvs[24];
    static unsigned short idx[36];
    struct { Vector3 n; Vector3 a, b, c, d; } faces[6] = {
        { {0,0, 1}, {-0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f, 0.5f}, { 0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f} },
        { {0,0,-1}, { 0.5f,-0.5f,-0.5f}, {-0.5f,-0.5f,-0.5f}, {-0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f,-0.5f} },
        { { 1,0,0}, { 0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f,-0.5f}, { 0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f, 0.5f} },
        { {-1,0,0}, {-0.5f,-0.5f,-0.5f}, {-0.5f,-0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f,-0.5f} },
        { {0, 1,0}, {-0.5f, 0.5f, 0.5f}, { 0.5f, 0.5f, 0.5f}, { 0.5f, 0.5f,-0.5f}, {-0.5f, 0.5f,-0.5f} },
        { {0,-1,0}, {-0.5f,-0.5f,-0.5f}, { 0.5f,-0.5f,-0.5f}, { 0.5f,-0.5f, 0.5f}, {-0.5f,-0.5f, 0.5f} },
    };
    for (int i = 0; i < 6; i++)
    {
        Vector3 *v = &verts[i * 4];
        v[0] = faces[i].a; v[1] = faces[i].b; v[2] = faces[i].c; v[3] = faces[i].d;
        for (int j = 0; j < 4; j++)
        {
            norms[i * 4 + j] = faces[i].n;
            uvs[i * 4 + j] = (Vector2){ (j == 1 || j == 2) ? 1.0f : 0.0f,
                                        (j >= 2) ? 1.0f : 0.0f };
        }
        unsigned short *ix = &idx[i * 6];
        ix[0] = i * 4 + 0; ix[1] = i * 4 + 1; ix[2] = i * 4 + 2;
        ix[3] = i * 4 + 0; ix[4] = i * 4 + 2; ix[5] = i * 4 + 3;
    }
    Mesh m = { 0 };
    m.vertexCount = 24;
    m.triangleCount = 12;
    m.vertices = RL_MALLOC(24 * sizeof(Vector3));
    m.normals = RL_MALLOC(24 * sizeof(Vector3));
    m.texcoords = RL_MALLOC(24 * sizeof(Vector2));
    m.indices = RL_MALLOC(36 * sizeof(unsigned short));
    memcpy(m.vertices, verts, sizeof(verts));
    memcpy(m.normals, norms, sizeof(norms));
    memcpy(m.texcoords, uvs, sizeof(uvs));
    memcpy(m.indices, idx, sizeof(idx));
    UploadMesh(&m, false);
    s_vm_cube = m;

    s_vm_mat = LoadMaterialDefault();   // diffuse = white texture -> tint shows as solid colour
    s_vm_ready = true;
}

// draw a box in camera space: `at` is right/down/forward from the eye,
// `size` its extents; the box orientation follows the camera basis
static void vm_box(Vector3 at, Vector3 size, Color tint)
{
    vm_ensure();

    Vector3 f = Vector3Normalize(Vector3Subtract(g_camera.target, g_camera.position));
    Vector3 r = Vector3Normalize(Vector3CrossProduct(f, (Vector3){ 0, 1, 0 }));
    Vector3 u = Vector3CrossProduct(r, f);

    // world-space center of the box: eye + right/down/forward offsets
    Vector3 c = Vector3Add(g_camera.position, (Vector3){
        r.x * at.x + u.x * at.y + f.x * at.z,
        r.y * at.x + u.y * at.y + f.y * at.z,
        r.z * at.x + u.z * at.y + f.z * at.z });

    // right-handed basis [r, u, -f]: det = +1. A [r, u, f] basis mirrors the
    // geometry (det -1), and with raylib's global backface culling on,
    // a mirrored box is culled away entirely - this is what made the
    // viewmodel invisible for so long.
    Matrix b;
    b.m0 = r.x;  b.m4 = u.x;  b.m8  = -f.x;  b.m12 = 0;
    b.m1 = r.y;  b.m5 = u.y;  b.m9  = -f.y;  b.m13 = 0;
    b.m2 = r.z;  b.m6 = u.z;  b.m10 = -f.z;  b.m14 = 0;
    b.m3 = 0;    b.m7 = 0;    b.m11 = 0;     b.m15 = 1;

    // NOTE: raymath's MatrixMultiply(A, B) applies A FIRST, then B (same
    // nesting as the zombie part drawing). So this reads: scale the unit
    // cube, rotate it into the camera frame, then move it to its world
    // position. Translating first (the old code) rotates the eye position
    // itself and throws the box into nonsense space.
    Matrix mat = MatrixMultiply(MatrixMultiply(MatrixScale(size.x, size.y, size.z), b),
                                MatrixTranslate(c.x, c.y, c.z));

    s_vm_mat.maps[MATERIAL_MAP_DIFFUSE].color = tint;
    DrawMesh(s_vm_cube, s_vm_mat, mat);
}

void player_draw_viewmodel(float dt)
{
    (void)dt;
    const WeaponDef *def = weapons_get(g_player.slots[g_player.cur_slot].def);
    if (!def) return;

    WeaponSlot *s = &g_player.slots[g_player.cur_slot];

    // hip vs ads position
    float ads_t = g_player.ads ? 1.0f : 0.0f;
    float right = lerpf(0.28f, 0.10f, ads_t);
    float down  = lerpf(-0.26f, -0.16f, ads_t);
    float fwd   = lerpf(0.55f, 0.62f, ads_t);

    // walk bob & recoil pushback
    float bobx = sinf(g_player.bob_t) * 0.012f;
    float boby = fabsf(cosf(g_player.bob_t)) * 0.010f;
    fwd -= g_player.recoil * 0.6f;
    down += boby;
    right += bobx;

    // reload dip
    if (s->reload_t > 0) down -= 0.16f * (s->reload_t / fmaxf(def->reload_time, 0.01f));
    if (s->switch_t > 0) down -= 0.20f * (s->switch_t / 0.35f);

    Color tint = def->tint;
    Color dark = (Color){ tint.r / 2, tint.g / 2, tint.b / 2, 255 };
    Color wood = (Color){ 110, 72, 44, 255 };

    // gun body
    vm_box((Vector3){ right, down, fwd }, (Vector3){ 0.09f, 0.12f, def->vm_len }, tint);
    // barrel
    vm_box((Vector3){ right, down + 0.02f, fwd + def->vm_len * 0.5f },
           (Vector3){ def->vm_barrel, def->vm_barrel, def->vm_len * 0.7f }, dark);

    // class-specific dressing
    switch (def->cls)
    {
        case WCLASS_PISTOL:
            vm_box((Vector3){ right, down - 0.09f, fwd - 0.12f }, (Vector3){ 0.05f, 0.14f, 0.06f }, dark);
            break;
        case WCLASS_SMG:
            vm_box((Vector3){ right, down - 0.10f, fwd - 0.02f }, (Vector3){ 0.05f, 0.18f, 0.06f }, dark);
            break;
        case WCLASS_RIFLE:
        case WCLASS_SNIPER:
            vm_box((Vector3){ right, down - 0.07f, fwd - 0.25f }, (Vector3){ 0.06f, 0.10f, 0.30f }, wood);
            if (def->cls == WCLASS_SNIPER)
                vm_box((Vector3){ right, down + 0.09f, fwd + 0.05f }, (Vector3){ 0.05f, 0.05f, 0.16f }, dark);
            break;
        case WCLASS_SHOTGUN:
            vm_box((Vector3){ right, down - 0.06f, fwd + def->vm_len * 0.4f },
                   (Vector3){ def->vm_barrel * 1.6f, def->vm_barrel * 1.6f, 0.3f }, dark);
            vm_box((Vector3){ right, down - 0.07f, fwd - 0.25f }, (Vector3){ 0.06f, 0.10f, 0.30f }, wood);
            break;
        case WCLASS_LMG:
            vm_box((Vector3){ right, down - 0.12f, fwd }, (Vector3){ 0.14f, 0.14f, 0.18f }, dark);
            break;
        case WCLASS_SPECIAL:
        {
            Color glow = (Color){ 90, 220, 130, 255 };
            vm_box((Vector3){ right, down + 0.03f, fwd + 0.1f }, (Vector3){ 0.12f, 0.06f, 0.25f }, glow);
        }
            break;
        default: break;
    }

    // hands (kept inside the vertical FOV - bottom of the screen)
    vm_box((Vector3){ right - 0.02f, down - 0.06f, fwd + 0.05f }, (Vector3){ 0.08f, 0.08f, 0.14f },
           (Color){ 205, 170, 140, 255 });

    // knife slash: blade sweeps in from the right when knifing
    if (g_player.knife_t > 0)
    {
        float prog = 1.0f - g_player.knife_t / KNIFE_CD;   // 0 -> 1 over the swing
        float sweep = sinf(prog * PI);                      // fast in the middle
        float kx = 0.30f - 0.42f * sweep;
        float ky = -0.20f + 0.06f * sinf(prog * PI * 2.0f);
        float kz = 0.50f + 0.10f * sweep;
        // blade + guard + handle
        vm_box((Vector3){ kx, ky + 0.02f, kz + 0.18f }, (Vector3){ 0.035f, 0.06f, 0.30f },
               (Color){ 205, 208, 215, 255 });              // blade
        vm_box((Vector3){ kx, ky - 0.02f, kz + 0.02f }, (Vector3){ 0.09f, 0.03f, 0.06f },
               (Color){ 70, 65, 60, 255 });                 // guard
        vm_box((Vector3){ kx, ky - 0.02f, kz - 0.08f }, (Vector3){ 0.05f, 0.07f, 0.16f },
               (Color){ 45, 30, 20, 255 });                 // grip
    }
}
