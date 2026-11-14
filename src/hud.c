// ============================================================================
//  DEAD ARRIVAL - hud.c
//  In-game HUD + particles + all menu screens (title, map select, settings,
//  pause, game over) + "Coming Soon" overlays (Custom Maps / Multiplayer).
// ============================================================================
#include "game.h"

static Font sFont;
static bool sFontLoaded = false;
static int sMenuSel = 0;             // map select index
static float sTitleTime = 0;

void HudInitFont(void) {
    if (sFontLoaded) return;
    // try a TTF from the system, else raylib default
    const char *paths[] = {
        "/usr/share/fonts/truetype/chinese/NotoSansSC-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
    };
    for (unsigned i = 0; i < ARRAY_LEN(paths); i++) {
        if (FileExists(paths[i])) {
            sFont = LoadFontEx(paths[i], 64, NULL, 0);
            sFontLoaded = true;
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Particles
// ---------------------------------------------------------------------------
void ParticlesSpawn(Vector3 pos, Vector3 vel, Color c, float size, float life, bool grav) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &g.particles[i];
        if (p->alive) continue;
        p->alive = true;
        p->pos = pos; p->vel = vel; p->color = c;
        p->size = size; p->life = life; p->maxLife = life; p->grav = grav;
        return;
    }
}

void ParticlesBurst(Vector3 pos, int n, Color c, float speed, float size, float life, bool grav) {
    for (int i = 0; i < n; i++) {
        Vector3 vel = Vec3(RngRange(-1,1), RngRange(-0.2f,1), RngRange(-1,1));
        if (Vec3Len(vel) > 0.01f) vel = Vec3Norm(vel);
        vel = Vec3Scale(vel, speed * RngRange(0.4f, 1.0f));
        ParticlesSpawn(pos, vel, c, size * RngRange(0.7f, 1.3f), life * RngRange(0.6f, 1.3f), grav);
    }
}

void ParticlesUpdate(void) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &g.particles[i];
        if (!p->alive) continue;
        p->life -= (float)g.dt;
        if (p->life <= 0) { p->alive = false; continue; }
        if (p->grav) p->vel.y -= 9.0f * (float)g.dt;
        p->pos = Vec3Add(p->pos, Vec3Scale(p->vel, (float)g.dt));
        float gy = GroundHeightAt(p->pos.x, p->pos.z, p->pos.y + 0.5f);
        if (p->grav && gy > -50 && p->pos.y < gy) { p->pos.y = gy; p->vel = Vec3Scale(p->vel, 0.3f); p->vel.y = 0; }
    }
}

void ParticlesDraw(Camera3D cam) {
    // billboarded quads via rlgl
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &g.particles[i];
        if (!p->alive) continue;
        float a = CLAMP(p->life / p->maxLife, 0, 1);
        Color c = p->color;
        c.a = (unsigned char)(c.a * a);
        Vector3 toCam = Vec3Sub(cam.position, p->pos);
        float w = p->size;
        if (Vec3Len(toCam) > 0.01f) {
            // cheap billboard: use camera right/up from raylib's camera matrix
            Vector3 right = Vec3Cross(Vec3Norm(toCam), cam.up);
            if (Vec3Len(right) < 1e-4f) right = Vec3(1,0,0); else right = Vec3Norm(right);
            Vector3 up = Vec3Cross(right, Vec3Norm(toCam));
            Vector3 v1 = Vec3Add(Vec3Add(p->pos, Vec3Scale(right, -w/2)), Vec3Scale(up, w/2));
            Vector3 v2 = Vec3Add(v1, Vec3Scale(right, w));
            Vector3 v3 = Vec3Add(v2, Vec3Scale(up, -w));
            Vector3 v4 = Vec3Add(v3, Vec3Scale(right, -w));
            DrawTriangle3D(v1, v2, v3, c);
            DrawTriangle3D(v1, v3, v4, c);
        }
    }
}

// ---------------------------------------------------------------------------
// Text helpers
// ---------------------------------------------------------------------------
static void T(const char *s, int x, int y, int size, Color c, bool shadow) {
    if (shadow) DrawText(s, x+2, y+2, size, (Color){0,0,0,160});
    DrawText(s, x, y, size, c);
}
float HudTextWidth(const char *s, int size) { return (float)MeasureText(s, size); }

bool UiButton(Rectangle r, const char *text, int fontSize) {
    Vector2 mp = GetMousePosition();
    bool hover = CheckCollisionPointRec(mp, r);
    bool click = IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
    Color fill = hover ? (Color){70, 20, 24, 235} : (Color){38, 12, 14, 215};
    Color border = hover ? (Color){220, 60, 50, 255} : (Color){130, 40, 36, 255};
    DrawRectangleRec(r, fill);
    DrawRectangleLinesEx(r, 2, border);
    int tw = MeasureText(text, fontSize);
    DrawText(text, (int)(r.x + r.width/2 - tw/2), (int)(r.y + r.height/2 - fontSize/2), fontSize,
             hover ? RAYWHITE : (Color){225, 210, 205, 255});
    return hover && click;
}

// ---------------------------------------------------------------------------
// HUD update
// ---------------------------------------------------------------------------
void HudUpdate(void) {
    if (g.hitMarker > 0) g.hitMarker -= (float)g.dt;
    if (g.damageFlash > 0) g.damageFlash -= (float)g.dt * 1.6f;
    if (g.muzzleFlash > 0) g.muzzleFlash -= (float)g.dt;
    else GfxSetFlash(Vec3(0,0,0), 0.0f);   // clear stale flash light
    if (g.shakeTime > 0) g.shakeTime -= (float)g.dt;
    if (g.roundBannerTime > 0) g.roundBannerTime -= (float)g.dt;
    sTitleTime += (float)g.dt;
    ParticlesUpdate();
}

// ---------------------------------------------------------------------------
// Gameplay HUD
// ---------------------------------------------------------------------------
void HudDrawGameplay(void) {
    Player *p = &g.player;
    int w = GetScreenWidth(), h = GetScreenHeight();

    // crosshair (spread)
    if (!p->downed && WeaponCurrentIndex() >= 0 && WEAPONS[WeaponCurrentIndex()].mode != FIRE_MELEE) {
        int cx = w/2, cy = h/2;
        float spread = WeaponSpreadCurrent();
        int gap = (int)(6 + spread * 700);
        Color cc = (Color){235, 235, 235, 220};
        DrawLine(cx-gap, cy, cx-gap+7, cy, cc);
        DrawLine(cx+gap, cy, cx+gap-7, cy, cc);
        DrawLine(cx, cy-gap, cx, cy-gap+7, cc);
        DrawLine(cx, cy+gap, cx, cy+gap-7, cc);
    }
    // hit marker
    if (g.hitMarker > 0) {
        int cx = w/2, cy = h/2;
        Color hc = (Color){255, 240, 240, (unsigned char)(g.hitMarker*4*255)};
        for (int s = -1; s <= 1; s += 2) {
            DrawLine(cx+5*s, cy+5*s, cx+12*s, cy+12*s, hc);
            DrawLine(cx+5*s, cy-5*s, cx+12*s, cy-12*s, hc);
        }
    }

    // ---- bottom left: health + perks ----
    int hx = 30, hy = h - 96;
    float hpFrac = CLAMP(p->hp / PLAYER_MAX_HP, 0, 1);
    Color hpCol = hpFrac > 0.5f ? (Color){200, 40, 40, 255} :
                  hpFrac > 0.25f ? (Color){230, 120, 30, 255} : (Color){240, 30, 30, 255};
    DrawRectangle(hx, hy, 260, 16, (Color){20, 8, 8, 180});
    DrawRectangle(hx+2, hy+2, (int)((260-4) * hpFrac), 12, hpCol);
    char hpTxt[32];
    snprintf(hpTxt, sizeof hpTxt, "%d HP", (int)p->hp);
    T(hpTxt, hx, hy - 22, 20, RAYWHITE, true);

    // perk icons (soda bottles as colored diamonds)
    int px0 = hx;
    for (int i = 0; i < PERK_COUNT; i++) {
        if (!p->perks[i]) continue;
        Color pc = PERKS[i].color;
        Vector2 c = { px0 + 14.0f, (float)(hy + 40) };
        DrawTriangle((Vector2){c.x, c.y-12}, (Vector2){c.x+12, c.y}, (Vector2){c.x, c.y+12}, pc);
        DrawTriangle((Vector2){c.x, c.y-12}, (Vector2){c.x-12, c.y}, (Vector2){c.x, c.y+12}, Fade(pc, 0.7f));
        px0 += 34;
    }
    // downed warning
    if (p->downed) {
        T("YOU ARE DOWN - BLEEDING OUT", w/2 - 190, 90, 26, (Color){255, 60, 50, 255}, true);
        char bt[48];
        snprintf(bt, sizeof bt, "%.0f s", fmaxf(p->downTimer, 0));
        T(bt, w/2 - 20, 120, 30, (Color){255, 120, 100, 255}, true);
    }
    // regen hint when waiting
    if (p->perks[PERK_REGEN] && p->hp < PLAYER_MAX_HP && !p->downed) {
        float wait = REGEN_DELAY - ((float)g.time - p->lastHitTime);
        if (wait > 0) {
            char rt[48];
            snprintf(rt, sizeof rt, "Regen in %.0f", wait);
            T(rt, hx, hy + 26, 18, (Color){240, 200, 140, 220}, true);
        }
    }

    // ---- bottom right: ammo ----
    WeaponSlot *s = &p->weapons[p->curSlot];
    if (s->widx >= 0) {
        const WeaponDef *wd = &WEAPONS[s->widx];
        char ammo[64];
        if (wd->mode == FIRE_MELEE) snprintf(ammo, sizeof ammo, "KNIFE");
        else snprintf(ammo, sizeof ammo, "%d / %d", s->magAmmo, s->reserveAmmo);
        int aw = MeasureText(ammo, 34);
        T(ammo, w - aw - 34, h - 60, 34, s->magAmmo == 0 ? (Color){255, 90, 70, 255} : RAYWHITE, true);
        char wname[48];
        if (s->upgraded) snprintf(wname, sizeof wname, "%s+", wd->name);
        else snprintf(wname, sizeof wname, "%s", wd->name);
        int nw = MeasureText(wname, 20);
        T(wname, w - nw - 34, h - 92, 20, s->upgraded ? (Color){140, 240, 255, 255} : (Color){220, 210, 200, 230}, true);
    }

    // ---- top left: round + zombies left ----
    char roundTxt[48];
    int left = g.roundQuota - g.roundKills;
    snprintf(roundTxt, sizeof roundTxt, "Round %d", g.round);
    T(roundTxt, 30, 26, 34, (Color){230, 60, 45, 255}, true);
    char zleft[48];
    snprintf(zleft, sizeof zleft, "%d left", left > 0 ? left : 0);
    T(zleft, 32, 64, 20, (Color){225, 210, 205, 220}, true);
    if (g.hardMode) T("HARD MODE", 32, 88, 18, (Color){255, 120, 60, 230}, true);

    // ---- torch charge (hard mode) ----
    if (g.hardMode) {
        int tx = 30, ty = h - 130;
        DrawRectangle(tx, ty, 150, 12, (Color){20, 8, 8, 180});
        float frac = CLAMP(g.torchCharge, 0, 1);
        Color tc = frac > 0.5f ? (Color){255, 220, 120, 255} :
                   frac > 0.2f ? (Color){255, 170, 60, 255} : (Color){255, 80, 40, 255};
        DrawRectangle(tx+2, ty+2, (int)((150-4) * frac), 8, tc);
        const char *lbl = "TORCH";
        T(lbl, tx, ty - 20, 16, (Color){225, 210, 190, 200}, true);
        if (frac <= 0.001f) {
            float pulse = 0.5f + 0.5f * sinf((float)g.time * 6.0f);
            const char *warn = "SHAKE THE TORCH!";
            T(warn, w/2 - MeasureText(warn, 24)/2, h*0.62f, 24,
              (Color){255, 200, 120, (unsigned char)(160 + 90*pulse)}, true);
        }
    }

    // ---- top right: points ----
    char pts[48];
    snprintf(pts, sizeof pts, "%d", p->points);
    int pw = MeasureText(pts, 34);
    T(pts, w - pw - 30, 26, 34, (Color){250, 215, 120, 255}, true);
    T("POINTS", w - 108, 62, 16, (Color){200, 185, 150, 200}, true);

    // ---- center: interaction prompt ----
    if (!p->downed) {
        const char *label = PlayerInteractLabel();
        if (label && label[0]) {
            int lw = MeasureText(label, 22);
            DrawRectangle(w/2 - lw/2 - 12, h/2 + 40, lw + 24, 32, (Color){0, 0, 0, 150});
            T(label, w/2 - lw/2, h/2 + 46, 22, RAYWHITE, false);
        }
    }

    // ---- round banner ----
    if (g.roundBannerTime > 0 && g.roundBannerActive) {
        float a = CLAMP(g.roundBannerTime / 3.0f, 0, 1);
        char bt[64];
        snprintf(bt, sizeof bt, "- ROUND %d -", g.round);
        int bw = MeasureText(bt, 52);
        Color bc = (Color){220, 45, 35, (unsigned char)(255 * sinf(a * PI))};
        T(bt, w/2 - bw/2, h*0.30f, 52, bc, true);
    }

    // ---- damage vignette ----
    if (g.damageFlash > 0) {
        DrawRectangleGradientV(0, 0, w, h*0/2, (Color){0,0,0,0}, (Color){0,0,0,0});
        DrawRectangle(0, 0, w, h, (Color){140, 0, 0, (unsigned char)(90 * CLAMP(g.damageFlash,0,1))});
    }
    if (p->hp < 35 && !p->downed) {
        float pulse = 0.5f + 0.5f * sinf((float)g.time * 5.0f);
        DrawRectangle(0, 0, w, h, (Color){120, 0, 0, (unsigned char)(40 + 30*pulse)});
    }
}

// ---------------------------------------------------------------------------
// Menus
// ---------------------------------------------------------------------------
static const char *MAP_NAMES[MAP_COUNT] = { "The Grand Theater", "Bunker 44", "Graveyard Hollow", "Die Arise", "Dead Con 5" };
static const char *MAP_DESCS[MAP_COUNT] = {
    "The curtain rose on the end of the world. Hold the stage.",
    "Holding position in the ruins of the final offensive.",
    "The dead were never resting. The church is your only refuge.",
    "The city is gone. Only the rooftops remain - and they go up.",
    "The Oval Office first. The FBI's secrets are upstairs.",
};

void HudDrawTitle(void) {
    int w = GetScreenWidth(), h = GetScreenHeight();
    // backdrop
    DrawRectangleGradientV(0, 0, w, h, (Color){12, 6, 8, 255}, (Color){2, 1, 2, 255});

    // title with flicker
    float flick = 0.85f + 0.15f * sinf(sTitleTime * 3.1f) * sinf(sTitleTime * 7.7f);
    int tw = MeasureText("DEAD ARRIVAL", 74);
    DrawText("DEAD ARRIVAL", w/2 - tw/2 + 4, (int)(h*0.16f) + 4, 74, (Color){60, 8, 5, 255});
    Color titleC = { (unsigned char)(215*flick), (unsigned char)(40*flick), (unsigned char)(30*flick), 255 };
    DrawText("DEAD ARRIVAL", w/2 - tw/2, (int)(h*0.16f), 74, titleC);
    int sw = MeasureText("a round-based survival FPS", 22);
    T("a round-based survival FPS", w/2 - sw/2, (int)(h*0.16f) + 92, 22, (Color){190, 175, 170, 220}, false);

    Rectangle bPlay  = { w/2.0f - 160, h*0.46f, 320, 54 };
    Rectangle bSet   = { w/2.0f - 160, h*0.46f + 66, 320, 44 };
    Rectangle bCust  = { w/2.0f - 160, h*0.46f + 120, 320, 44 };
    Rectangle bMulti = { w/2.0f - 160, h*0.46f + 174, 320, 44 };
    Rectangle bQuit  = { w/2.0f - 160, h*0.46f + 228, 320, 44 };
    if (UiButton(bPlay,  "PLAY", 30)) { g.state = STATE_MAPSELECT; SfxPlay(SND_BUY, 1.0f, 0.8f); }
    if (UiButton(bSet,   "SETTINGS", 22)) { g.state = STATE_SETTINGS; }
    if (UiButton(bCust,  "CUSTOM MAPS  [COMING SOON]", 20)) { g.overlay = OVERLAY_COMINGSOON; }
    if (UiButton(bMulti, "MULTIPLAYER  [COMING SOON]", 20)) { g.overlay = OVERLAY_COMINGSOON; }
    if (UiButton(bQuit,  "QUIT", 22)) g.windowShouldClose = true;

    T("v" GAME_VERSION "  -  " ORG_NAME, 14, h - 26, 16, (Color){150, 140, 140, 200}, false);
    T("WASD move - Mouse aim - E interact - R reload - Shift sprint - 1/2/3 weapons - F knife", 14, h - 46, 16, (Color){160, 150, 150, 180}, false);
    T("rebind keys and tune everything in SETTINGS", 14, h - 66, 14, (Color){130, 120, 120, 170}, false);
}

static bool UiToggle(Rectangle r, bool on, const char *text, int fontSize) {
    bool hover = CheckCollisionPointRec(GetMousePosition(), r);
    Color fill = on ? (Color){70, 24, 18, 235} : (Color){24, 12, 12, 215};
    Color border = on ? (Color){230, 90, 50, 255} : (Color){100, 40, 34, 255};
    DrawRectangleRec(r, fill);
    DrawRectangleLinesEx(r, 2, border);
    // little indicator lamp
    DrawCircle((int)(r.x + 22), (int)(r.y + r.height/2), 7, on ? (Color){255, 140, 60, 255} : (Color){60, 34, 30, 255});
    int tw = MeasureText(text, fontSize);
    DrawText(text, (int)(r.x + 40 + (r.width - 40 - tw)/2), (int)(r.y + r.height/2 - fontSize/2), fontSize,
             on ? RAYWHITE : (Color){190, 175, 170, 255});
    return hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
}

void HudDrawMapSelect(void) {
    int w = GetScreenWidth(), h = GetScreenHeight();
    DrawRectangleGradientV(0, 0, w, h, (Color){10, 5, 7, 255}, (Color){3, 2, 2, 255});
    T("SELECT MAP", w/2 - MeasureText("SELECT MAP", 44)/2, 40, 44, (Color){225, 60, 45, 255}, true);

    float bx = w/2 - 330;
    for (int i = 0; i < MAP_COUNT; i++) {
        Rectangle r = { bx, 118.0f + i * 96, 660, 86 };
        bool hover = CheckCollisionPointRec(GetMousePosition(), r);
        bool locked = false;
        // Die Arise unlocks after reaching round 3 on any map? keep all open but show difficulty
        Color border = hover ? (Color){230, 70, 55, 255} : (Color){110, 35, 32, 255};
        DrawRectangleRec(r, hover ? (Color){45, 16, 15, 240} : (Color){28, 10, 10, 220});
        DrawRectangleLinesEx(r, 2, border);
        T(MAP_NAMES[i], r.x + 24, r.y + 12, 28, locked ? GRAY : RAYWHITE, false);
        T(MAP_DESCS[i], r.x + 24, r.y + 48, 18, (Color){200, 190, 185, 220}, false);
        // difficulty skulls
        for (int d = 0; d < 5; d++) {
            Color dc = d < (i + 2) ? (Color){230, 60, 45, 255} : (Color){70, 40, 38, 255};
            DrawCircle(r.x + 480 + d*26, r.y + 28, 8, dc);
        }
        char best[40];
        snprintf(best, sizeof best, "best: round %d", g.bestRound[i]);
        T(best, r.x + 480, r.y + 50, 18, (Color){235, 205, 120, 220}, false);
        if (hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            g.state = STATE_PLAYING;
            GameStart((MapId)i);
        }
    }
    // hard mode toggle under the list
    Rectangle hm = { w/2.0f - 160, 118.0f + MAP_COUNT * 96 + 10, 320, 44 };
    if (UiToggle(hm, g.hardMode, g.hardMode ? "HARD MODE: ON" : "HARD MODE: OFF", 22)) {
        g.hardMode = !g.hardMode;
        SfxPlay(SND_BUY, g.hardMode ? 0.8f : 1.1f, 0.6f);
    }
    if (g.hardMode) {
        const char *hd = "Darker. Your flashlight only works while you keep shaking it.";
        T(hd, w/2 - MeasureText(hd, 17)/2, (int)(hm.y + hm.height + 8), 17, (Color){235, 170, 120, 220}, false);
    }
    T("ESC to go back", 20, h - 30, 18, (Color){160, 150, 150, 200}, false);
}

// ---------------------------------------------------------------------------
// Settings menu: tabs (Game / Audio / Video / Controls)
// ---------------------------------------------------------------------------
enum { SET_TAB_GAME = 0, SET_TAB_AUDIO, SET_TAB_VIDEO, SET_TAB_CONTROLS, SET_TAB_COUNT };
static int sSetTab = SET_TAB_GAME;
static int sRebindAction = -1;      // action currently waiting for a key
static float sSliderFlash = 0;      // feedback when a slider is dragged

// true while the Controls tab is capturing a key press (ESC must not exit)
bool HudCapturingKey(void) { return sRebindAction >= 0; }

// slider helper: returns true when the value changed this frame
static bool UiSlider(Rectangle bar, float *val, float min, float max, const char *label,
                     const char *valText, int x, int y) {
    T(label, x, y, 22, RAYWHITE, false);
    Rectangle track = { x + 240.0f, (float)y + 6, 340, 14 };
    bool changed = false;
    Vector2 mp = GetMousePosition();
    if (CheckCollisionPointRec(mp, (Rectangle){ track.x, track.y - 8, track.width, track.height + 16 }) &&
        IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        float t = CLAMP((mp.x - track.x) / track.width, 0, 1);
        *val = min + t * (max - min);
        changed = true;
    }
    DrawRectangleRec(track, (Color){30, 12, 12, 255});
    float frac = CLAMP((*val - min) / (max - min), 0, 1);
    DrawRectangleRec((Rectangle){ track.x, track.y, track.width * frac, track.height }, (Color){220, 60, 45, 255});
    DrawCircle((int)(track.x + track.width * frac), (int)(track.y + track.height/2), 8, RAYWHITE);
    T(valText, (int)(track.x + track.width + 16), y, 20, RAYWHITE, false);
    (void)bar;
    return changed;
}

void HudDrawSettings(void) {
    int w = GetScreenWidth(), h = GetScreenHeight();
    DrawRectangleGradientV(0, 0, w, h, (Color){10, 5, 7, 255}, (Color){3, 2, 2, 255});
    T("SETTINGS", w/2 - MeasureText("SETTINGS", 44)/2, 34, 44, (Color){225, 60, 45, 255}, true);

    // tabs
    const char *tabNames[SET_TAB_COUNT] = { "GAME", "AUDIO", "VIDEO", "CONTROLS" };
    int tabW = 150;
    for (int i = 0; i < SET_TAB_COUNT; i++) {
        Rectangle tr = { w/2.0f - (SET_TAB_COUNT*tabW)/2.0f + i*tabW, 92, (float)tabW - 6, 40 };
        bool active = sSetTab == i;
        bool hover = CheckCollisionPointRec(GetMousePosition(), tr);
        DrawRectangleRec(tr, active ? (Color){70, 20, 24, 235} : hover ? (Color){45, 16, 15, 235} : (Color){26, 11, 11, 215});
        DrawRectangleLinesEx(tr, 2, active ? (Color){230, 70, 55, 255} : (Color){100, 38, 34, 255});
        int tw = MeasureText(tabNames[i], 20);
        DrawText(tabNames[i], (int)(tr.x + (tr.width - tw)/2), (int)(tr.y + tr.height/2 - 10), 20,
                 active ? RAYWHITE : (Color){190, 175, 170, 255});
        if (hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && sRebindAction < 0) sSetTab = i;
    }
    int leftX = w/2 - 430;
    int rowY = 170;
    char vt[48];

    if (sSetTab == SET_TAB_GAME) {
        // sensitivity: exponential slider 0.2 .. 20 so the middle (~2) is a
        // natural aiming speed and the top end is twice the previous max
        float t = logf(g.sens / 0.2f) / logf(100.0f);   // 0..1
        snprintf(vt, sizeof vt, "%.2f", g.sens);
        Rectangle sb = { 0,0,0,0 };
        float tt = t;
        if (UiSlider(sb, &tt, 0.0f, 1.0f, "Mouse Sensitivity", vt, leftX, rowY)) {
            g.sens = 0.2f * powf(100.0f, CLAMP(tt, 0, 1));
        }
        T("low - high (default 1.0 turns 180 deg in one short swipe)", leftX + 240, rowY + 26, 14, (Color){150, 140, 140, 200}, false);
        int fy = rowY + 70;
        if (UiToggle((Rectangle){ (float)leftX, (float)fy, 340, 44 }, g.invertY, "Invert Y Axis", 20)) {
            g.invertY = !g.invertY;
        }
        T("FOV, brightness, audio and keys live in the other tabs.", leftX, fy + 60, 16, (Color){170, 158, 155, 210}, false);
    }
    else if (sSetTab == SET_TAB_AUDIO) {
        snprintf(vt, sizeof vt, "%.0f%%", g.volMaster * 100);
        if (UiSlider((Rectangle){0}, &g.volMaster, 0, 1, "Master Volume", vt, leftX, rowY)) {
            SfxSetVolume(g.volMaster, g.volSfx);
            SfxSetMusicVolume(g.volMusic);
        }
        snprintf(vt, sizeof vt, "%.0f%%", g.volMusic * 100);
        if (UiSlider((Rectangle){0}, &g.volMusic, 0, 1, "Music Volume", vt, leftX, rowY + 64)) {
            SfxSetVolume(g.volMaster, g.volSfx);
            SfxSetMusicVolume(g.volMusic);
        }
        snprintf(vt, sizeof vt, "%.0f%%", g.volSfx * 100);
        if (UiSlider((Rectangle){0}, &g.volSfx, 0, 1, "SFX Volume", vt, leftX, rowY + 128)) {
            SfxSetVolume(g.volMaster, g.volSfx);
            SfxSetMusicVolume(g.volMusic);
            SfxPlay(SND_BUY, 1.0f, 0.5f);   // preview blip
        }
    }
    else if (sSetTab == SET_TAB_VIDEO) {
        snprintf(vt, sizeof vt, "%.0f", g.fovSetting);
        if (UiSlider((Rectangle){0}, &g.fovSetting, 60, 120, "Field of View", vt, leftX, rowY)) { }
        snprintf(vt, sizeof vt, "%.2f", g.brightness);
        if (UiSlider((Rectangle){0}, &g.brightness, 0.5f, 1.5f, "Brightness", vt, leftX, rowY + 64)) { }
        int fy = rowY + 128;
        if (UiToggle((Rectangle){ (float)leftX, (float)fy, 340, 44 }, g.fullscreen, "Fullscreen", 20)) {
            g.fullscreen = !g.fullscreen;
            ToggleFullscreen();
        }
        if (UiToggle((Rectangle){ (float)leftX + 360, (float)fy, 340, 44 }, g.vsync, "VSync", 20)) {
            g.vsync = !g.vsync;
            SetTargetFPS(g.vsync ? 60 : 0);
        }
        T("Changing VSync takes effect on next launch (window flag).", leftX, fy + 60, 16, (Color){170, 158, 155, 210}, false);
    }
    else { // CONTROLS
        if (sRebindAction >= 0) {
            // waiting for a key press; ESC cancels
            const char *w1 = "Press a key for the action...";
            T(w1, w/2 - MeasureText(w1, 26)/2, 130, 26, (Color){255, 200, 120, 255}, true);
            const char *w2 = "(ESC cancels, no double bindings allowed)";
            T(w2, w/2 - MeasureText(w2, 18)/2, 164, 18, (Color){200, 185, 175, 220}, false);
            int key = GetKeyPressed();
            if (key != 0) {
                if (key != KEY_ESCAPE) {
                    // clear the same key from other actions, then bind
                    for (int i = 0; i < ACT_COUNT; i++) if (g.keys[i] == key) g.keys[i] = 0;
                    g.keys[sRebindAction] = key;
                }
                sRebindAction = -1;
            }
        } else {
            const char *ch = "Click a key to rebind it";
            T(ch, w/2 - MeasureText(ch, 18)/2, 128, 18, (Color){170, 158, 155, 210}, false);
        }
        // two columns of bindings
        int rows = (ACT_COUNT + 1) / 2;
        for (int i = 0; i < ACT_COUNT; i++) {
            int col = i / rows, row = i % rows;
            int bx = w/2 - 430 + col * 440;
            int by = 168 + row * 46;
            Rectangle r = { (float)bx, (float)by, 400, 38 };
            bool hot = sRebindAction == i;
            bool hover = CheckCollisionPointRec(GetMousePosition(), r);
            DrawRectangleRec(r, hot ? (Color){80, 30, 20, 240} : hover ? (Color){45, 16, 15, 235} : (Color){26, 11, 11, 215});
            DrawRectangleLinesEx(r, 2, hot ? (Color){255, 180, 90, 255} : (Color){100, 38, 34, 255});
            T(ActName((KeyAction)i), bx + 14, by + 8, 18, (Color){210, 198, 192, 255}, false);
            const char *kn = g.keys[i] ? TextFormat("%d", g.keys[i]) : "-";
            if (g.keys[i]) {
                // printable name for common keys
                int k = g.keys[i];
                if (k >= 32 && k < 127) kn = TextFormat("%c", (char)k);
                else if (k == KEY_SPACE) kn = "SPACE";
                else if (k == KEY_LEFT_SHIFT) kn = "L-SHIFT";
                else if (k == KEY_LEFT_CONTROL) kn = "L-CTRL";
                else if (k == KEY_LEFT_ALT) kn = "L-ALT";
                else if (k == KEY_TAB) kn = "TAB";
                else if (k == KEY_UP) kn = "UP";
                else if (k == KEY_DOWN) kn = "DOWN";
                else if (k == KEY_LEFT) kn = "LEFT";
                else if (k == KEY_RIGHT) kn = "RIGHT";
                else kn = TextFormat("KEY %d", k);
            }
            T(kn, bx + 290, by + 8, 18, hot ? (Color){255, 220, 160, 255} : RAYWHITE, false);
            if (hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && sRebindAction < 0)
                sRebindAction = i;
        }
        // hard mode shake mode toggle (the Controls-only gameplay option)
        Rectangle shake = { w/2.0f - 210, 168.0f + rows * 46 + 10, 420, 44 };
        if (UiToggle(shake, g.shakeByMouse, g.shakeByMouse ? "Shake Torch: MOUSE MOVEMENT" : "Shake Torch: KEY MASH (F)", 19)) {
            g.shakeByMouse = !g.shakeByMouse;
        }
        T("How you shake the flashlight in Hard Mode.", w/2 - MeasureText("How you shake the flashlight in Hard Mode.", 15)/2,
          (int)(shake.y + shake.height + 6), 15, (Color){150, 140, 140, 200}, false);
    }

    if (UiButton((Rectangle){ w/2.0f - 120, (float)h - 90, 240, 50 }, "BACK", 24)) {
        g.state = STATE_TITLE;
        SaveSettings();
    }
    if (sSliderFlash > 0) sSliderFlash -= (float)g.dt;
    T("ESC to go back (settings save automatically)", 20, h - 30, 18, (Color){160, 150, 150, 200}, false);
    // version string in settings header (dev builds)
    char ver[64];
    snprintf(ver, sizeof ver, "v" GAME_VERSION);
    T(ver, w - 100, 40, 16, (Color){150, 140, 140, 200}, false);
}

void HudDrawPause(void) {
    int w = GetScreenWidth(), h = GetScreenHeight();
    DrawRectangle(0, 0, w, h, (Color){0, 0, 0, 170});
    T("PAUSED", w/2 - MeasureText("PAUSED", 54)/2, h*0.18f, 54, (Color){225, 60, 45, 255}, true);
    if (UiButton((Rectangle){ w/2.0f - 140, h*0.38f, 280, 50 }, "RESUME", 24)) g.state = STATE_PLAYING;
    if (UiButton((Rectangle){ w/2.0f - 140, h*0.38f + 64, 280, 50 }, "SETTINGS", 24)) g.state = STATE_SETTINGS;
    if (UiButton((Rectangle){ w/2.0f - 140, h*0.38f + 128, 280, 50 }, "QUIT TO MENU", 24)) {
        g.state = STATE_TITLE;
        MusicStop();
        SaveScores();
    }
}

void HudDrawGameOver(void) {
    int w = GetScreenWidth(), h = GetScreenHeight();
    DrawRectangle(0, 0, w, h, (Color){20, 0, 0, 200});
    T("YOU DIED", w/2 - MeasureText("YOU DIED", 64)/2, h*0.14f, 64, (Color){225, 45, 35, 255}, true);
    Player *p = &g.player;
    char line[96];
    snprintf(line, sizeof line, "Survived to Round %d on %s", g.round, g.map.name);
    T(line, w/2 - MeasureText(line, 26)/2, h*0.14f + 90, 26, RAYWHITE, true);
    snprintf(line, sizeof line, "%d kills - %d points earned - %d doors opened", p->kills, p->earned, p->doorsOpened);
    T(line, w/2 - MeasureText(line, 22)/2, h*0.14f + 128, 22, (Color){220, 210, 205, 230}, true);
    if (UiButton((Rectangle){ w/2.0f - 140, h*0.55f, 280, 50 }, "RETRY", 24)) GameStart((MapId)g.mapId);
    if (UiButton((Rectangle){ w/2.0f - 140, h*0.55f + 64, 280, 50 }, "MAIN MENU", 24)) g.state = STATE_TITLE;
}

void HudDrawOverlay(void) {
    if (g.overlay != OVERLAY_COMINGSOON) return;
    int w = GetScreenWidth(), h = GetScreenHeight();
    DrawRectangle(0, 0, w, h, (Color){0, 0, 0, 185});
    const char *t1 = "COMING SOON";
    T(t1, w/2 - MeasureText(t1, 58)/2, h*0.34f, 58, (Color){230, 190, 90, 255}, true);
    const char *t2 = "Custom maps and multiplayer are on the roadmap.";
    T(t2, w/2 - MeasureText(t2, 24)/2, h*0.34f + 90, 24, RAYWHITE, true);
    const char *t3 = "The engine already loads maps from data - modding docs will follow.";
    T(t3, w/2 - MeasureText(t3, 20)/2, h*0.34f + 124, 20, (Color){200, 190, 185, 220}, true);
    if (UiButton((Rectangle){ w/2.0f - 100, h*0.6f, 200, 46 }, "OK", 24)) g.overlay = OVERLAY_NONE;
}

void HudDraw(void) {
    switch (g.state) {
        case STATE_TITLE:     HudDrawTitle(); break;
        case STATE_MAPSELECT: HudDrawMapSelect(); break;
        case STATE_SETTINGS:  HudDrawSettings(); break;
        case STATE_PLAYING:   HudDrawGameplay(); break;
        case STATE_PAUSED:    HudDrawPause(); break;
        case STATE_GAMEOVER:  HudDrawGameOver(); break;
        default: break;
    }
    HudDrawOverlay();
}
