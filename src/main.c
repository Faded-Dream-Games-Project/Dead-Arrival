// ============================================================================
//  DEAD ARRIVAL - main.c
//  Entry point: window, main loop, state machine, camera + render pipeline,
//  settings/score persistence, --selftest and --smoke modes.
// ============================================================================
#include "game.h"

Game g;

// ---------------------------------------------------------------------------
// Persistence (settings + best rounds) next to the binary
// ---------------------------------------------------------------------------
static const int DEF_KEYS[ACT_COUNT] = {
    [ACT_FWD]    = KEY_W,   [ACT_BACK]  = KEY_S,   [ACT_LEFT]  = KEY_A,
    [ACT_RIGHT]  = KEY_D,   [ACT_JUMP]  = KEY_SPACE,[ACT_SPRINT]= KEY_LEFT_SHIFT,
    [ACT_CROUCH] = KEY_LEFT_CONTROL,    [ACT_USE]   = KEY_E,
    [ACT_RELOAD] = KEY_R,   [ACT_KNIFE] = KEY_F,   [ACT_WEP1]  = KEY_ONE,
    [ACT_WEP2]   = KEY_TWO, [ACT_WEP3]  = KEY_THREE,[ACT_SWAP]  = KEY_Q,
    [ACT_SHAKE]  = KEY_F,
};

static const char *KEY_NAMES[ACT_COUNT] = {
    "Forward", "Back", "Left", "Right", "Jump", "Sprint", "Crouch",
    "Interact", "Reload", "Knife", "Weapon 1", "Weapon 2", "Weapon 3", "Swap",
    "Shake Torch",
};

const char *ActName(KeyAction a) {
    return (a >= 0 && a < ACT_COUNT) ? KEY_NAMES[a] : "?";
}

bool ActPressed(KeyAction a) { return IsKeyPressed((KeyboardKey)g.keys[a]); }
bool ActDown(KeyAction a)    { return IsKeyDown((KeyboardKey)g.keys[a]); }

static void SetDefaults(void) {
    g.sens = 1.0f;
    g.volMaster = 0.8f; g.volMusic = 0.8f; g.volSfx = 0.9f;
    g.brightness = 1.0f;
    g.fovSetting = 75.0f;
    g.invertY = false;
    g.fullscreen = false;
    g.vsync = true;
    g.shakeByMouse = true;
    for (int i = 0; i < ACT_COUNT; i++) g.keys[i] = DEF_KEYS[i];
}

void SaveSettings(void) {
    FILE *f = fopen("settings.cfg", "w");
    if (!f) return;
    fprintf(f, "sens %.3f\n", g.sens);
    fprintf(f, "volMaster %.3f\nvolMusic %.3f\nvolSfx %.3f\n", g.volMaster, g.volMusic, g.volSfx);
    fprintf(f, "brightness %.3f\n", g.brightness);
    fprintf(f, "fov %.1f\n", g.fovSetting);
    fprintf(f, "invertY %d\nfullscreen %d\nvsync %d\nshakeMouse %d\n",
            g.invertY ? 1 : 0, g.fullscreen ? 1 : 0, g.vsync ? 1 : 0, g.shakeByMouse ? 1 : 0);
    for (int i = 0; i < ACT_COUNT; i++) fprintf(f, "key %d %d\n", i, g.keys[i]);
    fclose(f);
}

void LoadSettings(void) {
    SetDefaults();
    FILE *f = fopen("settings.cfg", "r");
    if (!f) return;
    char key[32];
    while (fscanf(f, "%31s", key) == 1) {
        if      (!strcmp(key, "sens"))       { fscanf(f, "%f", &g.sens); }
        else if (!strcmp(key, "volume"))     { fscanf(f, "%f", &g.volMaster); }  // old format
        else if (!strcmp(key, "volMaster"))  { fscanf(f, "%f", &g.volMaster); }
        else if (!strcmp(key, "volMusic"))   { fscanf(f, "%f", &g.volMusic); }
        else if (!strcmp(key, "volSfx"))     { fscanf(f, "%f", &g.volSfx); }
        else if (!strcmp(key, "brightness")) { fscanf(f, "%f", &g.brightness); }
        else if (!strcmp(key, "fov"))        { fscanf(f, "%f", &g.fovSetting); }
        else if (!strcmp(key, "invertY"))    { int v; if (fscanf(f, "%d", &v) == 1) g.invertY = v != 0; }
        else if (!strcmp(key, "fullscreen")) { int v; if (fscanf(f, "%d", &v) == 1) g.fullscreen = v != 0; }
        else if (!strcmp(key, "vsync"))      { int v; if (fscanf(f, "%d", &v) == 1) g.vsync = v != 0; }
        else if (!strcmp(key, "shakeMouse")) { int v; if (fscanf(f, "%d", &v) == 1) g.shakeByMouse = v != 0; }
        else if (!strcmp(key, "key"))        {
            int idx, code;
            if (fscanf(f, "%d %d", &idx, &code) == 2 &&
                idx >= 0 && idx < ACT_COUNT && code > 0 && code < 512) g.keys[idx] = code;
        }
    }
    fclose(f);
    g.sens = CLAMP(g.sens, 0.2f, 20.0f);
    g.volMaster = CLAMP(g.volMaster, 0, 1);
    g.volMusic = CLAMP(g.volMusic, 0, 1);
    g.volSfx = CLAMP(g.volSfx, 0, 1);
    g.brightness = CLAMP(g.brightness, 0.5f, 1.5f);
    g.fovSetting = CLAMP(g.fovSetting, 60, 120);
}
void SaveScores(void) {
    FILE *f = fopen("scores.cfg", "w");
    if (!f) return;
    for (int i = 0; i < MAP_COUNT; i++) fprintf(f, "%d\n", g.bestRound[i]);
    fclose(f);
}
void LoadScores(void) {
    FILE *f = fopen("scores.cfg", "r");
    if (!f) return;
    for (int i = 0; i < MAP_COUNT; i++)
        if (fscanf(f, "%d", &g.bestRound[i]) != 1) g.bestRound[i] = 0;
    fclose(f);
}

// ---------------------------------------------------------------------------
// Game flow
// ---------------------------------------------------------------------------
void GameReset(void) {
    memset(&g.player, 0, sizeof(Player));
    memset(g.zombies, 0, sizeof(g.zombies));
    memset(g.particles, 0, sizeof(g.particles));
    g.round = 0;
    g.roundIntermission = 0;
    g.roundKills = g.roundQuota = g.roundSpawned = 0;
    g.boxInUse = false;
    g.boxSpinTimer = 0;
    g.boxResultWeapon = -1;
    g.boxUses = 0;
    g.hitMarker = g.damageFlash = g.muzzleFlash = 0;
    g.shakeTime = 0;
    g.ziplineT = 0;
    g.roundBannerActive = false;
    g.roundBannerTime = 0;
    SeedRng((uint32_t)(GetTime() * 1000) | 1);
}

void ApplyHardModeScene(void) {
    // hard mode: back to the old (pre-polish) darkness, torch is the lifeline
    if (!g.hardMode) return;
    Color amb = g.map.ambient;
    amb.r = (unsigned char)(amb.r * 0.35f);
    amb.g = (unsigned char)(amb.g * 0.35f);
    amb.b = (unsigned char)(amb.b * 0.35f);
    GfxSetScene(g.map.sunDir, g.map.sunColor, amb,
                Vec3(0, 1, 0), (Color){ 0, 0, 0, 255 },   // no fill light
                g.map.fogColor, g.map.fogDensity * 1.6f);
}

void TorchUpdate(void) {
    if (!g.hardMode) return;
    // charge feeds on shaking, drains over ~14 s from full
    g.torchCharge -= (float)g.dt / 14.0f;
    if (g.torchCharge < 0) g.torchCharge = 0;
    if (g.torchShakeAnim > 0) g.torchShakeAnim -= (float)g.dt * 3.0f;

    // input: mouse-move shaking or key mashing (Controls setting)
    bool shook = false;
    if (g.shakeByMouse) {
        Vector2 md = GetMouseDelta();
        float speed = fabsf(md.x) + fabsf(md.y);
        // vigorous swiping feeds the torch; gentle aiming barely does
        if (speed > 26.0f) {
            g.torchCharge += speed * 0.0011f * (float)g.dt * 60.0f;
            g.torchShakeAnim = 1.0f;
            shook = true;
        }
    } else {
        static bool shakeHeld = false;
        bool down = ActDown(ACT_SHAKE);
        if (down && !shakeHeld) {
            g.torchCharge += 0.035f;
            g.torchShakeAnim = 1.0f;
            shook = true;
        }
        shakeHeld = down;
    }
    if (g.torchCharge > 1) g.torchCharge = 1;
    (void)shook;
}

void GameStart(MapId id) {
    GameReset();
    g.mapId = id;
    MapLoad(id);
    PlayerSpawn();
    g.round = 1;
    ZombiesInitRound();
    g.roundBannerActive = true;
    g.roundBannerTime = 3.0f;
    GfxSetScene(g.map.sunDir, g.map.sunColor, g.map.ambient, g.map.fillDir, g.map.fillColor, g.map.fogColor, g.map.fogDensity);
    ApplyHardModeScene();
    g.fovCur = g.fovSetting;
    g.torchCharge = 0.85f;
    MusicStart(id);
    g.state = STATE_PLAYING;
}

// Torch light for the current frame (called right before GfxBegin).
static void UpdateTorchLight(Camera3D cam) {
    if (!g.hardMode) { GfxSetTorch(cam.position, Vec3(0,0,1), 0.0f); return; }
    // aim straight ahead from the camera
    Vector3 fwd = Vec3Norm(Vec3Sub(cam.target, cam.position));
    // charge -> strength with a soft knee; dead torch = nothing
    float s = g.torchCharge;
    float strength = s <= 0 ? 0 : (0.55f + 0.45f * s) * (2.6f + 2.4f * s);
    // flicker: worse the emptier it gets (once there's any charge)
    if (s > 0) {
        float flick = 1.0f - 0.35f * (1.0f - s) * (0.5f + 0.5f * sinf((float)g.time * 17.0f));
        strength *= flick;
    }
    // shake jolt briefly widens/brightens it
    if (g.torchShakeAnim > 0) strength *= 1.0f + 0.35f * g.torchShakeAnim;
    GfxSetTorch(cam.position, fwd, strength);
}

void GameOverSequence(void) {
    g.state = STATE_GAMEOVER;
    if (g.round > g.bestRound[g.mapId]) g.bestRound[g.mapId] = g.round;
    SaveScores();
    MusicStop();
    SfxPlay(SND_DOWN, 0.8f, 1.0f);
}

// ---------------------------------------------------------------------------
// Brightness: post-draw gamma-ish overlay (black veil or white lift)
// ---------------------------------------------------------------------------
static void ApplyBrightness(void) {
    if (g.brightness >= 0.995f && g.brightness <= 1.005f) return;
    int w = GetScreenWidth(), h = GetScreenHeight();
    if (g.brightness < 1.0f) {
        // darken: translucent black veil
        DrawRectangle(0, 0, w, h, (Color){ 0, 0, 0, (unsigned char)((1.0f - g.brightness) * 220) });
    } else {
        // brighten: additive-ish white lift via repeated soft passes
        float over = g.brightness - 1.0f;
        int passes = (int)(over / 0.05f) + 1;
        if (passes > 10) passes = 10;
        unsigned char a = (unsigned char)(over * 255.0f / (passes * 2.2f));
        for (int i = 0; i < passes; i++)
            DrawRectangle(0, 0, w, h, (Color){ 255, 250, 240, a });
    }
}

// ---------------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------------
static Camera3D BuildCamera(void) {
    Player *p = &g.player;
    Camera3D cam = { 0 };
    float bobY = sinf(p->bobPhase * 2) * 0.028f * (1 - p->adsBlend*0.8f);
    cam.position = Vec3(p->pos.x, p->pos.y + PLAYER_EYE + (p->downed ? -1.05f : 0) + bobY, p->pos.z);
    // shake
    if (g.shakeTime > 0) {
        float s = g.shakeAmp * (g.shakeTime / 0.25f);
        cam.position.x += RngRange(-s, s);
        cam.position.y += RngRange(-s, s);
    }
    float yawR = p->yaw * DEG2RAD, pitR = p->pitch * DEG2RAD;
    Vector3 fwd = Vec3(-sinf(yawR)*cosf(pitR), sinf(pitR), -cosf(yawR)*cosf(pitR));
    // recoil kick raises view slightly
    fwd.y += p->recoilKick * 0.6f;
    fwd = Vec3Norm(fwd);
    cam.target = Vec3Add(cam.position, fwd);
    cam.up = Vec3(0, 1, 0);
    // FOV: setting + ADS zoom
    int widx = p->weapons[p->curSlot].widx;
    float adsFov = (widx >= 0 && WEAPONS[widx].adsFov > 0) ? WEAPONS[widx].adsFov : 55.0f;
    float targetFov = LERP(g.fovSetting, adsFov, p->adsBlend);
    g.fovCur = LERP(g.fovCur, targetFov, (float)fmin(g.dt * 10.0, 1.0));
    cam.fovy = g.fovCur;
    cam.projection = CAMERA_PERSPECTIVE;
    return cam;
}

// ---------------------------------------------------------------------------
// Selftest: validate all maps, weapons table sanity, no window needed
// ---------------------------------------------------------------------------
static int RunSelftest(void) {
    int fails = 0;
    // weapon table
    for (int i = 0; i < GUN_COUNT_TOTAL; i++) {
        const WeaponDef *w = &WEAPONS[i];
        if (!w->name || !w->name[0]) { printf("FAIL weapon %d has no name\n", i); fails++; }
        if (w->damage <= 0) { printf("FAIL weapon %s damage %d\n", w->name, w->damage); fails++; }
        if (w->mode != FIRE_MELEE && w->mag <= 0) { printf("FAIL weapon %s mag %d\n", w->name, w->mag); fails++; }
        if (w->mode != FIRE_MELEE && w->rpm <= 0) { printf("FAIL weapon %s rpm %d\n", w->name, w->rpm); fails++; }
    }
    // knife rule: 150 dmg >= R1 hp (100), < 2*150 >= R2 hp (150) => 2 hits R2
    float r1 = ZombieHpForRound(1), r2 = ZombieHpForRound(2), r3 = ZombieHpForRound(3);
    printf("zombie HP: R1=%.0f R2=%.0f R3=%.0f | knife=150\n", r1, r2, r3);
    if (r1 > WEAPONS[W_KNIFE].damage) { printf("FAIL knife not 1-hit on R1\n"); fails++; }
    if (r2 <= WEAPONS[W_KNIFE].damage) { printf("FAIL knife 1-hits R2 (needs 2)\n"); fails++; }
    if (r2 > WEAPONS[W_KNIFE].damage * 2) { printf("FAIL knife needs >2 hits on R2\n"); fails++; }
    // 20 guns + knife
    int gunCount = 0;
    for (int i = 0; i < NUM_GUNS; i++) if (WEAPONS[i].name[0]) gunCount++;
    printf("guns: %d (+knife)\n", gunCount);
    if (gunCount != 20) { printf("FAIL need exactly 20 guns, got %d\n", gunCount); fails++; }
    // box weights
    int totalW = 0;
    for (int i = 0; i < NUM_GUNS; i++) totalW += WEAPONS[i].boxWeight;
    if (totalW <= 0) { printf("FAIL no weapons in mystery box\n"); fails++; }
    // maps
    for (int mapi = 0; mapi < MAP_COUNT; mapi++) {
        char err[128] = {0};
        // load map into g.map for nav functions, run checks
        bool ok = MapValidate((MapId)mapi, err, sizeof err);
        printf("map %d (%s): %s%s\n", mapi, mapi == 0 ? "Theater" : mapi == 1 ? "Bunker" : mapi == 2 ? "Graveyard" : "DieArise",
               ok ? "OK" : "FAIL: ", ok ? "" : err);
        if (!ok) fails++;
    }
    // settings sanity (persisted defaults round-trip)
    {
        Game check = g;   // save current
        g.volMaster = -1; g.sens = 99;   // out-of-range values
        LoadSettings();
        bool ok = g.sens >= 0.2f && g.sens <= 20.0f && g.volMaster >= 0 && g.volMaster <= 1;
        printf("settings clamp: %s\n", ok ? "OK" : "FAIL");
        if (!ok) fails++;
        g = check;   // restore
    }
    printf(fails == 0 ? "SELFTEST PASSED\n" : "SELFTEST FAILED (%d)\n", fails);
    return fails;
}

// ---------------------------------------------------------------------------
// Smoke test: run the game for N seconds headless, take screenshots
// ---------------------------------------------------------------------------
static int RunSmoke(const char *outdir, int mapId, float seconds) {
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(1280, 720, "smoke");
    InitAudioDevice();
    SetTargetFPS(60);
    LoadSettings(); LoadScores();
    SfxSetVolume(g.volMaster, g.volSfx);
    // CI/headless hook: SMOKE_HARD=1 runs the smoke in hard mode (torch test)
    if (getenv("SMOKE_HARD")) g.hardMode = true;
    AssetsLoad();
    HudInitFont();
    GfxInit();
    GameStart((MapId)mapId);
    // movement regression check: 0.25 s of walking must move ~0.9 m, not 60x that
    {
        Vector3 p0 = g.player.pos;
        Vector3 mvel = { 0.0f, 0.0f, PLAYER_WALK };
        float mgy;
        CollideMove(&g.player.pos, &mvel, PLAYER_RADIUS, PLAYER_HEIGHT, 0.25f, &mgy);
        float dz = g.player.pos.z - p0.z;
        printf("movement check: %.3f m over 0.25 s (walk %.2f m/s)\n", dz, PLAYER_WALK);
        if (dz < 0.35f || dz > 1.4f) { printf("SMOKE FAIL: movement speed broken\n"); CloseAudioDevice(); CloseWindow(); return 1; }
        g.player.pos = p0;
    }
    g.roundBannerActive = false;   // no banner in screenshots
    char path[256];
    int shots = 0;
    float t = 0;
    bool wroteOne = false;
    while (!WindowShouldClose() && t < seconds) {
        float dt = GetFrameTime();
        if (dt > 0.05f) dt = 0.05f;
        g.dt = dt; g.time = GetTime();
        // scripted input: look around slowly, no movement (spawn safety)
        g.player.yaw += dt * 12.0f;
        g.player.pitch = 5.0f;
        HudUpdate();
        TorchUpdate();
        ZombiesUpdate();
        BoxUpdate();
        // draw
        Camera3D cam = BuildCamera();
        UpdateTorchLight(cam);
        BeginDrawing();
        ClearBackground(g.map.fogColor);
        GfxDrawSky(cam, g.player.pitch);
        GfxBegin(cam);
        GfxDrawStatic();
        BoxDrawWorld();
        MapDrawInteractables();
        // closed doors drawn dynamically
        for (int i = 0; i < g.map.boxCount; i++)
            if (g.map.boxes[i].doorId > 0 && !g.map.doorOpen[g.map.boxes[i].doorId-1])
                GfxDrawBox(g.map.boxes[i].aabb, g.map.boxes[i].tex, WHITE);
        ZombiesDraw(cam);
        ParticlesDraw(cam);
        WeaponDrawViewmodel(cam);
        GfxEnd();
        HudDrawGameplay();
        EndDrawing();
        t += dt;
        // screenshot at intervals
        if (((int)(t * 2) != (int)((t - dt) * 2)) && shots < 3) {
            snprintf(path, sizeof path, "%s/smoke_map%d_%d.png", outdir, mapId, shots);
            TakeScreenshot(path);
            shots++;
        }
        wroteOne = shots > 0;
    }
    CloseAudioDevice();
    CloseWindow();
    printf("smoke map %d: %s (%d screenshots)\n", mapId, wroteOne ? "OK" : "NO SHOTS", shots);
    return wroteOne ? 0 : 1;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char **argv) {
    // modes
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--selftest")) {
            // no window: only validate data structures + nav
            memset(&g, 0, sizeof(g));
            LoadSettings(); LoadScores();
            return RunSelftest() == 0 ? 0 : 1;
        }
        if (!strcmp(argv[i], "--smoke") && i + 2 < argc) {
            const char *outdir = argv[i+1];
            int mapId = atoi(argv[i+2]);
            return RunSmoke(outdir, mapId, 4.0f);
        }
    }

    memset(&g, 0, sizeof(g));
    LoadSettings();          // read early: window flags depend on settings
    SetConfigFlags(FLAG_MSAA_4X_HINT | (g.vsync ? FLAG_VSYNC_HINT : 0) |
                   (g.fullscreen ? FLAG_FULLSCREEN_MODE : 0));
    InitWindow(1280, 720, GAME_TITLE);
    InitAudioDevice();
    SetTargetFPS(g.vsync ? 60 : 0);   // 0 = uncapped
    SetExitKey(KEY_NULL);      // handle ESC ourselves
    LoadScores();
    SfxSetVolume(g.volMaster, g.volSfx);
    AssetsLoad();
    HudInitFont();
    GfxInit();
    g.state = STATE_TITLE;

    while (!WindowShouldClose() && !g.windowShouldClose) {
        float dt = GetFrameTime();
        if (dt > 0.05f) dt = 0.05f;   // cap: no teleporting through walls on hitches
        g.dt = dt; g.time = GetTime();
        MusicUpdate();

        // cursor capture: grabbed in gameplay, free in menus/pause
        static GameState prevState = STATE_TITLE;
        if (g.state != prevState) {
            if (g.state == STATE_PLAYING) DisableCursor();
            else EnableCursor();
            prevState = g.state;
        }

        // global keys
        if (IsKeyPressed(KEY_ESCAPE)) {
            if (g.overlay != OVERLAY_NONE) g.overlay = OVERLAY_NONE;
            else if (g.state == STATE_SETTINGS && HudCapturingKey()) { /* swallow: rebinding */ }
            else if (g.state == STATE_PLAYING) { g.state = STATE_PAUSED; SaveSettings(); }
            else if (g.state == STATE_PAUSED) g.state = STATE_PLAYING;
            else if (g.state == STATE_MAPSELECT || g.state == STATE_SETTINGS) g.state = STATE_TITLE;
        }
        if (IsKeyPressed(KEY_F11)) ToggleFullscreen();

        // update
        switch (g.state) {
        case STATE_PLAYING:
            HudUpdate();
            TorchUpdate();
            if (!g.player.downed) WeaponsUpdate();
            else WeaponsUpdate(); // allow knife? no - skip fire while downed
            PlayerUpdate();
            ZombiesUpdate();
            BoxUpdate();
            break;
        case STATE_PAUSED:
        case STATE_TITLE:
        case STATE_MAPSELECT:
        case STATE_SETTINGS:
        case STATE_GAMEOVER:
            HudUpdate();
            break;
        default: break;
        }

        // draw
        BeginDrawing();
        if (g.state == STATE_PLAYING || g.state == STATE_PAUSED || g.state == STATE_GAMEOVER) {
            Camera3D cam = BuildCamera();
            UpdateTorchLight(cam);
            ClearBackground(g.map.fogColor);
            GfxDrawSky(cam, g.player.pitch);
            GfxBegin(cam);
            GfxDrawStatic();
            BoxDrawWorld();
            MapDrawInteractables();
            for (int i = 0; i < g.map.boxCount; i++)
                if (g.map.boxes[i].doorId > 0 && !g.map.doorOpen[g.map.boxes[i].doorId-1])
                    GfxDrawBox(g.map.boxes[i].aabb, g.map.boxes[i].tex, WHITE);
            ZombiesDraw(cam);
            ParticlesDraw(cam);
            WeaponDrawViewmodel(cam);
            GfxEnd();
            HudDrawGameplay();
        } else {
            ClearBackground(BLACK);
        }
        ApplyBrightness();
        HudDraw();
        EndDrawing();
    }

    SaveSettings(); SaveScores();
    AssetsUnload();
    GfxShutdown();
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
