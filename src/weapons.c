// ============================================================================
//  DEAD ARRIVAL - weapons.c
//  Shooting (hitscan + pellets + spread), reload, switching, knife melee,
//  low-poly 3D viewmodels drawn in a second camera pass, mystery box logic.
// ============================================================================
#include "game.h"

// ---------------------------------------------------------------------------
// Gun viewmodel models (OBJ from assets/guns/, built by tools/gen_guns.py)
// ---------------------------------------------------------------------------
static Model sGunModels[NUM_GUN_SLOTS_MAX];
static bool  sGunModelOk[NUM_GUN_SLOTS_MAX];

void GunModelsInit(void) {
    for (int i = 0; i < gGunCount; i++) {
        sGunModelOk[i] = false;
        char path[512];
        snprintf(path, sizeof path, "%s/guns/%s.obj", DataDir(), gWeapons[i].modelId);
        if (!FileExists(path)) {
            printf("guns: WARNING no model for %s (%s)\n", gWeapons[i].name, path);
            continue;
        }
        sGunModels[i] = LoadModel(path);
        sGunModelOk[i] = sGunModels[i].meshCount > 0;
        if (!sGunModelOk[i]) printf("guns: WARNING model failed to load: %s\n", path);
    }
}

void GunModelsUnload(void) {
    for (int i = 0; i < NUM_GUN_SLOTS_MAX; i++) {
        if (sGunModelOk[i]) UnloadModel(sGunModels[i]);
        sGunModelOk[i] = false;
    }
}

bool GunModelValid(int widx) { return widx >= 0 && widx < gGunCount && sGunModelOk[widx]; }

// ---------------------------------------------------------------------------
// Give / switch weapons
// ---------------------------------------------------------------------------
void WeaponGive(int widx, bool fromBox) {
    Player *p = &g.player;
    const WeaponDef *w = &gWeapons[widx];
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
    const WeaponDef *w = &gWeapons[widx];
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
    const WeaponDef *w = &gWeapons[widx];
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
    const WeaponDef *w = &gWeapons[widx];
    WeaponSlot *s = &p->weapons[p->curSlot];
    float dmg = (float)w->damage;
    // Pack-a-Punch: 2.5x damage, enough to keep pace past round 10's
    // sharp health ramp
    if (s->upgraded) dmg *= 2.5f;

    // sound category
    int snd = SND_SHOT_LIGHT;
    if (w->mode == FIRE_BOLT || (w->pellets <= 1 && w->damage >= 240)) snd = SND_SHOT_SNIPER;
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
        FireBullet(origin, dir, dmg);
    }
    s->magAmmo--;
    p->recoilKick = LERP(p->recoilKick, w->recoil * 0.05f, 1.0f);
    g.shakeTime = 0.06f; g.shakeAmp = w->recoil * 0.012f;
}

static void StartReload(void) {
    Player *p = &g.player;
    int widx = p->weapons[p->curSlot].widx;
    if (widx < 0) return;
    const WeaponDef *w = &gWeapons[widx];
    WeaponSlot *s = &p->weapons[p->curSlot];
    if (w->mode == FIRE_MELEE) return;
    if (p->reloadTimer > 0) return;
    if (s->magAmmo >= w->mag || s->reserveAmmo <= 0) return;
    float t = w->reloadTime;
    if (PlayerHasPerk(PerkSwiftIndex())) t *= 0.6f;
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
        ZombieDamage(best, (float)gWeapons[GunKnifeIndex()].damage, hp, true);
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
    if (ActPressed(ACT_WEP1))   { if (p->weapons[0].widx >= 0) { p->curSlot = 0; p->raiseTimer = 0.3f; p->reloadTimer = 0; } }
    if (ActPressed(ACT_WEP2))   { if (p->weapons[1].widx >= 0) { p->curSlot = 1; p->raiseTimer = 0.3f; p->reloadTimer = 0; } }
    if (ActPressed(ACT_WEP3) || IsKeyPressed(KEY_V)) { p->curSlot = SLOT_KNIFE; p->raiseTimer = 0.25f; p->reloadTimer = 0; }
    if (ActPressed(ACT_SWAP)) {
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
    const WeaponDef *w = &gWeapons[widx];
    WeaponSlot *s = &p->weapons[p->curSlot];

    // ADS blend
    bool wantAds = IsMouseButtonDown(MOUSE_BUTTON_RIGHT) && w->mode != FIRE_MELEE && p->knifeTimer <= 0;
    p->adsBlend = CLAMP(p->adsBlend + (float)g.dt * (wantAds ? 7.0f : -9.0f), 0, 1);

    // timers
    float rpm = (float)w->rpm;
    if (PlayerHasPerk(PerkRapidIndex())) rpm *= 1.25f;
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
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || ActPressed(ACT_KNIFE)) KnifeAttack();
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
    if (ActPressed(ACT_RELOAD)) StartReload();
}

// ---------------------------------------------------------------------------
// Viewmodel drawing: the OBJ model anchored to the camera basis
// (same sway/bob/reload/raise animation the old box-drawn guns had)
// ---------------------------------------------------------------------------
void WeaponDrawViewmodel(Camera3D cam) {
    Player *p = &g.player;
    if (p->downed) return;
    int widx = p->weapons[p->curSlot].widx;
    if (widx < 0) return;
    const WeaponDef *w = &gWeapons[widx];

    // sway from movement + bob
    float bobX = sinf(p->bobPhase) * 0.012f * (1.0f - p->adsBlend);
    float bobY = fabsf(cosf(p->bobPhase)) * 0.010f * (1.0f - p->adsBlend);
    float ads = p->adsBlend;
    // base position: hip vs ads (model origin = grip, so the grip sits here)
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

    // gun-space basis from camera yaw/pitch (drawn through the world shader,
    // so the gun is lit like the world and keeps its exact shape at any angle)
    float yawR = p->yaw*DEG2RAD, pitR = p->pitch*DEG2RAD;
    Vector3 fwd = Vec3(-sinf(yawR)*cosf(pitR), sinf(pitR), -cosf(yawR)*cosf(pitR));
    Vector3 right = Vec3Cross(Vec3(0,1,0), fwd);
    if (Vec3Len(right) < 1e-4f) right = Vec3(1,0,0); else right = Vec3Norm(right);
    Vector3 up = Vec3Cross(fwd, right);
    Vector3 eye = Vec3(p->pos.x, p->pos.y + PLAYER_EYE, p->pos.z);

    Vector3 gripPos = Vec3Add(Vec3Add(Vec3Add(eye, Vec3Scale(fwd, pz)),
        Vec3Scale(right, px)), Vec3Scale(up, py));
    (void)cam;

    // muzzle flash light at the model's muzzle
    float mzLen = w->muzzleZ > 0 ? w->muzzleZ : (w->mode == FIRE_MELEE ? 0.3f : (0.6f + w->mag * 0.002f));
    Vector3 muzzleWorld = Vec3Add(gripPos, Vec3Scale(fwd, mzLen));
    if (g.muzzleFlash > 0) GfxSetFlash(muzzleWorld, g.muzzleFlash * 14.0f);

    if (!sGunModelOk[widx]) {
        // no model (shouldn't happen; selftest checks): tiny dark box so the
        // slot still reads as armed
        GfxDrawCubeBasis(gripPos, right, up, fwd, 0.05f, 0.07f, 0.30f,
                         (Color){ 52, 54, 58, 255 }, AssetsTex(TEX_METAL));
        return;
    }

    // knife swings around the grip; guns get a slight roll while sprinting
    float rotY = 0, rotZ = 0, rotX = 0;
    if (w->mode == FIRE_MELEE) {
        gripPos = Vec3Add(gripPos, Vec3Add(Vec3Scale(fwd, -swing*0.1f),
                          Vec3Add(Vec3Scale(right, -swing*0.12f), Vec3Scale(up, swing*0.10f))));
        rotZ = swing * -35.0f;
    } else if (p->sprintTime > 0.05f && ads < 0.3f) {
        rotZ = 12.0f; rotY = -8.0f;
    }

    // build the model transform from the basis (right/up/fwd map gun-space
    // x/y/z; +Z forward matches the generator's convention)
    Matrix mat;
    mat.m0 = right.x; mat.m4 = up.x; mat.m8 = fwd.x; mat.m12 = gripPos.x;
    mat.m1 = right.y; mat.m5 = up.y; mat.m9 = fwd.y; mat.m13 = gripPos.y;
    mat.m2 = right.z; mat.m6 = up.z; mat.m10 = fwd.z; mat.m14 = gripPos.z;
    mat.m3 = 0; mat.m7 = 0; mat.m11 = 0; mat.m15 = 1;
    // gun-space rotations (deg) around the grip
    if (rotZ != 0) mat = MatrixMultiply(MatrixRotate(fwd, rotZ*DEG2RAD), mat);
    if (rotY != 0) mat = MatrixMultiply(MatrixRotate(up, rotY*DEG2RAD), mat);
    if (rotX != 0) mat = MatrixMultiply(MatrixRotate(right, rotX*DEG2RAD), mat);

    // Skin tint: WHITE keeps the MTL colors, colored skins multiply them.
    // Materials are emitted in first-use order (body, dark, then accents);
    // tint by slot order matches that. All parts go through the shared
    // world-shader material so the gun is lit with the scene.
    const SkinDef *skin = SkinCurrent();
    Model model = sGunModels[widx];
    Material *gm = GfxScratchMaterial();
    for (int mi = 0; mi < model.meshCount; mi++) {
        // material index -> slot color (models map every part to one of
        // body/dark/accent/silver by part kind)
        Color t = skin->body;
        if (mi >= 1 && model.meshCount > 2) t = (mi == 1) ? skin->dark : skin->accent;
        else if (mi == 1) t = skin->dark;
        // OBJ loader gives each part its own material index in file order
        if (model.materials && mi < model.materialCount) {
            Color base = model.materials[mi].maps
                         ? model.materials[mi].maps[MATERIAL_MAP_DIFFUSE].color
                         : WHITE;
            // multiply base MTL color by the skin tint channel
            t = (Color){ (unsigned char)(base.r * t.r / 255),
                         (unsigned char)(base.g * t.g / 255),
                         (unsigned char)(base.b * t.b / 255), 255 };
        }
        gm->maps[MATERIAL_MAP_DIFFUSE].texture = AssetsTex(TEX_METAL);
        gm->maps[MATERIAL_MAP_DIFFUSE].color = t;
        GfxSetTint(t);
        DrawMesh(model.meshes[mi], *gm, mat);
    }
}

// ---------------------------------------------------------------------------
// Mystery box
// ---------------------------------------------------------------------------
static int BoxPickWeapon(void) {
    // weighted pick among boxWeight > 0 (knife excluded)
    int knife = GunKnifeIndex();
    int total = 0;
    for (int i = 0; i < gGunCount; i++)
        if (i != knife) total += gWeapons[i].boxWeight;
    int r = RngInt(total);
    for (int i = 0; i < gGunCount; i++) {
        if (i == knife) continue;
        r -= gWeapons[i].boxWeight;
        if (r < 0) return i;
    }
    for (int i = 0; i < gGunCount; i++)
        if (i != knife && gWeapons[i].boxWeight > 0) return i;
    return 0;
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
                        gWeapons[g.boxResultWeapon].accent, AssetsTex(TEX_METAL));
    }
}
