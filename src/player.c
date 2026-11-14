// ============================================================================
//  DEAD ARRIVAL - player.c
//  First-person player: WASD + mouse look, sprint, jump, crouch, head bob,
//  2-hit health (100 hp / 50 dmg), downed state + Self Revive, Regen Soda,
//  interactions (doors, wall buys, perks, mystery box, jump pads, ziplines).
// ============================================================================
#include "game.h"

static float sMouseAccumX = 0, sMouseAccumY = 0;

void PlayerSpawn(void) {
    Player *p = &g.player;
    memset(p, 0, sizeof(Player));
    p->pos = g.map.playerStart;
    p->yaw = g.map.playerYaw;
    p->hp = PLAYER_MAX_HP;
    p->points = POINTS_START;
    p->curSlot = 0;
    p->weapons[0] = (WeaponSlot){ W_M1911, WEAPONS[W_M1911].mag, WEAPONS[W_M1911].reserve };
    p->weapons[1] = (WeaponSlot){ -1, 0, 0 };
    p->weapons[2] = (WeaponSlot){ W_KNIFE, 0, 0 };
    p->raiseTimer = 0.4f;
}

void PlayerDamage(float dmg) {
    Player *p = &g.player;
    if (p->downed) return;
    p->hp -= dmg;
    p->lastHitTime = (float)g.time;
    g.damageFlash = 1.0f;
    g.shakeTime = 0.25f; g.shakeAmp = 0.35f;
    SfxPlay(SND_HURT, RngRange(0.9f, 1.1f), 0.9f);
    // Juggernaut Soda: shrug off the hit that would down you, up to the
    // perk's limit. ZOMBIE_HIT_DMG 50 vs 100 hp = 2 hits normally; Jugg
    // effectively makes it 4. The perk is consumed on the saves it provides
    // and re-buys like the others; hits reset after 10 s without one.
    if (p->hp <= 0 && p->perks[PERK_JUGG]) {
        p->hp = 1.0f;
        p->perks[PERK_JUGG] = false;
        g.roundBannerTime = 1.6f;
        g.roundBannerActive = true;
        SfxPlay(SND_PERK, 0.7f, 1.0f);
        ParticlesBurst(Vec3(p->pos.x, p->pos.y+1, p->pos.z), 18, PERKS[PERK_JUGG].color, 3.0f, 0.12f, 0.9f, false);
    }
    if (p->hp <= 0) {
        p->hp = 0;
        if (p->perks[PERK_REVIVE] && p->reviveUsedRound != g.round) {
            // Self Revive: consumed, get back up
            p->perks[PERK_REVIVE] = false;
            p->reviveUsedRound = g.round;
            p->hp = PLAYER_MAX_HP;
            p->downTimer = 0;
            g.roundBannerTime = 2.5f;
            g.roundBannerActive = true;
            p->revives++;
            SfxPlay(SND_PERK, 1.0f, 1.0f);
            ParticlesBurst(Vec3(p->pos.x, p->pos.y+1, p->pos.z), 24, PERKS[PERK_REVIVE].color, 3.0f, 0.12f, 1.0f, false);
        } else {
            p->downed = true;
            p->downTimer = BLEEDOUT_TIME;
            SfxPlay(SND_DOWN, 1.0f, 1.0f);
        }
    }
}

bool PlayerHasPerk(PerkType t) { return g.player.perks[t]; }

void AddPoints(int n) {
    g.player.points += n;
    if (n > 0) g.player.earned += n;
}

// ---------------------------------------------------------------------------
// Interaction scanning: returns a description of what's in front of the
// player, and performs the action if `doIt` is set.
// ---------------------------------------------------------------------------
typedef enum { IA_NONE = 0, IA_DOOR, IA_WALLBUY, IA_WALLBUY_AMMO, IA_PERK, IA_BOX, IA_ZIP,
               IA_POWER, IA_PAP } InteractType;

static InteractType ScanInteract(char *label, int labelSize, int *idxOut, bool doIt) {
    Player *p = &g.player;
    Vector3 eye = Vec3(p->pos.x, p->pos.y + PLAYER_EYE, p->pos.z);
    float yawR = p->yaw * DEG2RAD;
    Vector3 fwd = Vec3(sinf(yawR)*cosf(p->pitch*DEG2RAD), sinf(p->pitch*DEG2RAD), cosf(yawR)*cosf(p->pitch*DEG2RAD));
    // Actually raylib camera looks down -Z at yaw 0; our yaw convention: fwd = (-sin(yaw), ., -cos(yaw))
    fwd = Vec3(-sinf(yawR)*cosf(p->pitch*DEG2RAD), sinf(p->pitch*DEG2RAD), -cosf(yawR)*cosf(p->pitch*DEG2RAD));

    // Doors (proximity + facing)
    float bestScore = 0; InteractType best = IA_NONE; int bestIdx = -1;
    for (int i = 0; i < g.map.doorCount; i++) {
        if (g.map.doorOpen[i]) continue;
        Vector3 d = Vec3Sub(g.map.doors[i].center, eye);
        float dist = Vec3Len(d);
        if (dist > 3.2f) continue;
        float dot = Vec3Dot(Vec3Norm(d), fwd);
        if (dot < 0.5f) continue;
        float score = dot / (dist + 0.5f);
        if (score > bestScore) { bestScore = score; best = IA_DOOR; bestIdx = i; }
    }
    // Wall buys
    for (int i = 0; i < g.map.wallbuyCount; i++) {
        Vector3 d = Vec3Sub(g.map.wallbuys[i].pos, eye);
        float dist = Vec3Len(d);
        if (dist > 2.8f) continue;
        float dot = Vec3Dot(Vec3Norm(d), fwd);
        if (dot < 0.4f) continue;
        float score = dot / (dist + 0.5f);
        if (score > bestScore) { bestScore = score; best = IA_WALLBUY; bestIdx = i; }
    }
    // Perk stations
    for (int i = 0; i < g.map.perkCount; i++) {
        Vector3 pp = Vec3(g.map.perks[i].pos.x, g.map.perks[i].pos.y + 1.0f, g.map.perks[i].pos.z);
        Vector3 d = Vec3Sub(pp, eye);
        float dist = Vec3Len(d);
        if (dist > 2.8f) continue;
        float dot = Vec3Dot(Vec3Norm(d), fwd);
        if (dot < 0.4f) continue;
        float score = dot / (dist + 0.5f);
        if (score > bestScore) { bestScore = score; best = IA_PERK; bestIdx = i; }
    }
    // Mystery box
    {
        Vector3 d = Vec3Sub(g.map.boxPos, eye);
        float dist = Vec3Len(d);
        if (dist < 3.0f) {
            float dot = Vec3Dot(Vec3Norm(d), fwd);
            if (dot > 0.35f) {
                float score = dot / (dist + 0.5f);
                if (score > bestScore) { bestScore = score; best = IA_BOX; bestIdx = 0; }
            }
        }
    }
    // Ziplines
    for (int i = 0; i < g.map.ziplineCount; i++) {
        // grab point near the A end
        Vector3 a = g.map.ziplines[i].a;
        Vector3 d = Vec3Sub(a, eye);
        float dist = Vec3Len(d);
        if (dist > 3.2f) continue;
        float dot = Vec3Dot(Vec3Norm(d), fwd);
        if (dot < 0.3f) continue;
        float score = dot / (dist + 0.5f);
        if (score > bestScore) { bestScore = score; best = IA_ZIP; bestIdx = i; }
    }
    // Power switch (skip if already on or none on this map)
    if (!g.powerOn && (g.map.powerPos.x != 0 || g.map.powerPos.y != 0 || g.map.powerPos.z != 0)) {
        Vector3 pp = Vec3(g.map.powerPos.x, g.map.powerPos.y + 1.2f, g.map.powerPos.z);
        Vector3 d = Vec3Sub(pp, eye);
        float dist = Vec3Len(d);
        if (dist < 3.0f) {
            float dot = Vec3Dot(Vec3Norm(d), fwd);
            if (dot > 0.4f) {
                float score = dot / (dist + 0.5f);
                if (score > bestScore) { bestScore = score; best = IA_POWER; bestIdx = 0; }
            }
        }
    }
    // Pack-a-Punch machine (needs power)
    if (g.powerOn && (g.map.papPos.x != 0 || g.map.papPos.y != 0 || g.map.papPos.z != 0)) {
        Vector3 pp = Vec3(g.map.papPos.x, g.map.papPos.y + 1.0f, g.map.papPos.z);
        Vector3 d = Vec3Sub(pp, eye);
        float dist = Vec3Len(d);
        if (dist < 3.0f) {
            float dot = Vec3Dot(Vec3Norm(d), fwd);
            if (dot > 0.4f) {
                float score = dot / (dist + 0.5f);
                if (score > bestScore) { bestScore = score; best = IA_PAP; bestIdx = 0; }
            }
        }
    }
    if (idxOut) *idxOut = bestIdx;

    // labels + actions
    if (best == IA_DOOR && label) {
        int price = g.map.doors[bestIdx].price;
        snprintf(label, labelSize, "[E] Open %s (%d)", g.map.doors[bestIdx].name, price);
        if (doIt) {
            if (!DoorTryOpen(bestIdx)) SfxPlay(SND_DENIED, 1.0f, 0.8f);
            else { SfxPlay(SND_DOOR, 1.0f, 1.0f); }
        }
    } else if (best == IA_WALLBUY && label) {
        int widx = g.map.wallbuys[bestIdx].widx;
        const WeaponDef *w = &WEAPONS[widx];
        bool owned = (p->weapons[0].widx == widx) || (p->weapons[1].widx == widx);
        if (owned) {
            int ammoPrice = w->wallPrice / 2;
            snprintf(label, labelSize, "[E] Buy Ammo: %s (%d)", w->name, ammoPrice);
            if (doIt) {
                WeaponSlot *slot = NULL;
                if (p->weapons[0].widx == widx) slot = &p->weapons[0];
                else if (p->weapons[1].widx == widx) slot = &p->weapons[1];
                if (slot) {
                    if (p->points >= ammoPrice && slot->reserveAmmo < w->reserve) {
                        p->points -= ammoPrice;
                        slot->reserveAmmo = w->reserve;
                        SfxPlay(SND_BUY, 1.0f, 0.9f);
                    } else SfxPlay(SND_DENIED, 1.0f, 0.8f);
                }
            }
        } else {
            snprintf(label, labelSize, "[E] Buy %s (%d)", w->name, w->wallPrice);
            if (doIt) {
                if (p->points >= w->wallPrice) {
                    p->points -= w->wallPrice;
                    WeaponGive(widx, false);
                    SfxPlay(SND_BUY, 1.0f, 0.9f);
                } else SfxPlay(SND_DENIED, 1.0f, 0.8f);
            }
        }
    } else if (best == IA_PERK && label) {
        PerkType t = g.map.perks[bestIdx].type;
        const PerkDef *pd = &PERKS[t];
        if (p->perks[t]) {
            snprintf(label, labelSize, "%s - owned", pd->name);
        } else if (!g.powerOn) {
            // perks need power; wall buys and doors still work without it
            snprintf(label, labelSize, "%s - needs POWER", pd->name);
        } else {
            snprintf(label, labelSize, "[E] Drink %s (%d)", pd->name, pd->price);
            if (doIt) {
                if (p->points >= pd->price) {
                    p->points -= pd->price;
                    p->perks[t] = true;
                    SfxPlay(SND_PERK, 1.0f, 1.0f);
                    ParticlesBurst(Vec3(g.map.perks[bestIdx].pos.x, 1.2f, g.map.perks[bestIdx].pos.z), 20, pd->color, 2.5f, 0.1f, 0.8f, false);
                } else SfxPlay(SND_DENIED, 1.0f, 0.8f);
            }
        }
    } else if (best == IA_POWER && label) {
        snprintf(label, labelSize, "[E] Turn on the Power");
        if (doIt) {
            g.powerOn = true;
            SfxPlay(SND_PERK, 0.8f, 1.0f);
            SfxPlay(SND_DOOR, 0.7f, 0.9f);
        }
    } else if (best == IA_PAP && label) {
        int cw = p->weapons[p->curSlot].widx;
        if (cw < 0 || WEAPONS[cw].mode == FIRE_MELEE) {
            snprintf(label, labelSize, "Pack-a-Punch: hold a gun");
        } else if (p->weapons[p->curSlot].upgraded) {
            snprintf(label, labelSize, "%s - already upgraded", WEAPONS[cw].name);
        } else {
            snprintf(label, labelSize, "[E] Pack-a-Punch %s (%d)", WEAPONS[cw].name, PAP_PRICE);
            if (doIt) {
                if (p->points >= PAP_PRICE) {
                    p->points -= PAP_PRICE;
                    p->weapons[p->curSlot].upgraded = true;
                    // free refill with the upgrade
                    p->weapons[p->curSlot].magAmmo = WEAPONS[cw].mag;
                    p->weapons[p->curSlot].reserveAmmo = WEAPONS[cw].reserve;
                    p->reloadTimer = 0;
                    g.papAnim = 2.0f;
                    SfxPlay(SND_BOX_WIN, 1.0f, 1.0f);
                } else SfxPlay(SND_DENIED, 1.0f, 0.8f);
            }
        }
    } else if (best == IA_BOX && label) {
        if (g.boxInUse) {
            if (g.boxResultWeapon >= 0) snprintf(label, labelSize, "[E] Take %s", WEAPONS[g.boxResultWeapon].name);
            else snprintf(label, labelSize, "The box is stirring...");
        }
        else snprintf(label, labelSize, "[E] Mystery Box (%d)", BOX_PRICE);
        if (doIt && !g.boxInUse) {
            if (p->points >= BOX_PRICE) {
                p->points -= BOX_PRICE;
                g.boxInUse = true;
                g.boxSpinTimer = 4.2f;
                g.boxResultWeapon = -1;
                SfxPlay(SND_BOX_OPEN, 1.0f, 1.0f);
            } else SfxPlay(SND_DENIED, 1.0f, 0.8f);
        }
    } else if (best == IA_ZIP && label) {
        snprintf(label, labelSize, "[E] Ride Zipline");
        if (doIt) {
            g.ziplineRide = bestIdx;
            g.ziplineT = 0.0001f;
            SfxPlay(SND_ZIPLINE, 1.0f, 0.9f);
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
// Movement update
// ---------------------------------------------------------------------------
void PlayerUpdate(void) {
    Player *p = &g.player;

    if (p->downed) { PlayerDownedUpdate(); return; }

    // ---- mouse look ----
    // sens 0.2..20 (slider is exponential); no acceleration, no smoothing,
    // no clamping beyond pitch limits: mouse in -> rotation out, same frame.
    // Base 0.057 deg/count: default sens 1.0 turns 180 deg in one short
    // (~10 cm) swipe on an 800 CPI mouse.
    Vector2 md = GetMouseDelta();
    float sens = g.sens * 0.057f;
    if (p->adsBlend > 0.5f) sens *= 0.6f;
    p->yaw   -= md.x * sens;
    p->pitch -= md.y * sens * (g.invertY ? -1.0f : 1.0f);
    p->pitch = CLAMP(p->pitch, -89.0f, 89.0f);
    if (p->yaw > 180.0f) p->yaw -= 360.0f;
    if (p->yaw < -180.0f) p->yaw += 360.0f;

    // ---- WASD ----
    float yawR = p->yaw * DEG2RAD;
    Vector3 fwd   = Vec3(-sinf(yawR), 0, -cosf(yawR));
    Vector3 right = Vec3( cosf(yawR), 0, -sinf(yawR));
    Vector3 wish = Vec3(0,0,0);
    if (ActDown(ACT_FWD))    wish = Vec3Add(wish, fwd);
    if (ActDown(ACT_BACK))   wish = Vec3Sub(wish, fwd);
    if (ActDown(ACT_RIGHT))  wish = Vec3Add(wish, right);
    if (ActDown(ACT_LEFT))   wish = Vec3Sub(wish, right);
    bool moving = Vec3Len(wish) > 0.01f;
    if (moving) wish = Vec3Norm(wish);

    bool sprinting = ActDown(ACT_SPRINT) && moving && p->adsBlend < 0.3f;
    bool crouched = ActDown(ACT_CROUCH) || IsKeyDown(KEY_C);
    float speed = crouched ? PLAYER_CRAWL : (sprinting ? PLAYER_SPRINT : PLAYER_WALK);
    // downed-not-possible here; reload slows slightly
    if (p->reloadTimer > 0) speed *= 0.85f;
    if (p->perks[PERK_STAMIN]) speed *= (sprinting ? 1.16f : 1.08f);

    if (moving) {
        p->vel.x = wish.x * speed;
        p->vel.z = wish.z * speed;
    } else {
        p->vel.x *= 0.0001f;
        p->vel.z *= 0.0001f;
    }
    p->sprintTime = sprinting ? 1.0f : 0.0f;

    // jump
    if (ActPressed(ACT_JUMP) && p->grounded) {
        p->vel.y = PLAYER_JUMP;
        p->grounded = false;
    }

    // gravity + collision
    p->vel.y -= GRAVITY * (float)g.dt;
    if (p->vel.y < -40) p->vel.y = -40;
    float groundY;
    p->grounded = CollideMove(&p->pos, &p->vel, PLAYER_RADIUS, PLAYER_HEIGHT, (float)g.dt, &groundY);

    // footsteps
    if (moving && p->grounded) {
        p->stepTimer -= (float)g.dt * speed;
        if (p->stepTimer <= 0) {
            p->stepTimer = 2.6f;
            SfxPlay(RngInt(2) ? SND_STEP1 : SND_STEP2, RngRange(0.9f, 1.1f), 0.35f);
        }
    }

    // ---- zipline riding ----
    if (g.ziplineT > 0) {
        Zipline *z = &g.map.ziplines[g.ziplineRide];
        g.ziplineT += (float)g.dt / 2.2f;   // 2.2 s ride
        if (g.ziplineT >= 1.0f) {
            g.ziplineT = 0;
            p->pos = Vec3(z->b.x, z->b.y, z->b.z);
            p->vel = Vec3(0,0,0);
        } else {
            Vector3 pos = Vec3Lerp4(z->a, z->b, g.ziplineT);
            p->pos = Vec3(pos.x, pos.y - 1.6f, pos.z);
            p->vel.y = 0;
        }
        return;   // no other movement while riding
    }

    // ---- jump pads ----
    for (int i = 0; i < g.map.padCount; i++) {
        JumpPad *pad = &g.map.pads[i];
        float dx = p->pos.x - pad->pos.x, dz = p->pos.z - pad->pos.z;
        float py = GroundHeightAt(p->pos.x, p->pos.z, p->pos.y + 0.5f);
        if (dx*dx + dz*dz < pad->radius*pad->radius &&
            fabsf(p->pos.y - py) < 0.3f && p->grounded &&
            fabsf(p->pos.y - pad->pos.y) < 0.6f) {
            p->vel = pad->impulse;
            p->grounded = false;
            SfxPlay(SND_JUMP_PAD, 1.0f, 0.9f);
            ParticlesBurst(Vec3(pad->pos.x, p->pos.y+0.1f, pad->pos.z), 14, (Color){120,220,255,255}, 3.0f, 0.1f, 0.6f, false);
        }
    }

    // ---- killzone (void) ----
    if (p->pos.y < -60) {
        p->hp = 0;
        PlayerDamage(1);   // triggers down/death handling
    }

    // ---- regen perk ----
    if (p->perks[PERK_REGEN] && p->hp < PLAYER_MAX_HP && !p->downed) {
        if ((float)g.time - p->lastHitTime > REGEN_DELAY) {
            p->hp += REGEN_RATE * (float)g.dt;
            if (p->hp > PLAYER_MAX_HP) p->hp = PLAYER_MAX_HP;
        }
    }

    // ---- interaction (E) ----
    char label[96];
    static bool eHeld = false;
    bool eNow = ActDown(ACT_USE);
    g.ePressedThisFrame = eNow && !eHeld;
    eHeld = eNow;
    if (eNow) ScanInteract(label, sizeof label, NULL, g.ePressedThisFrame);

    // head bob
    if (moving && p->grounded) p->bobPhase += (float)g.dt * (sprinting ? 11.0f : 7.0f);
    else p->bobPhase += (float)g.dt * 1.2f;
}

void PlayerDownedUpdate(void) {
    Player *p = &g.player;
    p->downTimer -= (float)g.dt;
    // crawl slowly while downed
    float yawR = p->yaw * DEG2RAD;
    Vector3 fwd = Vec3(-sinf(yawR), 0, -cosf(yawR));
    Vector3 right = Vec3(cosf(yawR), 0, -sinf(yawR));
    Vector3 wish = Vec3(0,0,0);
    if (ActDown(ACT_FWD))   wish = Vec3Add(wish, fwd);
    if (ActDown(ACT_BACK))  wish = Vec3Sub(wish, fwd);
    if (ActDown(ACT_RIGHT)) wish = Vec3Add(wish, right);
    if (ActDown(ACT_LEFT))  wish = Vec3Sub(wish, right);
    if (Vec3Len(wish) > 0.01f) {
        wish = Vec3Norm(wish);
        p->vel.x = wish.x * PLAYER_CRAWL;
        p->vel.z = wish.z * PLAYER_CRAWL;
    } else { p->vel.x = 0; p->vel.z = 0; }
    p->vel.y -= GRAVITY * (float)g.dt;
    float gy;
    CollideMove(&p->pos, &p->vel, PLAYER_RADIUS, 0.9f, (float)g.dt, &gy);
    if (p->downTimer <= 0) GameOverSequence();
}

// lerp helper used by zipline
Vector3 Vec3Lerp4(Vector3 a, Vector3 b, float t) {
    return Vec3(a.x+(b.x-a.x)*t, a.y+(b.y-a.y)*t, a.z+(b.z-a.z)*t);
}

void PlayerDraw(Camera3D cam) {
    // first-person weapon viewmodel is drawn in weapons.c; here we draw
    // downed-state vignette hint particles (none needed in 3D).
    (void)cam;
}

// HUD-facing label of the current interaction target (no action performed).
const char* PlayerInteractLabel(void) {
    static char label[96];
    label[0] = 0;
    ScanInteract(label, sizeof label, NULL, false);
    return label;
}
