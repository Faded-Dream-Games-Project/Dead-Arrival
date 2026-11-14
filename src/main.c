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
        particles_draw();
        player_draw_viewmodel(g.frame_time);
    EndMode3D();

    ui_draw_hud();
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

    InitWindow(1280, 720, APP_NAME " v" APP_VERSION);
    SetTargetFPS(60);
    SetTraceLogLevel(LOG_INFO);

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
    SetRandomSeed(42);
    game_boot(true);
    if (!IsWindowReady()) return 2;

    game_start_map(mapfile);
    if (!g.map_loaded) { game_shutdown(); return 2; }

    // skip the pre-round grace so spawns begin immediately
    g.round_break_t = 0.0f;
    g.frame_time = 1.0f / 60.0f;

    for (int f = 0; f < frames && !WindowShouldClose(); f++)
    {
        g.time += g.frame_time;
        game_update_playing(g.frame_time);
        BeginDrawing();
        game_draw_playing();
        EndDrawing();
    }

    const char *png = outpng ? outpng : "autotest.png";
    TakeScreenshot(png);

    int spawned = zombies_total_spawned();
    int alive = zombies_alive_count();
    int round = g.round;
    int pts = g_player.points;
    int kills = g_player.kills;
    int hits = g_player.hits;

    printf("AUTOTEST map=%s frames=%d round=%d spawned=%d alive=%d kills=%d hits=%d points=%d hp=%d\n",
           mapfile, frames, round, spawned, alive, kills, hits, pts, g_player.hp);

    game_shutdown();

    // sanity checks: zombies must spawn and combat must engage
    bool ok = (spawned > 0) && (round >= 1) && (hits > 0);
    printf("AUTOTEST RESULT: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
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

    if (!game_boot(false)) return 1;

    while (!WindowShouldClose() && !g.want_quit)
    {
        float dt = GetFrameTime();
        if (dt > 0.05f) dt = 0.05f;      // clamp huge hitches
        g.frame_time = dt;
        g.time += dt;

        // update
        if (g.state == GS_PLAYING) game_update_playing(dt);
        else ui_update();

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
