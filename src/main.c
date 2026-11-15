// main.c - entry point, CLI (--list-maps / --autotest), main loop and the
// playing-state game flow (start map, update, draw, death handling).
#include "game.h"

Game g;
static float s_death_timer = 0;

// ---------------------------------------------------------------------------
// Game flow
// ---------------------------------------------------------------------------
void game_start_map(const char *file)
{
    if (g.map_loaded) map_unload(&g.map);

    char path[512];
    snprintf(path, sizeof(path), "%s/maps/%s", ASSETS_DIR, file);
    if (!map_load(&g.map, path))
    {
        TraceLog(LOG_ERROR, "GAME: failed to load map %s", file);
        return;
    }
    g.map_loaded = true;
    snprintf(g.cur_map_file, sizeof(g.cur_map_file), "%s", file);

    world_init();
    particles_reset();
    zombies_reset();
    nav_build(&g.map);      // walkability grid for zombie pathfinding
    player_reset(g.map.player_spawn, g.map.player_yaw);

    g.round = 0;
    g.round_active = false;
    g.round_break_t = 3.5f;    // short grace period before round 1
    g.state = GS_PLAYING;
    s_death_timer = 0;
    DisableCursor();
    sfx_play("round_start", 0.9f, 1.0f);
    TraceLog(LOG_INFO, "GAME: started map '%s' (%s)", g.map.name, file);
}

void game_end_to_menu(void)
{
    if (g.map_loaded) map_unload(&g.map);
    nav_unload();
    g.map_loaded = false;
    if (g.round > g.best_round) g.best_round = g.round;
    g.state = GS_MENU;
    EnableCursor();
}

void game_update_playing(float dt)
{
    // player death sequence
    if (g_player.hp <= 0)
    {
        s_death_timer += dt;
        // camera sinks a little, then stops (do not fall through the floor)
        if (s_death_timer < 1.5f) g_player.pos.y -= dt * 0.8f;
        zombies_update(dt, false);
        player_update_grenades(dt);
        particles_update(dt);
        if (s_death_timer > 1.8f && g.state == GS_PLAYING)
        {
            g.state = GS_DEAD;
            EnableCursor();
            if (g.round > g.best_round) g.best_round = g.round;
        }
        player_update(dt, false);
        return;
    }

    player_update(dt, true);
    player_update_grenades(dt);
    zombies_update_round(dt);
    zombies_update(dt, g_player.hp > 0);
    world_update(dt);
    particles_update(dt);
}

void game_draw_playing(void)
{
    ClearBackground(g.map.sky);

    BeginMode3D(g_camera);
        world_draw(&g.map);
        zombies_draw();
        player_draw_grenades();
        particles_draw();
        player_draw_viewmodel(g.frame_time);
    EndMode3D();

    ui_draw_hud();
}

// grenade detonation: AoE zombie damage + visual/audio burst
void game_explode_at(Vector3 pos)
{
    sfx_play_at("explosion", pos, 1.0f);
    particles_spawn_sparks(pos, 40);
    particles_spawn_debris(pos, 24);
    zombies_apply_explosion(pos, GRENADE_RADIUS, GRENADE_DAMAGE);
    // mild flash/shake when the player is close
    float pd = Vector3Distance(g_player.pos, pos);
    if (pd < GRENADE_RADIUS)
        g_player.hurt_t = fmaxf(g_player.hurt_t, 0.3f * (1.0f - pd / GRENADE_RADIUS));
}

// ---------------------------------------------------------------------------
// Startup: discover maps, load weapons, etc. Returns false on fatal error.
// ---------------------------------------------------------------------------
static bool game_boot(bool hidden)
{
    settings_load(&g_settings);

    g_camera.projection = CAMERA_PERSPECTIVE;
    g_camera.fovy = g_settings.fov;
    g_camera.up = (Vector3){ 0, 1, 0 };
    g_camera.position = (Vector3){ 0, PLAYER_EYE, 0 };
    g_camera.target = (Vector3){ 0, PLAYER_EYE, -1 };

    if (hidden) SetConfigFlags(FLAG_WINDOW_HIDDEN);
    if (g_settings.fullscreen && !hidden) SetConfigFlags(FLAG_FULLSCREEN_MODE);

    InitWindow(1280, 720, APP_NAME " (" APP_VERSION ")");
    SetTargetFPS(60);
    SetTraceLogLevel(LOG_INFO);

    // raylib binds ESC to WindowShouldClose() by default, which would kill the
    // game from any screen. We handle ESC ourselves (pause / back out of menus).
    SetExitKey(0);

    // raylib 6.x does NOT auto-initialize the audio device: without this call
    // every LoadSound fails ("Failed to create data conversion pipeline").
    InitAudioDevice();
    if (IsAudioDeviceReady())
    {
        TraceLog(LOG_INFO, "GAME: audio device ready");
        SetMasterVolume(g_settings.volume_master);
    }
    else
    {
        TraceLog(LOG_WARNING, "GAME: audio device unavailable, running silent");
    }

    assets_load_all();

    char wpath[512];
    snprintf(wpath, sizeof(wpath), "%s/weapons.cfg", ASSETS_DIR);
    if (weapons_load(wpath) < 20)
        TraceLog(LOG_WARNING, "GAME: fewer than 20 weapons loaded, check weapons.cfg");
    weapons_log_summary();

    // map discovery (data-driven: every .map in assets/maps shows up)
    char files[MAX_MAPS][MAX_NAME];
    int n = map_list_dir(files, MAX_MAPS);
    g.map_count = 0;
    for (int i = 0; i < n && i < MAX_MAPS; i++)
    {
        memcpy(g.map_entries[i].file, files[i], MAX_NAME);
        map_peek(files[i], g.map_entries[i].name, 48, g.map_entries[i].desc, 128);
        g.map_count++;
        TraceLog(LOG_INFO, "GAME: map slot %d: %s", i, g.map_entries[i].name);
    }
    if (g.map_count == 0)
        TraceLog(LOG_WARNING, "GAME: no maps found in %s/maps", ASSETS_DIR);

    g.state = GS_SPLASH;
    g.frame_time = 0.016f;
    return true;
}

static void game_shutdown(void)
{
    if (g.map_loaded) map_unload(&g.map);
    assets_unload_all();
    if (IsAudioDeviceReady()) CloseAudioDevice();
    CloseWindow();
}

// ---------------------------------------------------------------------------
// --list-maps (no window needed)
// ---------------------------------------------------------------------------
static int cli_list_maps(void)
{
    char files[MAX_MAPS][MAX_NAME];
    int n = map_list_dir(files, MAX_MAPS);
    printf("maps in %s/maps: %d\n", ASSETS_DIR, n);
    for (int i = 0; i < n; i++)
    {
        char name[48], desc[128];
        map_peek(files[i], name, sizeof(name), desc, sizeof(desc));
        printf("  %-22s %s - %s\n", files[i], name, desc);
    }
    return (n > 0) ? 0 : 1;
}

// ---------------------------------------------------------------------------
// --autotest <map.map> <frames> [out.png] : headless regression run
// ---------------------------------------------------------------------------
static int cli_autotest(const char *mapfile, int frames, const char *outpng)
{
    g.autotest = true;
    game_boot(true);
    // NOTE: seed AFTER game_boot - InitWindow() reseeds raylib's RNG with the
    // wall clock (rcore.c), which would make this test nondeterministic
    SetRandomSeed(42);
    if (!IsWindowReady()) return 2;

    game_start_map(mapfile);
    if (!g.map_loaded) { game_shutdown(); return 2; }

    // skip the pre-round grace so spawns begin immediately
    g.round_break_t = 0.0f;
    g.frame_time = 1.0f / 60.0f;
    // test funds so the scripted buys can exercise doors/perks/PaP
    g_player.points = 6000;

    for (int f = 0; f < frames && !WindowShouldClose(); f++)
    {
        g.time += g.frame_time;
        // exercise the new systems headlessly: buy whatever is in range
        // every couple of seconds and throw one grenade mid-run
        if (f > 0 && f % 120 == 0 && player_interact_prompt()[0])
            player_try_interact();
        if (f == 900) player_throw_grenade();
        game_update_playing(g.frame_time);
        BeginDrawing();
        game_draw_playing();
        EndDrawing();
    }

    const char *png = outpng ? outpng : "autotest.png";
    TakeScreenshot(png);

    // log where each barrier projects on screen (barrier render verification)
    for (int i = 0; i < g.map.barrier_count; i++)
    {
        const Barrier *b = &g.map.barriers[i];
        Vector3 c = { b->pos.x, b->pos.y + 1.0f, b->pos.z };
        Vector2 s = GetWorldToScreen(c, g_camera);
        printf("BARRIER %d screen=%.0f,%.0f onscreen=%d\n", i, s.x, s.y,
               (s.x >= 0 && s.x < GetScreenWidth() && s.y >= 0 && s.y < GetScreenHeight()) ? 1 : 0);
    }

    // same for mystery boxes (beacon verification)
    for (int i = 0; i < g.map.mbox_count; i++)
    {
        Vector3 c = g.map.mboxes[i].pos;
        Vector2 s = GetWorldToScreen(c, g_camera);
        printf("MBOX %d screen=%.0f,%.0f onscreen=%d\n", i, s.x, s.y,
               (s.x >= 0 && s.x < GetScreenWidth() && s.y >= 0 && s.y < GetScreenHeight()) ? 1 : 0);
    }

    int spawned = zombies_total_spawned();
    int alive = zombies_alive_count();
    int round = g.round;
    int pts = g_player.points;
    int kills = g_player.kills;
    int hits = g_player.hits;

    printf("AUTOTEST map=%s frames=%d round=%d spawned=%d alive=%d kills=%d hits=%d points=%d hp=%d\n",
           mapfile, frames, round, spawned, alive, kills, hits, pts, g_player.hp);

    {
        int doors_open = 0;
        for (int i = 0; i < g.map.door_count; i++)
            if (g.map.doors[i].open) doors_open++;
        printf("AUTOTEST SYSTEMS perks=%d%d%d%d doors=%d/%d pap=%d grenades=%d\n",
               g_player.perks[0], g_player.perks[1], g_player.perks[2], g_player.perks[3],
               doors_open, g.map.door_count,
               (g_player.slots[0].upgraded || g_player.slots[1].upgraded) ? 1 : 0,
               g_player.grenades);
    }

    game_shutdown();

    // sanity checks: zombies must spawn and combat must engage
    bool ok = (spawned > 0) && (round >= 1) && (hits > 0);
    printf("AUTOTEST RESULT: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

// ---------------------------------------------------------------------------
// --pathtest <map.map> <seconds> [out.png] : zombie reachability run.
// The player stands still at the spawn (invulnerable, never fires); one
// zombie is placed at every spawn point / barrier. Verifies pathfinding:
// every zombie must reach melee range of the player.
// ---------------------------------------------------------------------------
static int cli_pathtest(const char *mapfile, int seconds, const char *outpng)
{
    // "reached" = closed to melee distance; with a whole horde crowding one
    // stationary target, separation keeps the outer ring slightly back, so
    // this is a bit beyond ZOMBIE_ATTACK_RANGE
    const float PATHTEST_REACH = 2.0f;
    g.autotest = true;
    g.pathtest = true;
    game_boot(true);
    SetRandomSeed(42);
    if (!IsWindowReady()) return 2;

    game_start_map(mapfile);
    if (!g.map_loaded) { game_shutdown(); return 2; }

    // freeze the round flow: exactly the zombies we place, nothing more
    g.round_break_t = 1e9f;
    g.frame_time = 1.0f / 60.0f;
    g_player.hp = 10000000;     // invulnerable: measure reachability, not survival

    int npts = 0;
    if (g.map.spawn_mode == SPAWN_BARRIERS)
    {
        // navigation test: open every door and pre-tear every barrier so
        // all spawn points are connected to the player
        for (int i = 0; i < g.map.door_count; i++) g.map.doors[i].open = true;
        for (int i = 0; i < g.map.barrier_count; i++)
        {
            g.map.barriers[i].boards = 0;
            zombies_spawn(i, true);
            npts++;
        }
    }
    else
    {
        for (int i = 0; i < g.map.spawn_count; i++)
        {
            zombies_spawn(i, false);
            npts++;
        }
    }

    float min_dist[MAX_ZOMBIES];
    float reach_t[MAX_ZOMBIES];
    Vector3 final_pos[MAX_ZOMBIES];
    for (int i = 0; i < MAX_ZOMBIES; i++)
    {
        min_dist[i] = 1e9f;
        reach_t[i] = -1.0f;
        final_pos[i] = (Vector3){ 0, 0, 0 };
    }

    Vector3 spawn_pos = g_player.pos;
    int frames = seconds * 60;
    for (int f = 0; f < frames && !WindowShouldClose(); f++)
    {
        g.time += g.frame_time;
        game_update_playing(g.frame_time);
        for (int i = 0; i < npts; i++)
        {
            Vector3 zp;
            if (!zombies_slot_pos(i, &zp)) continue;
            final_pos[i] = zp;
            float d = v3_dist_xz(zp, g_player.pos);
            if (d < min_dist[i])
            {
                min_dist[i] = d;
                if (d <= PATHTEST_REACH && reach_t[i] < 0.0f)
                    reach_t[i] = g.time;
            }
        }
        // periodic trace of the zombies that have not reached the player yet
        if (f % 120 == 0)
        {
            printf("T+%04.1f", g.time);
            for (int i = 0; i < npts; i++)
            {
                Vector3 zp;
                int st = -1; float stk = -1, det = -1;
                if (!zombies_slot_debug(i, &zp, &st, &stk, &det)) continue;
                if (min_dist[i] <= PATHTEST_REACH) continue;
                printf(" | z%d(%.1f,%.1f)s%dk%.1fd%.1f", i, zp.x, zp.z, st, stk, det);
            }
            printf("\n");
        }
        BeginDrawing();
        game_draw_playing();
        EndDrawing();
    }

    if (outpng) TakeScreenshot(outpng);

    float pdrift = v3_dist_xz(g_player.pos, spawn_pos);
    int reached = 0;
    printf("PATHTEST map=%s zombies=%d seconds=%d player_drift=%.2f player_y=%.2f\n",
           mapfile, npts, seconds, pdrift, g_player.pos.y);
    for (int i = 0; i < npts; i++)
    {
        int st = -1; float stk = -1, det = -1;
        Vector3 zp;
        zombies_slot_debug(i, &zp, &st, &stk, &det);
        printf("PATHTEST z%02d min_dist=%6.2f reached=%s t=%.1f end=(%.1f, %.1f) "
               "state=%d stuck=%.2f detour=%.2f\n",
               i, min_dist[i],
               (min_dist[i] <= PATHTEST_REACH) ? "YES" : "NO",
               reach_t[i], final_pos[i].x, final_pos[i].z, st, stk, det);
        if (min_dist[i] <= PATHTEST_REACH) reached++;
    }
    bool ok = (npts > 0) && (reached == npts) && (pdrift < 2.0f);
    printf("PATHTEST RESULT: %s (%d/%d reached)\n", ok ? "PASS" : "FAIL", reached, npts);

    game_shutdown();
    return ok ? 0 : 1;
}

// ---------------------------------------------------------------------------
// --navdump <map.map> <px> <py> <pz> : ASCII dump of the nav flow field from
// a given player position (pathfinding debugging)
// ---------------------------------------------------------------------------
static int cli_navdump(const char *mapfile, float px, float py, float pz)
{
    g.autotest = true;
    game_boot(true);
    SetRandomSeed(42);
    if (!IsWindowReady()) return 2;

    game_start_map(mapfile);
    if (!g.map_loaded) { game_shutdown(); return 2; }

    for (int i = 0; i < g.map.door_count; i++) g.map.doors[i].open = true;
    for (int i = 0; i < g.map.barrier_count; i++) g.map.barriers[i].boards = 0;

    nav_update(&g.map, (Vector3){ px, py, pz }, 1.0f);
    nav_debug_dump();

    game_shutdown();
    return 0;
}

// ---------------------------------------------------------------------------
// --zombietrace <map.map> <zx> <zy> <zz> <seconds> : one zombie from a given
// position, player at spawn; prints its state/steering each 0.5 s
// ---------------------------------------------------------------------------
static int cli_zombietrace(const char *mapfile, float zx, float zy, float zz,
                           int seconds)
{
    g.autotest = true;
    g.pathtest = true;
    game_boot(true);
    SetRandomSeed(42);
    if (!IsWindowReady()) return 2;

    game_start_map(mapfile);
    if (!g.map_loaded) { game_shutdown(); return 2; }

    g.round_break_t = 1e9f;
    g.frame_time = 1.0f / 60.0f;
    g_player.hp = 10000000;

    for (int i = 0; i < g.map.door_count; i++) g.map.doors[i].open = true;
    for (int i = 0; i < g.map.barrier_count; i++) g.map.barriers[i].boards = 0;

    Zombie *z = zombies_spawn(0, true);
    if (!z) { game_shutdown(); return 2; }
    z->pos = (Vector3){ zx, zy, zz };
    z->state = ZSTATE_CHASE;
    z->speed = 1.5f;
    z->runner = false;

    Vector3 tgt = { 0 };
    printf("TRACE map=%s zombie=(%.1f, %.1f, %.1f) player=(%.1f, %.1f, %.1f)\n",
           mapfile, zx, zy, zz, g_player.pos.x, g_player.pos.y, g_player.pos.z);

    int frames = seconds * 60;
    for (int f = 0; f < frames && !WindowShouldClose(); f++)
    {
        g.time += g.frame_time;
        game_update_playing(g.frame_time);

        if (f % 30 == 0)
        {
            float d = v3_dist_xz(z->pos, g_player.pos);
            bool guided = nav_steer(&g.map, z->pos, &tgt);
            printf("TRACE t=%5.1f pos=(%6.2f,%6.2f,%6.2f) state=%d dist=%5.2f "
                   "nav=%d tgt=(%.1f,%.1f) stuck=%.1f\n",
                   g.time, z->pos.x, z->pos.y, z->pos.z, (int)z->state, d,
                   guided, tgt.x, tgt.z, z->stuck_t);
        }
        BeginDrawing();
        game_draw_playing();
        EndDrawing();
    }

    game_shutdown();
    return 0;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// --autotest-menu <menu|mapselect|settings|pause> [out.png] : UI screenshot
// ---------------------------------------------------------------------------
static int cli_menu_shot(const char *screen, const char *outpng)
{
    g.autotest = true;
    SetRandomSeed(42);
    game_boot(true);
    if (!IsWindowReady()) return 2;

    if (strcmp(screen, "pause") == 0)
    {
        game_start_map("theater.map");
        if (!g.map_loaded) { game_shutdown(); return 2; }
        g.state = GS_PAUSED;
    }
    else if (strcmp(screen, "settings") == 0)  g.state = GS_SETTINGS;
    else if (strcmp(screen, "mapselect") == 0) g.state = GS_MAPSELECT;
    else                                        g.state = GS_MENU;

    for (int f = 0; f < 45 && !WindowShouldClose(); f++)
    {
        g.frame_time = 1.0f / 60.0f;
        g.time += g.frame_time;
        ui_update();
        BeginDrawing();
        if (g.state == GS_PAUSED && g.map_loaded) game_draw_playing();
        ui_draw();
        EndDrawing();
    }

    const char *png = outpng ? outpng : "menu.png";
    TakeScreenshot(png);
    printf("MENUSHOT screen=%s -> %s\n", screen, png);
    game_shutdown();
    return 0;
}

int main(int argc, char **argv)
{
    memset(&g, 0, sizeof(g));

    if (argc >= 2 && strcmp(argv[1], "--list-maps") == 0)
        return cli_list_maps();

    if (argc >= 4 && strcmp(argv[1], "--autotest") == 0)
    {
        int frames = atoi(argv[3]);
        if (frames < 60) frames = 60;
        return cli_autotest(argv[2], frames, (argc >= 5) ? argv[4] : NULL);
    }

    if (argc >= 4 && strcmp(argv[1], "--pathtest") == 0)
    {
        int secs = atoi(argv[3]);
        if (secs < 5) secs = 5;
        return cli_pathtest(argv[2], secs, (argc >= 5) ? argv[4] : NULL);
    }

    if (argc >= 6 && strcmp(argv[1], "--navdump") == 0)
    {
        int rc = cli_navdump(argv[2], (float)atof(argv[3]),
                             (float)atof(argv[4]), (float)atof(argv[5]));
        // bonus: if more coordinates follow, print their cell details
        for (int a = 6; a + 1 < argc; a += 2)
            nav_debug_cell((float)atof(argv[a]), (float)atof(argv[a + 1]));
        return rc;
    }

    if (argc >= 7 && strcmp(argv[1], "--zombietrace") == 0)
        return cli_zombietrace(argv[2], (float)atof(argv[3]),
                               (float)atof(argv[4]), (float)atof(argv[5]),
                               atoi(argv[6]));

    if (argc >= 3 && strcmp(argv[1], "--autotest-menu") == 0)
        return cli_menu_shot(argv[2], (argc >= 4) ? argv[3] : NULL);

    if (!game_boot(false)) return 1;

    while (!WindowShouldClose() && !g.want_quit)
    {
        float dt = GetFrameTime();
        if (dt > 0.05f) dt = 0.05f;      // clamp huge hitches
        g.frame_time = dt;
        g.time += dt;

        // update: UI first (ESC/pause handling + menu input), then gameplay
        // only while actually playing (ui_update may switch out of GS_PLAYING)
        ui_update();
        if (g.state == GS_PLAYING) game_update_playing(dt);

        // draw
        BeginDrawing();
        if (g.state == GS_PLAYING || g.state == GS_DEAD || g.state == GS_PAUSED)
        {
            if (g.map_loaded) game_draw_playing();
            ui_draw();                    // overlays: pause / death screens
        }
        else ui_draw();
        EndDrawing();
    }

    game_shutdown();
    return 0;
}
