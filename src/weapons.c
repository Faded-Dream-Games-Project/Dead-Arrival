// ============================================================================
//  DEAD ARRIVAL - weapons.c
//  Shooting (hitscan + pellets + spread), reload, switching, knife melee,
//  low-poly 3D viewmodels drawn in a second camera pass, mystery box logic.
// ============================================================================
#include "game.h"

// ---------------------------------------------------------------------------
// Give / switch weapons
// ---------------------------------------------------------------------------
void WeaponGive(int widx, bool fromBox) {
    Player *p = &g.player;
    const WeaponDef *w = &WEAPONS[widx];
    // already owned -> refill
    for (int s = 0; s < 2; s++) {
        if (p->weapons[s].widx == widx) {
            p->weapons[s].magAmmo = w->mag;
            p->weapons[s].reserveAmmo = w->reserve;
            p->curSlot = s;
            p->raiseTimer = 0.35f;
            return;
        }
    }
    // into empty slot, else replace current (not knife)
    int slot = p->weapons[0].widx < 0 ? 0 : (p->weapons[1].widx < 0 ? 1 : p->curSlot);
    if (slot == SLOT_KNIFE) slot = 0;
    p->weapons[slot] = (WeaponSlot){ widx, w->mag, w->reserve };
    p->curSlot = slot;
    p->raiseTimer = 0.35f;
    p->reloadTimer = 0;
    if (fromBox) p->raiseTimer = 0.8f;
}

void WeaponRefillCurrent(void) {
    Player *p = &g.player;
    int widx = p->weapons[p->curSlot].widx;
    if (widx < 0) return;
    const WeaponDef *w = &WEAPONS[widx];
    WeaponSlot *s = &p->weapons[p->curSlot];
    if (s->reserveAmmo < w->reserve) s->reserveAmmo = w->reserve;
}

int WeaponCurrentIndex(void) {
    return g.player.weapons[g.player.curSlot].widx;
}

float WeaponSpreadCurrent(void) {
    Player *p = &g.player;
    int widx = p->weapons[p->curSlot].widx;
    if (widx < 0) return 0;
    const WeaponDef *w = &WEAPONS[widx];
    float base = LERP(w->spread, w->adsSpread, p->adsBlend);
    if (p->grounded) base *= 1.0f; else base *= 1.8f;
    return base;
}

// ---------------------------------------------------------------------------
// One bullet: ray vs zombies (capsules) and walls
// ---------------------------------------------------------------------------
static void FireBullet(Vector3 origin, Vector3 dir, float dmg) {
    Player *p = &g.player;
    p->shots++;
    // nearest zombie hit
    Zombie *best = NULL;
    float bestT = 1e9;
    Vector3 bestHit = Vec3(0,0,0);
    for (int i = 0; i < MAX_ZOMBIES; i++) {
        Zombie *z = &g.zombies[i];
        if (!z->alive || z->state == ZS_DEAD) continue;
        // capsule approx: body AABB from groundY..+1.8, radius 0.42
        AABB body = AABB_Make(Vec3(z->pos.x-0.42f, z->groundY + 0.05f, z->pos.z-0.42f),
                              Vec3(z->pos.x+0.42f, z->groundY + 1.8f, z->pos.z+0.42f));
        float t = AABB_RayHit(body, origin, dir, 100.0f);
        if (t >= 0 && t < bestT) { bestT = t; best = z; bestHit = Vec3Add(origin, Vec3Scale(dir, t)); }
    }
    // walls
    float wallT;
    bool wallHit = MapRayHitWalls(origin, dir, 100.0f, &wallT);
    if (best && (!wallHit || bestT < wallT)) {
        ZombieDamage(best, dmg, bestHit, false);
        p->hits++;
        g.hitMarker = 0.25f;
        // blood particles
        ParticlesBurst(bestHit, 6, (Color){130, 20, 25, 255}, 2.2f, 0.07f, 0.45f, true);
    } else if (wallHit) {
        Vector3 hp = Vec3Add(origin, Vec3Scale(dir, wallT));
        ParticlesBurst(hp, 4, (Color){200, 190, 160, 255}, 1.6f, 0.05f, 0.3f, true);
    }
}

static void DoShot(void) {
    Player *p = &g.player;
    int widx = p->weapons[p->curSlot].widx;
    const WeaponDef *w = &WEAPONS[widx];
    WeaponSlot *s = &p->weapons[p->curSlot];

    // sound category
    int snd = SND_SHOT_LIGHT;
    if (widx == W_L96A1 || widx == W_ENFIELD) snd = SND_SHOT_SNIPER;
    else if (w->pellets > 1) snd = SND_SHOT_SHOTGUN;
    else if (w->damage >= 44) snd = SND_SHOT_HEAVY;
    SfxPlay(snd, RngRange(0.94f, 1.06f), 1.0f);

    // muzzle position (viewmodel approx)
    float yawR = p->yaw*DEG2RAD, pitR = p->pitch*DEG2RAD;
    Vector3 fwd = Vec3(-sinf(yawR)*cosf(pitR), sinf(pitR), -cosf(yawR)*cosf(pitR));
    Vector3 right = Vec3(cosf(yawR), 0, -sinf(yawR));
    Vector3 origin = Vec3Add(Vec3Add(
        Vec3(p->pos.x, p->pos.y + PLAYER_EYE - 0.06f, p->pos.z),
        Vec3Scale(fwd, 0.5f)), Vec3Scale(right, 0.12f));
    g.muzzlePos = origin;
    g.muzzleFlash = 0.06f;

    float spread = WeaponSpreadCurrent();
    for (int i = 0; i < w->pellets; i++) {
        Vector3 dir = fwd;
        if (spread > 0) {
            float a = RngRange(0, PI*2), r = sqrtf(RngRange(0, 1)) * spread;
            Vector3 up = Vec3Cross(right, fwd);
            dir = Vec3Norm(Vec3Add(Vec3Add(dir, Vec3Scale(right, cosf(a)*r)), Vec3Scale(up, sinf(a)*r)));
        }
        FireBullet(origin, dir, (float)w->damage);
    }
    s->magAmmo--;
    p->recoilKick = LERP(p->recoilKick, w->recoil * 0.05f, 1.0f);
    g.shakeTime = 0.06f; g.shakeAmp = w->recoil * 0.012f;
}

static void StartReload(void) {
    Player *p = &g.player;
    int widx = p->weapons[p->curSlot].widx;
    if (widx < 0) return;
    const WeaponDef *w = &WEAPONS[widx];
    WeaponSlot *s = &p->weapons[p->curSlot];
    if (w->mode == FIRE_MELEE) return;
    if (p->reloadTimer > 0) return;
    if (s->magAmmo >= w->mag || s->reserveAmmo <= 0) return;
    float t = w->reloadTime;
    if (p->perks[PERK_SWIFT]) t *= 0.6f;
    p->reloadTimer = t;
    SfxPlay(SND_RELOAD, 1.0f, 0.7f);
}

// ---------------------------------------------------------------------------
// Knife swing
// ---------------------------------------------------------------------------
static void KnifeAttack(void) {
    Player *p = &g.player;
    if (p->knifeTimer > 0) return;
    p->knifeTimer = 0.45f;
    p->knifeHitDone = 0;
    SfxPlay(SND_KNIFE, RngRange(0.9f, 1.1f), 0.9f);
}

static void KnifeHitCheck(void) {
    Player *p = &g.player;
    if (p->knifeHitDone) return;
    if (p->knifeTimer > 0.30f) return;    // mid-swing contact window
    p->knifeHitDone = 1;
    float yawR = p->yaw*DEG2RAD, pitR = p->pitch*DEG2RAD;
    Vector3 fwd = Vec3(-sinf(yawR)*cosf(pitR), sinf(pitR), -cosf(yawR)*cosf(pitR));
    Vector3 origin = Vec3(p->pos.x, p->pos.y + PLAYER_EYE, p->pos.z);
    Zombie *best = NULL;
    float bestT = 2.6f;
    for (int i = 0; i < MAX_ZOMBIES; i++) {
        Zombie *z = &g.zombies[i];
        if (!z->alive || z->state == ZS_DEAD) continue;
        Vector3 zc = Vec3(z->pos.x, z->groundY + 1.0f, z->pos.z);
        Vector3 d = Vec3Sub(zc, origin);
        float dist = Vec3Len(d);
        if (dist > 2.6f) continue;
        float dot = Vec3Dot(Vec3Norm(d), fwd);
        if (dot < 0.55f) continue;
        if (dist < bestT) { bestT = dist; best = z; }
    }
    if (best) {
        Vector3 hp = Vec3Add(origin, Vec3Scale(fwd, bestT*0.8f));
        ZombieDamage(best, (float)WEAPONS[W_KNIFE].damage, hp, true);
        g.hitMarker = 0.25f;
        ParticlesBurst(hp, 8, (Color){140, 25, 30, 255}, 2.5f, 0.08f, 0.5f, true);
        g.shakeTime = 0.08f; g.shakeAmp = 0.15f;
    }
}

// ---------------------------------------------------------------------------
// Weapons update (input)
// ---------------------------------------------------------------------------
void WeaponsUpdate(void) {
    Player *p = &g.player;
    if (p->downed) return;

    // switching: 1/2/3, Q swap, mouse wheel
    if (IsKeyPressed(KEY_ONE))   { if (p->weapons[0].widx >= 0) { p->curSlot = 0; p->raiseTimer = 0.3f; p->reloadTimer = 0; } }
    if (IsKeyPressed(KEY_TWO))   { if (p->weapons[1].widx >= 0) { p->curSlot = 1; p->raiseTimer = 0.3f; p->reloadTimer = 0; } }
    if (IsKeyPressed(KEY_THREE) || IsKeyPressed(KEY_V)) { p->curSlot = SLOT_KNIFE; p->raiseTimer = 0.25f; p->reloadTimer = 0; }
    if (IsKeyPressed(KEY_Q)) {
        int other = p->curSlot == 0 ? 1 : 0;
        if (p->weapons[other].widx >= 0) { p->curSlot = other; p->raiseTimer = 0.3f; p->reloadTimer = 0; }
    }
    float wheel = GetMouseWheelMoveV().y;
    if (wheel != 0) {
        int order[3] = {0, 1, SLOT_KNIFE};
        int cur = 0;
        for (int i = 0; i < 3; i++) if (order[i] == p->curSlot) cur = i;
        for (int step = 0; step < 3; step++) {
            cur = (cur + (wheel > 0 ? 1 : -1) + 3) % 3;
            if (order[cur] == SLOT_KNIFE || p->weapons[order[cur]].widx >= 0) { p->curSlot = order[cur]; break; }
        }
        p->raiseTimer = 0.3f; p->reloadTimer = 0;
    }

    int widx = p->weapons[p->curSlot].widx;
    if (widx < 0) return;
    const WeaponDef *w = &WEAPONS[widx];
    WeaponSlot *s = &p->weapons[p->curSlot];

    // ADS blend
    bool wantAds = IsMouseButtonDown(MOUSE_BUTTON_RIGHT) && w->mode != FIRE_MELEE && p->knifeTimer <= 0;
    p->adsBlend = CLAMP(p->adsBlend + (float)g.dt * (wantAds ? 7.0f : -9.0f), 0, 1);

    // timers
    float rpm = (float)w->rpm;
    if (p->perks[PERK_RAPID]) rpm *= 1.25f;
    p->fireTimer -= (float)g.dt;
    if (p->raiseTimer > 0) p->raiseTimer -= (float)g.dt;
    if (p->knifeTimer > 0) {
        p->knifeTimer -= (float)g.dt;
        KnifeHitCheck();
    }
    if (p->reloadTimer > 0) {
        p->reloadTimer -= (float)g.dt;
        if (p->reloadTimer <= 0) {
            int need = w->mag - s->magAmmo;
            int take = need < s->reserveAmmo ? need : s->reserveAmmo;
            s->magAmmo += take;
            s->reserveAmmo -= take;
            p->reloadTimer = 0;
        }
    }
    p->recoilKick *= powf(0.001f, (float)g.dt);   // fast decay

    if (p->raiseTimer > 0) return;

    // fire input
    if (w->mode == FIRE_MELEE) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsKeyPressed(KEY_F)) KnifeAttack();
        return;
    }
    if (p->reloadTimer > 0) {
        // reload cancel by switching handled above
        return;
    }
    bool trig = false;
    switch (w->mode) {
        case FIRE_SEMI:
        case FIRE_BOLT:
        case FIRE_PUMP:
            trig = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
            break;
        case FIRE_AUTO:
            trig = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
            break;
        case FIRE_BURST:
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && p->burstShots == 0 && p->fireTimer <= 0) {
                p->burstShots = 3;
            }
            trig = false;
            break;
        default: break;
    }
    // burst handling
    if (p->burstShots > 0 && p->fireTimer <= 0 && s->magAmmo > 0) {
        DoShot();
        p->burstShots--;
        p->fireTimer = 60.0f / rpm;
        if (p->burstShots == 0) p->fireTimer = 60.0f / rpm * 2.2f;  // burst gap
        trig = false;
    }
    if (trig && p->fireTimer <= 0 && s->magAmmo > 0) {
        DoShot();
        if (s->magAmmo == 0 && s->reserveAmmo > 0) StartReload();
        // fire cooldown
        float cd = 60.0f / rpm;
        if (w->mode == FIRE_PUMP) cd += 0.25f;
        if (w->mode == FIRE_BOLT) cd += 0.45f;
        p->fireTimer = cd;
    } else if (trig && s->magAmmo <= 0) {
        if (s->reserveAmmo > 0) StartReload();
        else SfxPlay(SND_DRY, 1.0f, 0.6f);
    }
    // manual reload (R)
    if (IsKeyPressed(KEY_R)) StartReload();
}

// ---------------------------------------------------------------------------
// Viewmodel drawing (low-poly boxes in view space, drawn after world)
// ---------------------------------------------------------------------------
void WeaponDrawViewmodel(Camera3D cam) {
    Player *p = &g.player;
    if (p->downed) return;
    int widx = p->weapons[p->curSlot].widx;
    if (widx < 0) return;
    const WeaponDef *w = &WEAPONS[widx];

    // sway from movement + bob
    float bobX = sinf(p->bobPhase) * 0.012f * (1.0f - p->adsBlend);
    float bobY = fabsf(cosf(p->bobPhase)) * 0.010f * (1.0f - p->adsBlend);
    float ads = p->adsBlend;
    // base position: hip vs ads
    float hipX = 0.22f, hipY = -0.20f, hipZ = 0.38f;
    float adsX = 0.0f,  adsY = -0.115f, adsZ = 0.26f;
    float px = LERP(hipX, adsX, ads) + bobX;
    float py = LERP(hipY, adsY, ads) + bobY - p->recoilKick*0.35f;
    float pz = LERP(hipZ, adsZ, ads) + p->recoilKick*0.9f;
    if (p->raiseTimer > 0) py -= p->raiseTimer * 0.9f;
    if (p->sprintTime > 0.05f && ads < 0.3f) { px += 0.05f; py -= 0.04f; }
    // reload dip
    if (p->reloadTimer > 0) {
        float k = 1.0f - fabsf((p->reloadTimer / w->reloadTime) - 0.5f)*2;
        py -= 0.16f * k;
        px += 0.03f * k;
    }
    // knife swing
    float swing = 0;
    if (p->knifeTimer > 0) {
        float t = 1.0f - p->knifeTimer / 0.45f;
        swing = sinf(t * PI) * 1.0f;
    }

    // We draw in world space anchored to camera to reuse the world shader.
    // Build gun-space basis from camera yaw/pitch.
    float yawR = p->yaw*DEG2RAD, pitR = p->pitch*DEG2RAD;
    Vector3 fwd = Vec3(-sinf(yawR)*cosf(pitR), sinf(pitR), -cosf(yawR)*cosf(pitR));
    Vector3 right = Vec3Cross(Vec3(0,1,0), fwd);
    if (Vec3Len(right) < 1e-4f) right = Vec3(1,0,0); else right = Vec3Norm(right);
    Vector3 up = Vec3Cross(fwd, right);
    Vector3 eye = Vec3(p->pos.x, p->pos.y + PLAYER_EYE, p->pos.z);
    // muzzle flash light
    Vector3 muzzleWorld = Vec3Add(Vec3Add(Vec3Add(eye, Vec3Scale(fwd, pz + w->bodyLen)),
        Vec3Scale(right, px)), Vec3Scale(up, py + 0.03f));
    if (g.muzzleFlash > 0) GfxSetFlash(muzzleWorld, g.muzzleFlash * 14.0f);
    (void)cam;

    // helper: draw gun part in gun-space (x right, y up, z forward) -> world
    #define GUN_PART(cx, cy, cz, sx, sy, sz, tint) do { \
        Vector3 _lp = Vec3Add(Vec3Add(Vec3Add(eye, Vec3Scale(fwd, pz + (cz))), \
            Vec3Scale(right, px + (cx))), Vec3Scale(up, py + (cy))); \
        Vector3 _size = Vec3Add(Vec3Add(Vec3Scale(fwd, (sz)), Vec3Scale(right, (sx))), Vec3Scale(up, (sy))); \
        /* axis-aligned approx: build from 3 scale comps on axes */ \
        Vector3 _axisSize = Vec3(fabsf(_size.x)+fabsf(_size.y)*0+fabsf(_size.z)*0, \
                                 fabsf(_size.y)+fabsf(_size.x)*0+fabsf(_size.z)*0, \
                                 fabsf(_size.z)+fabsf(_size.x)*0+fabsf(_size.y)*0); \
        GfxDrawCubePart(_lp, _axisSize, Vec3(0,0,0), tint, AssetsTex(TEX_METAL)); \
    } while (0)

    Color gunmetal = (Color){ 52, 54, 58, 255 };
    Color dark     = (Color){ 34, 35, 38, 255 };
    Color accent   = w->accent;

    if (w->mode == FIRE_MELEE) {
        // knife: blade + handle, swung
        float kn = 0.30f + swing * 0.14f;
        Vector3 lp = Vec3Add(Vec3Add(Vec3Add(eye, Vec3Scale(fwd, pz+0.18f - swing*0.1f)),
            Vec3Scale(right, px + 0.04f - swing*0.12f)), Vec3Scale(up, py + 0.02f + swing*0.10f));
        // handle
        Vector3 hSize = Vec3Add(Vec3Add(Vec3Scale(right, 0.030f), Vec3Scale(up, 0.030f)), Vec3Scale(fwd, 0.11f));
        GfxDrawCubePart(lp, Vec3(fabsf(hSize.x), fabsf(hSize.y), fabsf(hSize.z)), Vec3(0,0,0), dark, AssetsTex(TEX_METAL));
        // blade
        Vector3 bp = Vec3Add(lp, Vec3Scale(fwd, 0.13f));
        Vector3 bSize = Vec3Add(Vec3Add(Vec3Scale(right, 0.012f), Vec3Scale(up, 0.045f)), Vec3Scale(fwd, kn));
        GfxDrawCubePart(bp, Vec3(fabsf(bSize.x), fabsf(bSize.y), fabsf(bSize.z)), Vec3(0,0,0),
                        (Color){200, 202, 208, 255}, AssetsTex(TEX_METAL));
        return;
    }

    // receiver
    GUN_PART(0, 0.01f, w->bodyLen*0.5f, w->bodyW, w->bodyH, w->bodyLen, gunmetal);
    // barrel
    GUN_PART(0, w->bodyH*0.25f, w->bodyLen + w->barrelLen*0.5f, w->barrelR*2, w->barrelR*2, w->barrelLen, dark);
    // front sight
    GUN_PART(0, w->bodyH*0.5f + w->sightH*0.5f, w->bodyLen + w->barrelLen*0.92f, 0.012f, w->sightH, 0.014f, dark);
    // rear sight
    GUN_PART(0, w->bodyH*0.5f + w->sightH*0.4f, 0.02f, 0.030f, w->sightH*0.6f, 0.012f, dark);
    // magazine
    if (w->magH > 0) GUN_PART(0, -w->magH*0.5f - 0.01f, w->bodyLen*0.42f, w->magW, w->magH, w->magW*2.2f, accent);
    // grip
    GUN_PART(0, -0.055f, w->bodyLen*0.16f, 0.032f, 0.09f, 0.042f, accent);
    // stock
    if (w->stockLen > 0) GUN_PART(0, -0.01f, -w->stockLen*0.5f + 0.01f, 0.036f, 0.062f, w->stockLen, accent);
    // pump for shotguns
    if (w->mode == FIRE_PUMP) GUN_PART(0, w->bodyH*0.1f, w->bodyLen + w->barrelLen*0.35f, 0.045f, 0.038f, 0.13f, accent);
    // scope for sniper
    if (w->adsFov < 40) GUN_PART(0, w->bodyH*0.55f + 0.03f, w->bodyLen*0.5f, 0.026f, 0.026f, 0.16f, dark);
    #undef GUN_PART
}

// ---------------------------------------------------------------------------
// Mystery box
// ---------------------------------------------------------------------------
static int BoxPickWeapon(void) {
    // weighted pick among boxWeight > 0
    int total = 0;
    for (int i = 0; i < NUM_GUNS; i++) total += WEAPONS[i].boxWeight;
    int r = RngInt(total);
    for (int i = 0; i < NUM_GUNS; i++) {
        r -= WEAPONS[i].boxWeight;
        if (r < 0) return i;
    }
    return W_M1911;
}

void BoxUpdate(void) {
    if (!g.boxInUse) return;
    if (g.boxResultWeapon < 0) {
        // spinning phase
        g.boxSpinTimer -= (float)g.dt;
        if (RngInt(8) == 0) SfxPlay(SND_BOX_SPIN, RngRange(0.8f, 1.3f), 0.4f);
        if (RngInt(4) == 0)
            ParticlesSpawn(Vec3(g.map.boxPos.x + RngRange(-0.4f, 0.4f), g.map.boxPos.y + 0.8f, g.map.boxPos.z + RngRange(-0.4f, 0.4f)),
                           Vec3(0, RngRange(0.4f, 1.2f), 0), (Color){255, 220, 130, 255}, 0.06f, 0.7f, false);
        if (g.boxSpinTimer <= 0) {
            g.boxResultWeapon = BoxPickWeapon();
            g.boxSpinTimer = 12.0f;    // window to grab it
            g.boxUses++;
            SfxPlay(SND_BOX_WIN, 1.0f, 1.0f);
        }
        return;
    }
    // reveal window: grab with E or timeout
    g.boxSpinTimer -= (float)g.dt;
    if (g.boxSpinTimer <= 0) { g.boxInUse = false; g.boxResultWeapon = -1; return; }
    if (g.ePressedThisFrame) {
        Player *p = &g.player;
        Vector3 eye = Vec3(p->pos.x, p->pos.y + PLAYER_EYE, p->pos.z);
        Vector3 d = Vec3Sub(g.map.boxPos, eye);
        if (Vec3Len(d) < 3.0f) {
            WeaponGive(g.boxResultWeapon, true);
            g.boxInUse = false;
            g.boxResultWeapon = -1;
            SfxPlay(SND_BUY, 1.0f, 0.9f);
        }
    }
}

void BoxDrawWorld(void) {
    // box body
    GfxDrawBox(AABB_Make(Vec3(g.map.boxPos.x-0.55f, g.map.boxPos.y, g.map.boxPos.z-0.4f),
                          Vec3(g.map.boxPos.x+0.55f, g.map.boxPos.y+0.75f, g.map.boxPos.z+0.4f)), TEX_BOX, WHITE);
    // glowing lid while spinning
    Color lid = (g.boxInUse) ? (Color){255, 220, 140, 255} : (Color){120, 80, 40, 255};
    GfxDrawBox(AABB_Make(Vec3(g.map.boxPos.x-0.55f, g.map.boxPos.y+0.75f, g.map.boxPos.z-0.4f),
                          Vec3(g.map.boxPos.x+0.55f, g.map.boxPos.y+0.83f, g.map.boxPos.z+0.4f)), TEX_BOX, lid);
    // rising weapon silhouette during reveal
    if (g.boxInUse && g.boxResultWeapon >= 0) {
        float t = CLAMP(1.0f - g.boxSpinTimer/12.0f, 0, 1);
        float rise = t < 0.15f ? t/0.15f : 1.0f;
        Vector3 bp = g.map.boxPos;
        GfxDrawCubePart(Vec3(bp.x, bp.y + 0.8f + rise*0.5f, bp.z), Vec3(0.10f, 0.35f, 0.10f), Vec3(0, g.time*90, 0),
                        WEAPONS[g.boxResultWeapon].accent, AssetsTex(TEX_METAL));
    }
}
