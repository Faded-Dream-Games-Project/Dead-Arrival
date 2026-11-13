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
void SaveSettings(void) {
    FILE *f = fopen("settings.cfg", "w");
    if (!f) return;
    fprintf(f, "sens %.3f\nvolume %.3f\nfov %.1f\n", g.sens, g.volume, g.fovSetting);
    fclose(f);
}
void LoadSettings(void) {
    g.sens = 1.0f; g.volume = 0.8f; g.fovSetting = 75.0f;
    FILE *f = fopen("settings.cfg", "r");
    if (!f) return;
    char key[32];
    while (fscanf(f, "%31s", key) == 1) {
        if (!strcmp(key, "sens")) fscanf(f, "%f", &g.sens);
        else if (!strcmp(key, "volume")) fscanf(f, "%f", &g.volume);
        else if (!strcmp(key, "fov")) fscanf(f, "%f", &g.fovSetting);
    }
    fclose(f);
    g.sens = CLAMP(g.sens, 0.2f, 2.0f);
    g.volume = CLAMP(g.volume, 0, 1);
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

void GameStart(MapId id) {
    GameReset();
    g.mapId = id;
    MapLoad(id);
    PlayerSpawn();
    g.round = 1;
    ZombiesInitRound();
    g.roundBannerActive = true;
    g.roundBannerTime = 3.0f;
    GfxSetScene(g.map.sunDir, g.map.sunColor, g.map.ambient, g.map.fogColor, g.map.fogDensity);
    MusicStart(id);
    g.state = STATE_PLAYING;
}

void GameOverSequence(void) {
    g.state = STATE_GAMEOVER;
    if (g.round > g.bestRound[g.mapId]) g.bestRound[g.mapId] = g.round;
    SaveScores();
    MusicStop();
    SfxPlay(SND_DOWN, 0.8f, 1.0f);
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
    printf(fails == 0 ? "SELFTEST PASSED\n" : "SELFTEST FAILED (%d)\n", fails);
    return fails;
}

// ---------------------------------------------------------------------------
// Smoke test: run the game for N seconds headless, take screenshots
// ---------------------------------------------------------------------------
static int RunSmoke(const char *outdir, int mapId, float seconds) {
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(1280, 720, "smoke");
    SetTargetFPS(60);
    AssetsLoad();
    HudInitFont();
    LoadSettings(); LoadScores();
    GfxInit();
    GameStart((MapId)mapId);
    g.roundBannerActive = false;   // no banner in screenshots
    char path[256];
    int shots = 0;
    float t = 0;
    bool wroteOne = false;
    while (!WindowShouldClose() && t < seconds) {
        float dt = GetFrameTime();
        g.dt = dt; g.time = GetTime();
        // scripted input: look around slowly, no movement (spawn safety)
        g.player.yaw += dt * 12.0f;
        g.player.pitch = 5.0f;
        HudUpdate();
        ZombiesUpdate();
        BoxUpdate();
        // draw
        Camera3D cam = BuildCamera();
        BeginDrawing();
        ClearBackground(g.map.fogColor);
        GfxDrawSky(cam, g.player.pitch);
        GfxBegin(cam);
        GfxDrawStatic();
        BoxDrawWorld();
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
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(1280, 720, GAME_TITLE);
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);      // handle ESC ourselves
    AssetsLoad();
    HudInitFont();
    LoadSettings(); LoadScores();
    SfxSetVolume(g.volume);
    GfxInit();
    g.state = STATE_TITLE;

    while (!WindowShouldClose() && !g.windowShouldClose) {
        float dt = GetFrameTime();
        g.dt = dt; g.time = GetTime();
        MusicUpdate();

        // global keys
        if (IsKeyPressed(KEY_ESCAPE)) {
            if (g.overlay != OVERLAY_NONE) g.overlay = OVERLAY_NONE;
            else if (g.state == STATE_PLAYING) { g.state = STATE_PAUSED; SaveSettings(); }
            else if (g.state == STATE_PAUSED) g.state = STATE_PLAYING;
            else if (g.state == STATE_MAPSELECT || g.state == STATE_SETTINGS) g.state = STATE_TITLE;
        }
        if (IsKeyPressed(KEY_F11)) ToggleFullscreen();

        // update
        switch (g.state) {
        case STATE_PLAYING:
            HudUpdate();
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
            ClearBackground(g.map.fogColor);
            GfxDrawSky(cam, g.player.pitch);
            GfxBegin(cam);
            GfxDrawStatic();
            BoxDrawWorld();
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
        HudDraw();
        EndDrawing();
    }

    SaveSettings(); SaveScores();
    AssetsUnload();
    GfxShutdown();
    CloseWindow();
    return 0;
}
