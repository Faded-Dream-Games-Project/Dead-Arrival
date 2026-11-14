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
    // data-driven curve (gameplay.json zombieHealth): R1 = base (100,
    // knife 150 one-hits), R2 = round2 (200, knife needs exactly 2),
    // +linearPerRound through linearUntil (R9 = 900), then the original
    // CoD Zombies sharp ramp: +growthPerRound compounding from there.
    if (round <= 1) return gGameplay.hpBase;
    if (round == 2) return gGameplay.hpRound2;
    if (round <= gGameplay.hpLinearUntil)
        return gGameplay.hpRound2 + (round - 2) * gGameplay.hpLinearPerRound;
    float atLinEnd = gGameplay.hpRound2 + (gGameplay.hpLinearUntil - 2) * gGameplay.hpLinearPerRound;
    return atLinEnd * powf(gGameplay.hpGrowth, (float)(round - gGameplay.hpLinearUntil));
}

float ZombieSpeedForRound(int round) {
    float s = gGameplay.speedBase + (round - 1) * gGameplay.speedPerRound;
    return CLAMP(s, gGameplay.speedMin, gGameplay.speedMax);
}

int ZombieQuotaForRound(int round) {
    if (ZombieIsDogRound(round)) {
        // dog rounds: small pack, fixed window (fast and frantic)
        int q = gGameplay.dogQuotaMin + round / 8;
        return CLAMP(q, gGameplay.dogQuotaMin, gGameplay.dogQuotaMax);
    }
    int q = (int)(gGameplay.quotaBase * powf(gGameplay.quotaGrowth, (float)(round - 1)));
    return q > gGameplay.quotaCap ? gGameplay.quotaCap : q;
}

bool ZombieIsDogRound(int round) {
    // every `dogEvery` rounds starting at dogFirstRound (gameplay.json)
    if (gGameplay.dogEvery <= 0 || round < gGameplay.dogFirstRound) return false;
    return (round - gGameplay.dogFirstRound) % gGameplay.dogEvery == 0;
}

// ---------------------------------------------------------------------------
// Spawning
// ---------------------------------------------------------------------------
static bool SpawnActive(SpawnPoint *s) {
    if (s->doorId < 0) return true;
    if (s->doorId >= g.map.doorCount) return true;
    return g.map.doorOpen[s->doorId];
}

// A spawn is usable if it has ground under its own feet (probe just above
// the spawn's y, never from far above: that would snap to an upper floor)
// and a walkable nav cell.
static bool SpawnUsable(SpawnPoint *s) {
    float gy = GroundHeightAt(s->pos.x, s->pos.z, s->pos.y + 0.6f);
    if (gy < -50 || fabsf(gy - s->pos.y) > 2.5f) {
        // fallback: old probe window (some spawns sit slightly high)
        gy = GroundHeightAt(s->pos.x, s->pos.z, s->pos.y + 6.0f);
        if (gy < -50) return false;
    }
    Vector3 a = Vec3(s->pos.x, gy, s->pos.z);
    return NavWalkableLine(a, Vec3(a.x + 0.9f, gy, a.z)) ||
           NavWalkableLine(a, Vec3(a.x - 0.9f, gy, a.z)) ||
           NavWalkableLine(a, Vec3(a.x, gy, a.z + 0.9f)) ||
           NavWalkableLine(a, Vec3(a.x, gy, a.z - 0.9f));
}

// True if a zombie standing at `from` can path to the player: the nav grid
// only connects through open doors, so this is the zone check. A spawn behind
// any buyable door (even one this spawn isn't tagged with) fails here.
bool ZombieSpawnReachable(Vector3 from, Vector3 playerPos) {
    Vector3 path[48]; int pathLen = 0;
    float gy = GroundHeightAt(from.x, from.z, from.y + 0.6f);
    if (gy < -50 || fabsf(gy - from.y) > 2.5f)
        gy = GroundHeightAt(from.x, from.z, from.y + 6.0f);
    if (gy < -50) gy = from.y;
    return NavFind(Vec3(from.x, gy, from.z), playerPos, path, &pathLen, 48);
}

// Stuck rescue: move a zombie to a random reachable spawn near the player's
// current zone so it can rejoin the fight instead of pounding a wall forever.
void ZombieRespawnNear(Zombie *z, Vector3 playerPos) {
    int candidates[MAX_SPAWNS];
    int n = 0;
    for (int i = 0; i < g.map.spawnCount; i++) {
        SpawnPoint *s = &g.map.spawns[i];
        if (!SpawnActive(s)) continue;
        // prefer spawns in the player's zone: within 18 m of the player
        float dx = s->pos.x - playerPos.x, dz = s->pos.z - playerPos.z;
        if (dx*dx + dz*dz > 18.0f*18.0f) continue;
        if (!ZombieSpawnReachable(s->pos, playerPos)) continue;
        candidates[n++] = i;
    }
    if (n == 0) {
        // widen: any active spawn near the player in x/z (same floor),
        // reachable or not - better than leaving the zombie stuck
        for (int i = 0; i < g.map.spawnCount; i++) {
            SpawnPoint *s = &g.map.spawns[i];
            if (!SpawnActive(s)) continue;
            float dx = s->pos.x - playerPos.x, dz = s->pos.z - playerPos.z;
            float dy = s->pos.y - playerPos.y;
            if (dy*dy > 9.0f) continue;   // stay on the player's floor
            if (dx*dx + dz*dz > 18.0f*18.0f) continue;
            candidates[n++] = i;
        }
    }
    if (n == 0) return;
    SpawnPoint *sp = &g.map.spawns[candidates[RngInt(n)]];
    z->pos = sp->pos;
    z->groundY = GroundHeightAt(sp->pos.x, sp->pos.z, sp->pos.y + 6.0f);
    if (z->groundY < -50) z->groundY = sp->pos.y;
    z->pos.y = z->groundY;
    z->pathTimer = 0;
    z->stuckTimer = 0;
    z->lastDist = Vec3Len(Vec3Sub(playerPos, z->pos));
    ParticlesBurst(Vec3(z->pos.x, z->groundY + 1.0f, z->pos.z), 10,
                   (Color){60, 50, 40, 255}, 2.0f, 0.08f, 0.5f, true);
}

static void SpawnZombie(void) {
    // pick a random active, usable spawn the player can actually be reached
    // from (never behind a closed door), whose grave/window isn't crowded
    Player *p = &g.player;
    Vector3 target = Vec3(p->pos.x, p->pos.y, p->pos.z);
    int candidates[MAX_SPAWNS];
    int n = 0;
    for (int i = 0; i < g.map.spawnCount; i++) {
        SpawnPoint *s = &g.map.spawns[i];
        if (!SpawnActive(s) || !SpawnUsable(s)) continue;
        if (!ZombieSpawnReachable(s->pos, target)) continue;
        candidates[n++] = i;
    }
    if (n == 0) {
        // no reachable spawn right now (e.g. player between floors): fall
        // back to any active usable spawn rather than freezing the round;
        // the stuck watchdog will relocate them near the player.
        for (int i = 0; i < g.map.spawnCount; i++) {
            SpawnPoint *s = &g.map.spawns[i];
            if (!SpawnActive(s) || !SpawnUsable(s)) continue;
            candidates[n++] = i;
        }
    }
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
    z->groundY = GroundHeightAt(sp->pos.x, sp->pos.z, sp->pos.y + 0.6f);
    if (z->groundY < -50 || fabsf(z->groundY - sp->pos.y) > 2.5f)
        z->groundY = GroundHeightAt(sp->pos.x, sp->pos.z, sp->pos.y + 6.0f);
    if (z->groundY < -50) z->groundY = sp->pos.y;
    z->dog = g.dogRound;
    if (z->dog) {
        z->maxHp = gGameplay.dogHp;
        z->speed = gGameplay.dogSpeed * RngRange(0.9f, 1.1f);
        z->runner = false;
    } else {
        z->maxHp = ZombieHpForRound(g.round);
        z->speed = ZombieSpeedForRound(g.round) * RngRange(0.9f, 1.1f);
        z->runner = (g.round >= gGameplay.runnerFromRound &&
                     RngInt(100) < gGameplay.runnerChanceBase + g.round);
        if (z->runner) z->speed *= 1.9f;
    }
    z->hp = z->maxHp;
    z->variant = RngInt(4);
    z->animPhase = RngRange(0, PI*2);
    z->attackCd = 0;
    z->lastDist = Vec3Len(Vec3Sub(target, z->pos));
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
        // max ammo drop: rare chance per kill; the last hellhound of a dog
        // round always drops one (the classic round reward)
        bool lastDog = z->dog && g.roundKills >= g.roundQuota;
        if (lastDog || RngInt(100) < gGameplay.maxAmmoChance)
            DropSpawn(Vec3(z->pos.x, z->groundY + 0.3f, z->pos.z), DROP_MAXAMMO);
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
    g.dogRound = ZombieIsDogRound(g.round);
    g.dogWarnTimer = g.dogRound ? 3.0f : 0;
}

static void RoundAdvance(void) {
    g.round++;
    ZombiesInitRound();
    g.roundBannerActive = true;
    g.roundBannerTime = 3.0f;
    SfxPlay(SND_ROUND_START, 1.0f, 0.9f);
    if (g.dogRound) {
        // warning growl + sky turns blood red for the dog round
        SfxPlay(SND_ZRISE, 0.6f, 1.0f);
        SfxPlay(SND_GROAN1, 0.55f, 1.0f);
    }
    // Self Revive re-armable each new round (bought again from machine)
}

void ZombiesUpdate(void) {
    // dog round warning phase: growl has sounded, dogs not yet spawning
    if (g.dogWarnTimer > 0) {
        g.dogWarnTimer -= (float)g.dt;
        if (RngInt(120) == 0) SfxPlay(SND_GROAN1 + RngInt(3), 0.5f, 0.8f);
        return;
    }
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
        g.roundIntermission = g.dogRound ? 3.0f : 6.0f;
        g.dogRound = false;   // sky back to normal during intermission
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
                    // no route: locked door between us and the player, or fell
                    // off the nav grid. Count it toward the stuck timer; the
                    // watchdog below respawns persistent offenders.
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
            // stuck watchdog: if the distance to the player hasn't shrunk for
            // 4 s straight (banging on a wall / locked out of the zone),
            // respawn at a valid spawn near the player's zone.
            {
                float d = Vec3Len(Vec3Sub(target, z->pos));
                if (d < z->lastDist - 0.05f) z->stuckTimer = 0;
                else z->stuckTimer += (float)g.dt;
                z->lastDist = d;
                if (z->stuckTimer > 4.0f) ZombieRespawnNear(z, target);
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
                if (dist < 1.6f) PlayerDamage(z->dog ? gGameplay.zombieHitDmg * 1.2f : gGameplay.zombieHitDmg);
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
static void DogDraw(Zombie *z) {
    float ph = z->animPhase;
    bool attacking = (z->state == ZS_ATTACK);
    // hellhound: long low body, four legs, snarling head, ember eyes
    Color fur  = (Color){ 38, 30, 30, 255 };
    Color ember = (Color){ 255, 120, 40, 255 };
    if (z->hitFlash > 0) fur = (Color){ 255, 90, 80, 255 };
    float run = fminf(ph, 1e9f);
    Vector3 base = z->pos;
    // body: long box along facing
    Vector3 fwd = Vec3(-sinf(z->yaw*DEG2RAD), 0, -cosf(z->yaw*DEG2RAD));
    Vector3 right = Vec3(-fwd.z, 0, fwd.x);
    Vector3 bodyPos = Vec3Add(base, Vec3Scale(fwd, 0.15f));
    GfxDrawCubePart(Vec3(bodyPos.x, base.y + 0.42f, bodyPos.z), Vec3(0.34f, 0.34f, 0.85f),
                    Vec3(0, z->yaw, 0), fur, AssetsTex(TEX_ZOMBIE));
    // head + snout
    Vector3 headPos = Vec3Add(base, Vec3Scale(fwd, 0.62f));
    GfxDrawCubePart(Vec3(headPos.x, base.y + 0.52f + sinf(run*2)*0.02f, headPos.z), Vec3(0.26f, 0.24f, 0.30f),
                    Vec3(attacking ? -12.0f : 0, z->yaw, 0), fur, AssetsTex(TEX_ZOMBIE));
    Vector3 snoutPos = Vec3Add(base, Vec3Scale(fwd, 0.80f));
    GfxDrawCubePart(Vec3(snoutPos.x, base.y + 0.47f, snoutPos.z), Vec3(0.12f, 0.12f, 0.16f),
                    Vec3(attacking ? -12.0f : 0, z->yaw, 0), fur, AssetsTex(TEX_ZOMBIE));
    // ember eyes
    for (int side = -1; side <= 1; side += 2) {
        Vector3 e = Vec3Add(Vec3Add(headPos, Vec3Scale(fwd, 0.14f)), Vec3Scale(right, 0.07f*side));
        GfxDrawCubePart(Vec3(e.x, base.y + 0.56f, e.z), Vec3(0.04f, 0.04f, 0.04f), Vec3(0,0,0), ember, AssetsTex(TEX_FLASH));
    }
    // tail
    Vector3 tailPos = Vec3Sub(base, Vec3Scale(fwd, 0.42f));
    GfxDrawCubePart(Vec3(tailPos.x, base.y + 0.50f, tailPos.z), Vec3(0.07f, 0.07f, 0.34f),
                    Vec3(0, z->yaw + sinf(ph*2)*8.0f, 0), fur, AssetsTex(TEX_ZOMBIE));
    // legs: gallop cycle
    for (int side = -1; side <= 1; side += 2) {
        for (int fr = 0; fr < 2; fr++) {
            float phase = ph + (side > 0 ? 0 : PI) + (fr ? PI*0.5f : 0);
            float swing = sinf(phase) * 0.16f;
            Vector3 leg = Vec3Add(Vec3Add(base, Vec3Scale(fwd, fr ? 0.32f : -0.28f)), Vec3Scale(right, 0.12f*side));
            GfxDrawCubePart(Vec3(leg.x + swing*0.5f, base.y + 0.17f, leg.z + swing*0.5f), Vec3(0.09f, 0.36f, 0.09f),
                            Vec3(0, z->yaw, 0), fur, AssetsTex(TEX_ZOMBIE));
        }
    }
    // fire trail while running
    if (z->state == ZS_CHASE && RngInt(3) == 0)
        ParticlesSpawn(Vec3(base.x + RngRange(-0.15f, 0.15f), base.y + 0.2f, base.z + RngRange(-0.15f, 0.15f)),
                       Vec3(RngRange(-0.2f, 0.2f), RngRange(0.4f, 1.0f), RngRange(-0.2f, 0.2f)),
                       (Color){ 255, RngInt(120) + 80, 30, 255 }, 0.06f, 0.4f, false);
}

void ZombiesDraw(Camera3D cam) {
    (void)cam;
    for (int i = 0; i < MAX_ZOMBIES; i++) {
        Zombie *z = &g.zombies[i];
        if (!z->alive) continue;
        if (z->dog) { DogDraw(z); continue; }
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


// ---------------------------------------------------------------------------
// Ground power-up drops (max ammo)
// ---------------------------------------------------------------------------
void DropSpawn(Vector3 pos, DropType type) {
    // reuse a free slot (oldest expires first)
    int slot = -1;
    for (int i = 0; i < MAX_DROPS; i++)
        if (!g.drops[i].alive) { slot = i; break; }
    if (slot < 0) {
        float minLife = 1e9;
        for (int i = 0; i < MAX_DROPS; i++)
            if (g.drops[i].life < minLife) { minLife = g.drops[i].life; slot = i; }
    }
    g.drops[slot] = (PowerDrop){ true, type, pos, gGameplay.maxAmmoLife, 0 };
    SfxPlay(SND_BOX_WIN, 1.3f, 0.5f);
}

void DropsUpdate(void) {
    Player *p = &g.player;
    for (int i = 0; i < MAX_DROPS; i++) {
        PowerDrop *d = &g.drops[i];
        if (!d->alive) continue;
        d->life -= (float)g.dt;
        d->spin += (float)g.dt * 90.0f;
        if (d->life <= 0) { d->alive = false; continue; }
        // pickup: walk within 1.2 m
        Vector3 dd = Vec3Sub(p->pos, d->pos);
        dd.y = 0;
        if (Vec3Len(dd) < 1.2f) {
            d->alive = false;
            if (d->type == DROP_MAXAMMO) {
                for (int s = 0; s < 2; s++) {
                    int wi = p->weapons[s].widx;
                    if (wi < 0) continue;
                    p->weapons[s].magAmmo = gWeapons[wi].mag;
                    p->weapons[s].reserveAmmo = gWeapons[wi].reserve;
                }
                g.dropPickupFlash = 1.0f;
                SfxPlay(SND_BUY, 0.8f, 1.0f);
                SfxPlay(SND_BOX_WIN, 1.0f, 0.9f);
                ParticlesBurst(Vec3(d->pos.x, d->pos.y + 0.6f, d->pos.z), 16,
                               (Color){ 120, 220, 255, 255 }, 2.5f, 0.07f, 0.6f, false);
            }
        }
    }
    if (g.dropPickupFlash > 0) g.dropPickupFlash -= (float)g.dt;
}

void DropsDraw(Camera3D cam) {
    (void)cam;
    for (int i = 0; i < MAX_DROPS; i++) {
        PowerDrop *d = &g.drops[i];
        if (!d->alive) continue;
        float bob = sinf((float)g.time * 3.0f + i) * 0.08f;
        float fade = d->life < 5.0f ? (sinf((float)g.time * 8.0f) > 0 ? 1.0f : 0.35f) : 1.0f;
        if (d->type == DROP_MAXAMMO) {
            // glowing ammo crate
            Vector3 c = Vec3(d->pos.x, d->pos.y + 0.45f + bob, d->pos.z);
            GfxDrawCubePart(c, Vec3(0.42f, 0.30f, 0.42f), Vec3(0, d->spin, 0),
                            (Color){ 60, 140, 200, (unsigned char)(235*fade) }, AssetsTex(TEX_BOX));
            GfxDrawCubePart(Vec3(c.x, c.y + 0.17f, c.z), Vec3(0.30f, 0.06f, 0.30f), Vec3(0, d->spin, 0),
                            (Color){ 150, 240, 255, (unsigned char)(235*fade) }, AssetsTex(TEX_GLASS));
            ParticlesSpawn(Vec3(d->pos.x + RngRange(-0.2f, 0.2f), d->pos.y + 0.2f, d->pos.z + RngRange(-0.2f, 0.2f)),
                           Vec3(0, RngRange(0.3f, 0.8f), 0), (Color){ 120, 220, 255, 255 }, 0.05f, 0.5f, false);
        }
    }
}
