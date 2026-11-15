// zombies.c - round flow, spawning (barriers / graves / drop-from-above),
// zombie AI (steering + step-up + gravity), attack and death handling.
#include "game.h"

static Zombie g_zombies[MAX_ZOMBIES];
static int  s_to_spawn = 0;        // zombies left to spawn this round
static int  s_total_this_round = 0;
static int  s_spawned_total = 0;   // all-time this session (autotest stat)
static float s_spawn_timer = 0;

// one unit cube mesh shared by all body parts (scaled per part at draw time)
static Mesh s_zcube = { 0 };
static bool s_zcube_ready = false;
static Material s_zmat = { 0 };
static bool s_zmat_ready = false;

// ---------------------------------------------------------------------------
// Round tuning
// ---------------------------------------------------------------------------
int round_zombie_count(int round)
{
    int n = ROUND_SPAWN_BASE + (round - 1) * ROUND_SPAWN_PER_RND;
    if (round > 10) n += (round - 10) * 2;
    return (n > ROUND_MAX_TO_SPAWN) ? ROUND_MAX_TO_SPAWN : n;
}

float round_zombie_hp(int round)
{
    return 80.0f + 40.0f * round;
}

float round_zombie_speed(int round, bool *runner)
{
    float runner_chance = 0.0f;
    if (round >= 3) runner_chance = fminf(0.75f, 0.07f * (round - 2));
    *runner = (frand_range(0.0f, 1.0f) < runner_chance);
    if (*runner) return frand_range(2.9f, 3.6f);
    return fminf(1.9f, 0.9f + 0.06f * round);
}

float round_spawn_interval(int round)
{
    return fmaxf(0.45f, 2.3f - round * 0.12f);
}

// ---------------------------------------------------------------------------
// Reset / counts
// ---------------------------------------------------------------------------
void zombies_reset(void)
{
    memset(g_zombies, 0, sizeof(g_zombies));
    s_to_spawn = 0;
    s_total_this_round = 0;
    s_spawned_total = 0;
    s_spawn_timer = 1.5f;
}

int zombies_total_spawned(void) { return s_spawned_total; }

bool zombies_nearest(Vector3 to, Vector3 *out)
{
    float best = 1e9f;
    bool found = false;
    for (int i = 0; i < MAX_ZOMBIES; i++)
    {
        Zombie *z = &g_zombies[i];
        if (!z->active || z->state == ZSTATE_DYING) continue;
        Vector3 c = { z->pos.x, z->pos.y + 1.1f, z->pos.z };
        float d = Vector3Distance(c, to);
        if (d < best) { best = d; *out = c; found = true; }
    }
    return found;
}

int zombies_alive_count(void)
{
    int n = 0;
    for (int i = 0; i < MAX_ZOMBIES; i++)
        if (g_zombies[i].active && g_zombies[i].state != ZSTATE_DYING) n++;
    return n;
}

int zombies_pending_count(void) { return s_to_spawn; }

// pathtest support: position of the zombie in a given slot (false if gone)
bool zombies_slot_pos(int slot, Vector3 *out)
{
    if (slot < 0 || slot >= MAX_ZOMBIES) return false;
    Zombie *z = &g_zombies[slot];
    if (!z->active || z->state == ZSTATE_DYING) return false;
    *out = z->pos;
    return true;
}

// pathtest support: full state of a slot for debugging stalls
bool zombies_slot_debug(int slot, Vector3 *out, int *state, float *stuck_t,
                        float *detour_t)
{
    if (slot < 0 || slot >= MAX_ZOMBIES) return false;
    Zombie *z = &g_zombies[slot];
    if (!z->active) return false;
    *out = z->pos;
    *state = (int)z->state;
    *stuck_t = z->stuck_t;
    *detour_t = z->detour_t;
    return true;
}

// ---------------------------------------------------------------------------
// Spawning
// ---------------------------------------------------------------------------
Zombie *zombies_spawn(int idx, bool at_barrier)
{
    for (int i = 0; i < MAX_ZOMBIES; i++)
    {
        Zombie *z = &g_zombies[i];
        if (z->active) continue;

        memset(z, 0, sizeof(*z));
        z->active = true;
        s_spawned_total++;
        bool runner = false;
        z->speed = round_zombie_speed(g.round, &runner);
        z->runner = runner;
        z->maxhp = round_zombie_hp(g.round);
        z->hp = z->maxhp;
        z->attack_cd = 0.5f;
        z->at_barrier = -1;
        z->anim_t = frand_range(0, 6.0f);

        if (at_barrier && g.map.spawn_mode == SPAWN_BARRIERS)
        {
            // idx is the direct barrier index (picked in zombies_update_round)
            int bi = idx % g.map.barrier_count;
            Barrier *b = &g.map.barriers[bi];
            Vector3 out = barrier_outward(b);
            z->pos = (Vector3){ b->pos.x + out.x * 0.75f, b->pos.y, b->pos.z + out.z * 0.75f };
            z->state = ZSTATE_AT_BARRIER;
            z->at_barrier = bi;
        }
        else if (g.map.spawn_mode == SPAWN_GRAVES)
        {
            SpawnPoint *p = &g.map.spawns[idx % g.map.spawn_count];
            z->pos = p->pos;
            z->pos.y -= 1.7f;                    // underground, rising up
            z->state = ZSTATE_RISING;
            z->state_t = 0;
            sfx_play_at("zombie_rise", p->pos, 0.9f);
        }
        else // SPAWN_DROP
        {
            SpawnPoint *p = &g.map.spawns[idx % g.map.spawn_count];
            z->pos = p->pos;
            z->pos.y += 1.8f;                    // up in the air, falls in
            z->state = ZSTATE_FALLING;
            z->state_t = 0;
        }
        sfx_play_at("zombie_groan", z->pos, 0.7f);
        return z;
    }
    return NULL;
}

void zombies_update_round(float dt)
{
    // round in progress?
    if (g.round_active)
    {
        if (s_to_spawn > 0)
        {
            s_spawn_timer -= dt;
            if (s_spawn_timer <= 0 && zombies_alive_count() < ROUND_MAX_ACTIVE)
            {
                int pick = GetRandomValue(0, 0x7fffffff);
                if (g.map.spawn_mode == SPAWN_BARRIERS)
                {
                    // room-based spawning: strongly prefer barriers in the
                    // room the player is in; otherwise any visited (opened)
                    // room. Barriers outside all rooms are always eligible.
                    int cand[MAX_BARRIERS * 3];
                    int n = 0;
                    int proom = map_room_of(&g.map, g_player.pos);
                    if (proom >= 0) g.map.rooms[proom].visited = true;
                    for (int i = 0; i < g.map.barrier_count; i++)
                    {
                        int br = g.map.barriers[i].room;
                        if (br < 0) { cand[n++] = i; continue; }
                        if (br == proom) { cand[n++] = i; cand[n++] = i; cand[n++] = i; }
                        else if (g.map.rooms[br].visited) cand[n++] = i;
                    }
                    if (n == 0)
                        for (int i = 0; i < g.map.barrier_count; i++) cand[n++] = i;
                    zombies_spawn(cand[pick % n], true);
                }
                else
                    zombies_spawn(pick, false);
                s_to_spawn--;
                s_spawn_timer = round_spawn_interval(g.round);
            }
        }
        else if (zombies_alive_count() == 0)
        {
            // round cleared
            g.round_active = false;
            g.round_break_t = ROUND_BREAK_TIME;
            sfx_play("round_end", 0.8f, 1.0f);
        }
    }
    else
    {
        g.round_break_t -= dt;
        if (g.round_break_t <= 0)
        {
            g.round++;
            g.round_active = true;
            s_total_this_round = round_zombie_count(g.round);
            s_to_spawn = s_total_this_round;
            s_spawn_timer = 1.0f;
            sfx_play("round_start", 0.9f, 1.0f);
            TraceLog(LOG_INFO, "ROUND %d begins: %d zombies, %.0f hp",
                     g.round, s_total_this_round, round_zombie_hp(g.round));
        }
    }
}

// ---------------------------------------------------------------------------
// AI update
// ---------------------------------------------------------------------------
static void zombie_face(Zombie *z, Vector3 target)
{
    float dx = target.x - z->pos.x, dz = target.z - z->pos.z;
    z->yaw = atan2f(dx, dz);
}

void zombies_update(float dt, bool player_alive)
{
    // refresh the shared nav flow field from the player's position
    nav_update(&g.map, g_player.pos, dt);

    for (int i = 0; i < MAX_ZOMBIES; i++)
    {
        Zombie *z = &g_zombies[i];
        if (!z->active) continue;
        z->hit_flash = fmaxf(0, z->hit_flash - dt * 3.0f);

        // ---- dying ----
        if (z->state == ZSTATE_DYING)
        {
            z->state_t += dt;
            if (z->state_t > 0.9f) z->active = false;
            continue;
        }

        // random groans
        if (frand_range(0, 1) < dt * 0.12f)
            sfx_play_at("zombie_groan", z->pos, 0.55f);

        switch (z->state)
        {
            case ZSTATE_AT_BARRIER:
            {
                Barrier *b = &g.map.barriers[z->at_barrier];
                if (b->boards > 0)
                {
                    // stand outside the window and tear boards off
                    zombie_face(z, b->pos);
                    z->anim_t += dt * 2.2f;
                    b->tear_accum += ZOMBIE_TEARS_PER_S * dt;
                    if (b->tear_accum >= 1.0f)
                    {
                        b->tear_accum -= 1.0f;
                        b->boards--;
                        sfx_play_at("board_break", b->pos, 0.8f);
                        particles_spawn_debris(b->pos, 6);
                    }
                }
                else
                {
                    // opening clear: climb through (scripted, avoids pathing
                    // around the outside of the wall)
                    zombie_face(z, g_player.pos);
                    Vector3 out = barrier_outward(b);
                    Vector3 inside = { b->pos.x - out.x * 0.95f, b->pos.y, b->pos.z - out.z * 0.95f };
                    Vector3 d = Vector3Subtract(inside, z->pos);
                    float len = Vector3Length(d);
                    if (len < 0.3f)
                    {
                        z->state = ZSTATE_CHASE;
                        z->state_t = 0;
                    }
                    else
                    {
                        z->pos.x += (d.x / len) * z->speed * dt;
                        z->pos.z += (d.z / len) * z->speed * dt;
                        z->pos.y = b->pos.y;
                        z->anim_t += dt * 4.5f;
                    }
                }
            } break;

            case ZSTATE_RISING:
            {
                z->state_t += dt;
                z->pos.y += (1.7f / 1.4f) * dt;
                z->anim_t += dt;
                zombie_face(z, g_player.pos);
                if (z->state_t >= 1.4f)
                {
                    z->state = ZSTATE_CHASE;
                    z->state_t = 0;
                }
            } break;

            case ZSTATE_FALLING:
            {
                z->vel.y -= ZOMBIE_GRAVITY * dt;
                z->pos.y += z->vel.y * dt;
                bool og = false;
                // simple landing probe (reuse full move for XZ too)
                map_move_aabb(&g.map, &z->pos, &z->vel, dt, ZOMBIE_RADIUS,
                              ZOMBIE_HEIGHT, ZOMBIE_STEP_HEIGHT, &og);
                if (og)
                {
                    z->state = ZSTATE_CHASE;
                    z->state_t = 0;
                    sfx_play_at("zombie_land", z->pos, 0.8f);
                    particles_spawn_debris(z->pos, 4);
                }
            } break;

            case ZSTATE_CHASE:
            {
                if (!player_alive) break;
                z->anim_t += dt * (z->runner ? 9.0f : 4.5f);

                float dist = v3_dist_xz(z->pos, g_player.pos);
                if (dist < ZOMBIE_ATTACK_RANGE)
                {
                    z->state = ZSTATE_ATTACK;
                    z->state_t = 0;
                    break;
                }

                // ---- steering target ----
                // Straight at the player when close or with a clear line of
                // sight; otherwise follow the nav flow field, which routes
                // around walls, down trenches and through doorways instead
                // of grinding into the first obstacle.
                Vector3 tgt = g_player.pos;
                bool guided = false;
                if (dist > 3.0f)
                {
                    // "can I WALK straight to the player?" - cast the ray at
                    // step height, not eye height: zombies can see over an
                    // altar or crate they can never climb and would then
                    // beeline straight into it forever
                    Vector3 eye = { z->pos.x, z->pos.y + 0.55f, z->pos.z };
                    Vector3 chest = { g_player.pos.x, g_player.pos.y + 0.55f, g_player.pos.z };
                    Vector3 dv = Vector3Subtract(chest, eye);
                    float dl = Vector3Length(dv);
                    bool visible = (dl <= 0.01f);
                    if (!visible)
                    {
                        Ray ray = { eye, { dv.x / dl, dv.y / dl, dv.z / dl } };
                        RayCollision rc = map_raycast(&g.map, ray, dl);
                        visible = !rc.hit;
                    }
                    if (!visible)
                        guided = nav_steer(&g.map, z->pos, &tgt);
                }

                Vector3 dir = { tgt.x - z->pos.x, 0, tgt.z - z->pos.z };
                float len = sqrtf(dir.x * dir.x + dir.z * dir.z);
                if (len > 0.001f) { dir.x /= len; dir.z /= len; }

                // ---- anti-stuck ----
                // barely moved since last frame while chasing: after a
                // second of that, lunge in a random direction for a moment
                // (unsticks doorway / corner pile-ups)
                float moved = v3_dist_xz(z->pos, z->last_pos);
                z->last_pos = z->pos;
                if (moved < z->speed * dt * 0.3f)
                    z->stuck_t += dt;
                else
                    z->stuck_t = fmaxf(0.0f, z->stuck_t - dt * 2.0f);
                if (z->stuck_t > 1.0f)
                {
                    z->stuck_t = 0.0f;
                    z->detour_t = 0.9f;
                    z->detour_slide = false;
                    float a = frand_range(0.0f, 2.0f * PI);
                    z->detour_x = cosf(a);
                    z->detour_z = sinf(a);
                }
                if (z->detour_t > 0.0f)
                {
                    z->detour_t -= dt;
                    if (z->detour_slide)
                    {
                        // wall slide: go full speed along the wall toward
                        // the target instead of grinding into the face
                        dir.x = z->detour_x;
                        dir.z = z->detour_z;
                    }
                    else
                    {
                        dir.x += z->detour_x * 1.2f;
                        dir.z += z->detour_z * 1.2f;
                        len = sqrtf(dir.x * dir.x + dir.z * dir.z);
                        if (len > 0.001f) { dir.x /= len; dir.z /= len; }
                    }
                }

                // separation from other zombies: strong up close so the horde
                // fans out into an attack arc, weak at range so zombies
                // funnel through doorways as a column instead of jamming
                // shoulder-first into the walls beside the opening
                float sep_w = 0.8f * clampf(4.0f / fmaxf(dist, 0.5f), 0.25f, 1.0f);
                for (int j = 0; j < MAX_ZOMBIES; j++)
                {
                    if (j == i) continue;
                    Zombie *o = &g_zombies[j];
                    if (!o->active || o->state == ZSTATE_DYING) continue;
                    float dx = z->pos.x - o->pos.x, dz = z->pos.z - o->pos.z;
                    float d2 = dx * dx + dz * dz;
                    if (d2 > 0.0001f && d2 < 0.65f * 0.65f)
                    {
                        float d = sqrtf(d2);
                        dir.x += dx / d * sep_w;
                        dir.z += dz / d * sep_w;
                    }
                }
                len = sqrtf(dir.x * dir.x + dir.z * dir.z);
                if (len > 0.001f) { dir.x /= len; dir.z /= len; }

                // remember the direction we WANT to travel (for the
                // wall-contact check after the move)
                float want_x = dir.x, want_z = dir.z;

                z->vel.x = dir.x * z->speed;
                z->vel.z = dir.z * z->speed;
                z->vel.y -= ZOMBIE_GRAVITY * dt;
                bool og = false;
                map_move_aabb(&g.map, &z->pos, &z->vel, dt, ZOMBIE_RADIUS,
                              ZOMBIE_HEIGHT, ZOMBIE_STEP_HEIGHT, &og);

                // bumped a wall this frame? (map_move_aabb zeroes the
                // blocked axis) - start a short slide along the wall so the
                // zombie rounds corners and unwedges from door jambs at
                // full speed instead of grinding. The slide direction is
                // PROBED: nudge half a metre to either side and keep the
                // side from which the blocked axis is actually clear (a
                // corner-clipped body often only escapes one way).
                if (z->detour_t <= 0.0f)
                {
                    bool wall_x = (fabsf(z->vel.x) < 0.001f && fabsf(want_x) > 0.15f);
                    bool wall_z = (fabsf(z->vel.z) < 0.001f && fabsf(want_z) > 0.15f);
                    if (wall_x && !wall_z)
                    {
                        float step = (want_x > 0) ? 0.06f : -0.06f;
                        Vector3 pn = { z->pos.x + step, z->pos.y, z->pos.z + 0.50f };
                        Vector3 pp = { z->pos.x + step, z->pos.y, z->pos.z - 0.50f };
                        bool n_ok = map_body_fits(&g.map, pn, ZOMBIE_RADIUS, ZOMBIE_HEIGHT);
                        bool p_ok = map_body_fits(&g.map, pp, ZOMBIE_RADIUS, ZOMBIE_HEIGHT);
                        if (n_ok || p_ok)
                        {
                            z->detour_t = 0.45f;
                            z->detour_slide = true;
                            z->detour_x = 0.0f;
                            // prefer the side the target is on when both work
                            if (n_ok && p_ok)
                                z->detour_z = (tgt.z >= z->pos.z) ? 1.0f : -1.0f;
                            else z->detour_z = n_ok ? 1.0f : -1.0f;
                        }
                    }
                    else if (wall_z && !wall_x)
                    {
                        float step = (want_z > 0) ? 0.06f : -0.06f;
                        Vector3 pn = { z->pos.x + 0.50f, z->pos.y, z->pos.z + step };
                        Vector3 pp = { z->pos.x - 0.50f, z->pos.y, z->pos.z + step };
                        bool n_ok = map_body_fits(&g.map, pn, ZOMBIE_RADIUS, ZOMBIE_HEIGHT);
                        bool p_ok = map_body_fits(&g.map, pp, ZOMBIE_RADIUS, ZOMBIE_HEIGHT);
                        if (n_ok || p_ok)
                        {
                            z->detour_t = 0.45f;
                            z->detour_slide = true;
                            z->detour_z = 0.0f;
                            if (n_ok && p_ok)
                                z->detour_x = (tgt.x >= z->pos.x) ? 1.0f : -1.0f;
                            else z->detour_x = n_ok ? 1.0f : -1.0f;
                        }
                    }
                }

                // look where you are going; face the player up close
                if (dist < 4.0f || !guided) zombie_face(z, g_player.pos);
                else zombie_face(z, tgt);
            } break;

            case ZSTATE_ATTACK:
            {
                if (!player_alive) { z->state = ZSTATE_CHASE; break; }
                zombie_face(z, g_player.pos);
                z->anim_t += dt * 5.0f;
                z->attack_cd -= dt;
                float dist = v3_dist_xz(z->pos, g_player.pos);
                if (dist > ZOMBIE_ATTACK_RANGE * 1.25f)
                {
                    z->state = ZSTATE_CHASE;
                    break;
                }
                if (z->attack_cd <= 0)
                {
                    z->attack_cd = ZOMBIE_ATTACK_CD;
                    sfx_play_at("zombie_attack", z->pos, 0.8f);
                    if (dist <= ZOMBIE_ATTACK_RANGE + 0.2f)
                        player_damage(ZOMBIE_ATTACK_DMG);
                }
            } break;

            default: break;
        }
    }
}

// ---------------------------------------------------------------------------
// Combat
// ---------------------------------------------------------------------------
static BoundingBox zombie_body_bb(const Zombie *z)
{
    return (BoundingBox){
        (Vector3){ z->pos.x - ZOMBIE_RADIUS * 0.9f, z->pos.y + 0.05f, z->pos.z - ZOMBIE_RADIUS * 0.9f },
        (Vector3){ z->pos.x + ZOMBIE_RADIUS * 0.9f, z->pos.y + 1.42f, z->pos.z + ZOMBIE_RADIUS * 0.9f }
    };
}

static BoundingBox zombie_head_bb(const Zombie *z)
{
    return (BoundingBox){
        (Vector3){ z->pos.x - 0.17f, z->pos.y + 1.42f, z->pos.z - 0.17f },
        (Vector3){ z->pos.x + 0.17f, z->pos.y + 1.75f, z->pos.z + 0.17f }
    };
}

// grenade / explosion damage: falloff from the center, kills award points
void zombies_apply_explosion(Vector3 pos, float radius, float max_damage)
{
    for (int i = 0; i < MAX_ZOMBIES; i++)
    {
        Zombie *z = &g_zombies[i];
        if (!z->active || z->state == ZSTATE_DYING) continue;
        float d = Vector3Distance(z->pos, pos);
        if (d > radius) continue;
        float dmg = max_damage * (1.0f - d / radius);
        int killed = 0;
        Vector3 kpos = { 0 };
        zombies_damage(i, dmg, z->pos, false, &killed, &kpos);
    }
}

int zombies_raycast(Ray ray, float max_dist, ZombieHit *out, int max_out)
{
    int n = 0;
    for (int i = 0; i < MAX_ZOMBIES && n < max_out; i++)
    {
        Zombie *z = &g_zombies[i];
        if (!z->active || z->state == ZSTATE_DYING) continue;

        RayCollision h = GetRayCollisionBox(ray, zombie_head_bb(z));
        if (h.hit && h.distance < max_dist)
        {
            out[n].index = i; out[n].dist = h.distance; out[n].head = true; n++;
            continue;   // head supersedes body
        }
        h = GetRayCollisionBox(ray, zombie_body_bb(z));
        if (h.hit && h.distance < max_dist)
        {
            out[n].index = i; out[n].dist = h.distance; out[n].head = false; n++;
        }
    }

    // sort by distance (insertion sort, tiny n)
    for (int i = 1; i < n; i++)
    {
        ZombieHit key = out[i];
        int j = i - 1;
        while (j >= 0 && out[j].dist > key.dist) { out[j + 1] = out[j]; j--; }
        out[j + 1] = key;
    }
    return n;
}

void zombies_damage(int index, float dmg, Vector3 at, bool headshot,
                    int *killed_out, Vector3 *kill_pos)
{
    if (killed_out) *killed_out = 0;
    if (index < 0 || index >= MAX_ZOMBIES) return;
    Zombie *z = &g_zombies[index];
    if (!z->active || z->state == ZSTATE_DYING) return;

    z->hp -= dmg;
    z->hit_flash = 1.0f;
    particles_spawn_blood(at, headshot ? 10 : 6);
    sfx_play_at("zombie_hit", at, 0.6f);

    if (z->hp <= 0)
    {
        z->state = ZSTATE_DYING;
        z->state_t = 0;
        g_player.points += PTS_PER_KILL;
        g_player.kills++;
        if (killed_out) *killed_out = 1;
        if (kill_pos) *kill_pos = z->pos;
        particles_spawn_blood((Vector3){ z->pos.x, z->pos.y + 1.0f, z->pos.z }, 14);
        sfx_play_at("zombie_death", z->pos, 0.8f);
        if (headshot) sfx_play_at("headshot", at, 0.7f);
    }
}

// ---------------------------------------------------------------------------
// Rendering: box-built humanoid with classic arms-forward pose
// ---------------------------------------------------------------------------
static void ensure_zombie_materials(void)
{
    if (!s_zcube_ready)
    {
        // unit cube with 0..1 UVs per face
        Mesh m = { 0 };
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
        s_zcube = m;
        s_zcube_ready = true;
    }
    if (!s_zmat_ready)
    {
        s_zmat = LoadMaterialDefault();
        s_zmat.maps[MATERIAL_MAP_DIFFUSE].texture = *assets_tex("zombie_skin");
        s_zmat_ready = true;
    }
}

// draw one body part: cube scaled, positioned, rotated around the zombie
static void zpart(Vector3 center, Vector3 size, float yaw, float tint_flash,
                  Texture2D *tex)
{
    ensure_zombie_materials();
    s_zmat.maps[MATERIAL_MAP_DIFFUSE].texture = *tex;
    Color base = WHITE;
    Color flash = (Color){ 255, 120, 110, 255 };
    s_zmat.maps[MATERIAL_MAP_DIFFUSE].color = (Color){
        (unsigned char)lerpf(base.r, flash.r, tint_flash),
        (unsigned char)lerpf(base.g, flash.g, tint_flash),
        (unsigned char)lerpf(base.b, flash.b, tint_flash), 255 };

    Matrix mat = MatrixMultiply(MatrixRotateY(yaw), MatrixTranslate(center.x, center.y, center.z));
    // scale: build as scale * rotate * translate
    Matrix scl = MatrixScale(size.x, size.y, size.z);
    mat = MatrixMultiply(scl, mat);
    DrawMesh(s_zcube, s_zmat, mat);
}

void zombies_draw(void)
{
    Texture2D *skin = assets_tex("zombie_skin");
    Texture2D *cloth = assets_tex("zombie_cloth");

    for (int i = 0; i < MAX_ZOMBIES; i++)
    {
        Zombie *z = &g_zombies[i];
        if (!z->active) continue;

        float flash = z->hit_flash * 0.7f;
        float yaw = z->yaw;

        // dying: fall forward
        float fall = 0.0f;
        float sink = 0.0f;
        if (z->state == ZSTATE_DYING)
        {
            fall = fminf(1.0f, z->state_t / 0.9f);
            sink = fall * 0.4f;
        }
        // rising: clip below ground (cheap "emerging" effect)
        float rise_clip = 0.0f;
        if (z->state == ZSTATE_RISING)
            rise_clip = (1.0f - fminf(1.0f, z->state_t / 1.4f)) * 1.7f;

        float y0 = z->pos.y - rise_clip - sink;

        float swing = sinf(z->anim_t);
        float swing2 = sinf(z->anim_t + PI);

        // torso
        zpart((Vector3){ z->pos.x, y0 + 1.02f, z->pos.z },
              (Vector3){ 0.56f, 0.72f, 0.32f }, yaw, flash, cloth);

        // head (tilts when falling)
        float heady = y0 + 1.56f - fall * 0.5f;
        zpart((Vector3){ z->pos.x, heady, z->pos.z + fall * 0.45f },
              (Vector3){ 0.34f, 0.34f, 0.34f }, yaw, flash, skin);

        // arms: classic forward reach; swing while walking
        float fw = z->runner ? 0.55f : 0.30f + 0.08f * swing;
        if (z->state == ZSTATE_ATTACK) fw = 0.55f + 0.18f * sinf(z->anim_t * 3.0f);
        float sy = sinf(yaw), cy = cosf(yaw);
        // forward / right vectors in world space for arm placement
        Vector3 fwd = { sy, 0, cy };
        Vector3 rgt = { cy, 0, -sy };
        float arm_len = 0.62f;
        float arm_fwd = 0.10f + arm_len * 0.5f + fw;

        Vector3 armc = {
            z->pos.x + fwd.x * arm_fwd + rgt.x * 0.36f,
            y0 + 1.22f - fall * 0.6f,
            z->pos.z + fwd.z * arm_fwd + rgt.z * 0.36f };
        // arms oriented along the facing direction
        Matrix arm_rot = MatrixMultiply(
            MatrixScale(0.16f, 0.16f, arm_len),
            MatrixMultiply(MatrixRotateY(yaw), MatrixTranslate(armc.x, armc.y, armc.z)));
        s_zmat.maps[MATERIAL_MAP_DIFFUSE].texture = *skin;
        s_zmat.maps[MATERIAL_MAP_DIFFUSE].color = (Color){
            (unsigned char)lerpf(255, 255, flash),
            (unsigned char)lerpf(255, 120, flash),
            (unsigned char)lerpf(255, 110, flash), 255 };
        DrawMesh(s_zcube, s_zmat, arm_rot);

        Matrix arm_rot2 = MatrixMultiply(
            MatrixScale(0.16f, 0.16f, arm_len),
            MatrixMultiply(MatrixRotateY(yaw),
                MatrixTranslate(z->pos.x + fwd.x * arm_fwd - rgt.x * 0.36f,
                                y0 + 1.22f - fall * 0.6f,
                                z->pos.z + fwd.z * arm_fwd - rgt.z * 0.36f)));
        DrawMesh(s_zcube, s_zmat, arm_rot2);

        // legs (only above ground when rising/falling)
        float legy = y0 + 0.33f;
        zpart((Vector3){ z->pos.x + rgt.x * 0.14f + swing * 0.10f * fwd.x,
                         legy, z->pos.z + rgt.z * 0.14f + swing * 0.10f * fwd.z },
              (Vector3){ 0.20f, 0.66f, 0.22f }, yaw, flash, cloth);
        zpart((Vector3){ z->pos.x - rgt.x * 0.14f + swing2 * 0.10f * fwd.x,
                         legy, z->pos.z - rgt.z * 0.14f + swing2 * 0.10f * fwd.z },
              (Vector3){ 0.20f, 0.66f, 0.22f }, yaw, flash, cloth);
    }
}
