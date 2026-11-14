// ============================================================================
//  DEAD ARRIVAL - zombies.c
//  Round-based zombie AI: spawn from windows/graves, chase via nav-grid BFS,
//  attack in melee, slowly scale tougher each round. Points economy:
//  +10 per hit landed, +50 per kill.
// ============================================================================
#include "game.h"

// ---------------------------------------------------------------------------
// Round scaling (COD-zombies style, slow ramp)
// ---------------------------------------------------------------------------
float ZombieHpForRound(int round) {
    // R1 = 100 (knife 150 one-hits), R2 = 200 (knife needs exactly 2),
    // +100/round through R9, then +10%/round (slow COD-style ramp).
    if (round <= 1) return 100.0f;
    if (round == 2) return 200.0f;
    if (round <= 9) return 200.0f + (round - 2) * 100.0f;
    return 700.0f * powf(1.1f, (float)(round - 9));
}

float ZombieSpeedForRound(int round) {
    // base walk 0.95 m/s, +0.06/round capped; runners appear later
    float s = 0.95f + (round - 1) * 0.055f;
    return CLAMP(s, 0.9f, 2.6f);
}

int ZombieQuotaForRound(int round) {
    // slow ramp: ~6 zombies round 1, +~25%/round, cap at MAX_ZOMBIES alive
    int q = (int)(6.0f * powf(1.22f, (float)(round - 1)));
    return q > 120 ? 120 : q;
}

// ---------------------------------------------------------------------------
// Spawning
// ---------------------------------------------------------------------------
static bool SpawnActive(SpawnPoint *s) {
    if (s->doorId < 0) return true;
    if (s->doorId >= g.map.doorCount) return true;
    return g.map.doorOpen[s->doorId];
}

// A spawn is usable if it has ground and a walkable nav cell (inside map,
// headroom, not on a wall top). Checked per map in selftest too.
static bool SpawnUsable(SpawnPoint *s) {
    float gy = GroundHeightAt(s->pos.x, s->pos.z, s->pos.y + 6.0f);
    if (gy < -50) return false;
    Vector3 a = Vec3(s->pos.x, gy, s->pos.z);
    return NavWalkableLine(a, Vec3(a.x + 0.9f, gy, a.z)) ||
           NavWalkableLine(a, Vec3(a.x - 0.9f, gy, a.z)) ||
           NavWalkableLine(a, Vec3(a.x, gy, a.z + 0.9f)) ||
           NavWalkableLine(a, Vec3(a.x, gy, a.z - 0.9f));
}

static void SpawnZombie(void) {
    // pick a random active, usable spawn whose grave/window isn't crowded
    int candidates[MAX_SPAWNS];
    int n = 0;
    for (int i = 0; i < g.map.spawnCount; i++)
        if (SpawnActive(&g.map.spawns[i]) && SpawnUsable(&g.map.spawns[i])) candidates[n++] = i;
    if (n == 0) return;
    int tries = 6;
    int pick = -1;
    while (tries-- > 0) {
        int c = candidates[RngInt(n)];
        Vector3 pos = g.map.spawns[c].pos;
        bool crowded = false;
        for (int i = 0; i < MAX_ZOMBIES; i++) {
            Zombie *z = &g.zombies[i];
            if (!z->alive) continue;
            float dx = z->pos.x - pos.x, dz = z->pos.z - pos.z;
            if (dx*dx + dz*dz < 1.2f) crowded = true;
        }
        if (!crowded) { pick = c; break; }
    }
    if (pick < 0) pick = candidates[RngInt(n)];

    // find a free slot
    Zombie *z = NULL;
    for (int i = 0; i < MAX_ZOMBIES; i++) if (!g.zombies[i].alive) { z = &g.zombies[i]; break; }
    if (!z) return;

    SpawnPoint *sp = &g.map.spawns[pick];
    memset(z, 0, sizeof(Zombie));
    z->alive = true;
    z->state = sp->grave ? ZS_RISE : ZS_CHASE;
    z->stateTime = 0;
    z->pos = sp->pos;
    z->groundY = GroundHeightAt(sp->pos.x, sp->pos.z, sp->pos.y + 6.0f);
    if (z->groundY < -50) z->groundY = sp->pos.y;
    z->maxHp = ZombieHpForRound(g.round);
    z->hp = z->maxHp;
    z->speed = ZombieSpeedForRound(g.round) * RngRange(0.9f, 1.1f);
    z->variant = RngInt(4);
    z->animPhase = RngRange(0, PI*2);
    z->attackCd = 0;
    // runners: small chance from round 5+
    z->runner = (g.round >= 5 && RngInt(100) < 4 + g.round);
    if (z->runner) z->speed *= 1.9f;
    if (sp->grave) {
        // start below ground
        z->pos.y = z->groundY - 1.8f;
        SfxPlay(SND_ZRISE, RngRange(0.9f, 1.1f), 0.7f);
    }
    g.roundSpawned++;
}

// ---------------------------------------------------------------------------
// Damage
// ---------------------------------------------------------------------------
void ZombieDamage(Zombie *z, float dmg, Vector3 hitPos, bool melee) {
    if (!z->alive || z->state == ZS_DEAD) return;
    z->hp -= dmg;
    z->hitFlash = 0.12f;
    SfxPlay(melee ? SND_SQUISH : SND_HIT, RngRange(0.9f, 1.15f), 0.65f);
    (void)hitPos;
    if (z->hp <= 0) {
        z->state = ZS_DEAD;
        z->stateTime = 0;
        z->alive = false;
        AddPoints(POINTS_PER_KILL);
        g.player.kills++;
        g.roundKills++;
        g.hitMarker = 0.3f;
        SfxPlay(SND_SQUISH, RngRange(0.8f, 0.95f), 0.9f);
        ParticlesBurst(Vec3(z->pos.x, z->groundY + 1.0f, z->pos.z), 14, (Color){110, 18, 22, 255}, 2.6f, 0.09f, 0.7f, true);
    } else {
        AddPoints(POINTS_PER_HIT);
    }
}

int ZombiesAliveCount(void) {
    int n = 0;
    for (int i = 0; i < MAX_ZOMBIES; i++)
        if (g.zombies[i].alive) n++;
    return n;
}

// ---------------------------------------------------------------------------
// Round flow
// ---------------------------------------------------------------------------
void ZombiesInitRound(void) {
    g.roundQuota = ZombieQuotaForRound(g.round);
    g.roundKills = 0;
    g.roundSpawned = 0;
    g.spawnTimer = 2.0f;
}

static void RoundAdvance(void) {
    g.round++;
    ZombiesInitRound();
    g.roundBannerActive = true;
    g.roundBannerTime = 3.0f;
    SfxPlay(SND_ROUND_START, 1.0f, 0.9f);
    // Self Revive re-armable each new round (bought again from machine)
}

void ZombiesUpdate(void) {
    // intermission
    if (g.roundIntermission > 0) {
        g.roundIntermission -= (float)g.dt;
        if (g.roundIntermission <= 0) RoundAdvance();
        return;
    }
    // spawn pacing
    int alive = ZombiesAliveCount();
    if (g.roundKills >= g.roundQuota) {
        // round cleared
        g.roundIntermission = 6.0f;
        SfxPlay(SND_ROUND_END, 1.0f, 0.8f);
        return;
    }
    bool canSpawn = g.roundSpawned < g.roundQuota && alive < MAX_ZOMBIES - 1;
    if (canSpawn) {
        g.spawnTimer -= (float)g.dt;
        // spawn interval shrinks with round
        float interval = (float)fmax(1.6 - g.round * 0.05, 0.35);
        if (alive > 24) interval *= 1.6f;
        if (g.spawnTimer <= 0) {
            SpawnZombie();
            g.spawnTimer = interval * RngRange(0.75f, 1.25f);
        }
    }

    // zombie updates
    Player *p = &g.player;
    Vector3 target = Vec3(p->pos.x, p->pos.y, p->pos.z);
    for (int i = 0; i < MAX_ZOMBIES; i++) {
        Zombie *z = &g.zombies[i];
        if (!z->alive) continue;
        z->stateTime += (float)g.dt;
        if (z->hitFlash > 0) z->hitFlash -= (float)g.dt;

        switch (z->state) {
        case ZS_RISE: {
            float t = z->stateTime / 1.6f;
            if (t >= 1.0f) { z->state = ZS_CHASE; z->stateTime = 0; }
            z->pos.y = z->groundY - 1.8f * (1.0f - t);
            if (RngInt(30) == 0)
                ParticlesSpawn(Vec3(z->pos.x + RngRange(-0.3f,0.3f), z->groundY + 0.1f, z->pos.z + RngRange(-0.3f,0.3f)),
                               Vec3(0, RngRange(0.3f,0.9f), 0), (Color){90, 70, 50, 255}, 0.07f, 0.6f, true);
            break;
        }
        case ZS_CHASE: {
            // repath periodically
            z->pathTimer -= (float)g.dt;
            if (z->pathTimer <= 0) {
                z->pathTimer = RngRange(0.7f, 1.3f);
                Vector3 from = Vec3(z->pos.x, z->groundY, z->pos.z);
                if (!NavFind(from, target, z->path, &z->pathLen, 48)) {
                    // fallback: straight line
                    z->path[0] = target; z->pathLen = 1; z->pathIdx = 0;
                } else z->pathIdx = 0;
            }
            // move along path
            Vector3 seek = target;
            if (z->pathLen > 0 && z->pathIdx < z->pathLen) {
                seek = z->path[z->pathIdx];
                Vector3 d2 = Vec3Sub(seek, Vec3(z->pos.x, z->pos.y, z->pos.z));
                d2.y = 0;
                if (Vec3Len(d2) < 0.7f && z->pathIdx < z->pathLen-1) z->pathIdx++;
            }
            Vector3 dir = Vec3Sub(seek, Vec3(z->pos.x, z->pos.y, z->pos.z));
            dir.y = 0;
            float dist = Vec3Len(dir);
            // attack range?
            if (dist < 1.35f && fabsf((p->pos.y) - z->groundY) < 2.0f) {
                z->state = ZS_ATTACK; z->stateTime = 0; z->attackCd = 0;
                break;
            }
            if (dist > 0.01f) {
                dir = Vec3Norm(dir);
                z->yaw = atan2f(-dir.x, -dir.z) * RAD2DEG;
                Vector3 vel = Vec3Scale(dir, z->speed);
                float gy;
                Vector3 npos = z->pos;
                CollideMove(&npos, &vel, 0.36f, 1.75f, (float)g.dt, &gy);
                if (gy > -50) z->groundY = gy;
                z->pos = npos;
                // step down onto ground
                float g2 = GroundHeightAt(z->pos.x, z->pos.z, z->groundY + 0.3f);
                if (g2 > -50 && z->pos.y > g2 + 0.02f) z->pos.y = g2;
                z->animPhase += (float)g.dt * (z->runner ? 9.0f : 5.5f);
            }
            break;
        }
        case ZS_ATTACK: {
            z->attackCd -= (float)g.dt;
            Vector3 d = Vec3Sub(target, z->pos);
            d.y = 0;
            float dist = Vec3Len(d);
            if (dist > 1.7f) { z->state = ZS_CHASE; z->stateTime = 0; break; }
            if (z->attackCd <= 0) {
                z->attackCd = 1.1f;
                z->animPhase = 0;   // swing anim
                SfxPlay(SND_ZATTACK, RngRange(0.9f, 1.1f), 0.8f);
                if (dist < 1.6f) PlayerDamage(ZOMBIE_HIT_DMG);
            }
            z->yaw = atan2f(-d.x, -d.z) * RAD2DEG;
            z->animPhase += (float)g.dt * 4.0f;
            break;
        }
        default: break;
        }
        // groans
        if (RngInt(240) == 0)
            SfxPlay(SND_GROAN1 + RngInt(3), RngRange(0.85f, 1.2f),
                    CLAMP(1.6f - Vec3Len(Vec3Sub(target, z->pos)) * 0.06f, 0.0f, 0.55f));
    }
}

// ---------------------------------------------------------------------------
// Drawing: low-poly body parts, procedural shamble animation
// ---------------------------------------------------------------------------
void ZombiesDraw(Camera3D cam) {
    (void)cam;
    for (int i = 0; i < MAX_ZOMBIES; i++) {
        Zombie *z = &g.zombies[i];
        if (!z->alive) continue;
        float ph = z->animPhase;
        bool attacking = (z->state == ZS_ATTACK);
        float swing = attacking ? sinf(fminf(z->stateTime*6.0f, PI)) : 0;

        // variant tint colors (sickly skin + cloth)
        static const Color skins[4] = {
            {138, 148, 118, 255}, {150, 140, 110, 255},
            {120, 135, 125, 255}, {160, 150, 130, 255},
        };
        static const Color clothes[4] = {
            { 70,  62,  55, 255}, { 58,  66,  78, 255},
            { 84,  54,  50, 255}, { 62,  70,  58, 255},
        };
        Color skin = skins[z->variant];
        Color cloth = clothes[z->variant];
        if (z->hitFlash > 0) { skin = (Color){255, 90, 80, 255}; cloth = (Color){255, 120, 100, 255}; }
        if (z->runner) { skin = (Color){170, 120, 100, 255}; }

        Vector3 base = z->pos;
        float lean = z->state == ZS_RISE ? 0 : sinf(ph*0.5f) * 4.0f;  // sway
        // torso
        Vector3 torsoPos = Vec3(base.x, base.y + 1.05f, base.z);
        GfxDrawCubePart(torsoPos, Vec3(0.52f, 0.62f, 0.30f), Vec3(lean, z->yaw, 0), cloth, AssetsTex(TEX_ZOMBIE));
        // head
        Vector3 headPos = Vec3(base.x, base.y + 1.55f, base.z);
        GfxDrawCubePart(headPos, Vec3(0.30f, 0.32f, 0.30f), Vec3(lean*0.5f, z->yaw, sinf(ph)*3.0f), skin, AssetsTex(TEX_ZOMBIE));
        // arms: raised forward when attacking, shamble otherwise
        for (int side = -1; side <= 1; side += 2) {
            float armSwing = attacking ? swing : sinf(ph + (side>0?0:PI)) * 0.4f;
            float ang = z->yaw + (attacking ? 0 : side * 8.0f);
            Vector3 armPos = Vec3(
                base.x + sinf(-(ang)*DEG2RAD) * -0.34f * cosf(armSwing*0.5f),
                base.y + 1.25f - armSwing*0.25f,
                base.z + cosf(-(ang)*DEG2RAD) * -0.34f * cosf(armSwing*0.5f));
            GfxDrawCubePart(armPos, Vec3(0.13f, 0.13f, 0.58f), Vec3(0, ang, 0), skin, AssetsTex(TEX_ZOMBIE));
        }
        // legs
        for (int side = -1; side <= 1; side += 2) {
            float legSwing = sinf(ph + (side>0?0:PI)) * 0.35f;
            Vector3 legPos = Vec3(base.x + side*0.14f, base.y + 0.36f - fabsf(legSwing)*0.06f, base.z + legSwing*0.18f);
            GfxDrawCubePart(legPos, Vec3(0.16f, 0.72f, 0.18f), Vec3(0, z->yaw, 0), cloth, AssetsTex(TEX_ZOMBIE));
        }
    }
}
