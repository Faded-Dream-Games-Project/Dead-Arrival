// ui.c - menus (title, map select, settings, extras), the in-game HUD and
// point popups. Menus are mouse+keyboard; HUD shows points, round, ammo,
// health vignette and interaction prompts.
#include "game.h"

static int  s_focus = 0;             // keyboard focus index for the current menu
static int  s_settings_from = 0;     // state to return to when leaving settings
static float s_splash_t = 0;

// point popups ("+50")
#define MAX_POPUPS 12
typedef struct { Vector2 screen; float t; char text[16]; } Popup;
static Popup s_popups[MAX_POPUPS];

void ui_point_popup(Vector3 world_pos, int amount)
{
    Vector2 s = GetWorldToScreen(world_pos, g_camera);
    for (int i = 0; i < MAX_POPUPS; i++)
    {
        if (s_popups[i].t > 0) continue;
        s_popups[i].screen = s;
        s_popups[i].t = 1.0f;
        snprintf(s_popups[i].text, sizeof(s_popups[i].text), "+%d", amount);
        return;
    }
}

// ---------------------------------------------------------------------------
// Immediate-mode button helper (mouse + keyboard focus)
// ---------------------------------------------------------------------------
static bool ui_button(const char *label, Rectangle r, bool enabled, int idx, int total)
{
    (void)total;
    Vector2 m = GetMousePosition();
    bool hover = CheckCollisionPointRec(m, r) && enabled;
    if (hover) s_focus = idx;

    Color base  = enabled ? (Color){ 30, 30, 36, 220 } : (Color){ 20, 20, 24, 160 };
    Color frame = enabled ? (Color){ 180, 40, 40, 255 } : (Color){ 70, 70, 74, 200 };
    Color textc = enabled ? RAYWHITE : (Color){ 120, 120, 126, 255 };

    if (hover || (s_focus == idx && enabled))
    {
        base = (Color){ 56, 20, 20, 235 };
        frame = (Color){ 255, 90, 70, 255 };
    }
    DrawRectangleRec(r, base);
    DrawRectangleLinesEx(r, 2, frame);

    int fs = 22;
    int tw = MeasureText(label, fs);
    DrawText(label, (int)(r.x + (r.width - tw) / 2), (int)(r.y + r.height / 2 - fs / 2), fs, textc);

    bool clicked = false;
    if (enabled && hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) clicked = true;
    return clicked;
}

static bool ui_key_enter(void) { return IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER); }

static void menu_nav(int total)
{
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) s_focus = (s_focus + 1) % total;
    if (IsKeyPressed(KEY_UP)   || IsKeyPressed(KEY_W)) s_focus = (s_focus - 1 + total) % total;
}

// ---------------------------------------------------------------------------
// Settings helpers (rows: 0 sens, 1 fov, 2 master vol, 3 sfx vol,
// 4 invert Y, 5 show fps, 6 fullscreen, 7 back)
// ---------------------------------------------------------------------------
enum { SET_ROW_SENS = 0, SET_ROW_FOV, SET_ROW_MASTER, SET_ROW_SFX,
       SET_ROW_INVERT, SET_ROW_FPS, SET_ROW_FULLSCREEN, SET_ROW_BACK, SET_ROWS };

static void settings_apply_fullscreen(void)
{
    if (!IsWindowReady()) return;
    if (g_settings.fullscreen != IsWindowFullscreen()) ToggleFullscreen();
}

static void settings_adjust(int row, int dir)
{
    switch (row)
    {
        case SET_ROW_SENS:
            g_settings.sensitivity = clampf(g_settings.sensitivity + 0.1f * dir, 0.3f, 4.0f);
            break;
        case SET_ROW_FOV:
            g_settings.fov = clampf(g_settings.fov + 5.0f * dir, 60.0f, 110.0f);
            break;
        case SET_ROW_MASTER:
            g_settings.volume_master = clampf(g_settings.volume_master + 0.05f * dir, 0.0f, 1.0f);
            SetMasterVolume(g_settings.volume_master);
            break;
        case SET_ROW_SFX:
            g_settings.volume_sfx = clampf(g_settings.volume_sfx + 0.05f * dir, 0.0f, 1.0f);
            break;
        case SET_ROW_INVERT:     if (dir != 0) g_settings.invert_y = !g_settings.invert_y; break;
        case SET_ROW_FPS:        if (dir != 0) g_settings.show_fps = !g_settings.show_fps; break;
        case SET_ROW_FULLSCREEN:
            if (dir != 0)
            {
                g_settings.fullscreen = !g_settings.fullscreen;
                settings_apply_fullscreen();
            }
            break;
        default: break;
    }
}

static void settings_leave(void)
{
    settings_save(&g_settings);
    // return to wherever settings was opened from (main menu or pause menu)
    g.state = (s_settings_from == GS_PAUSED) ? GS_PAUSED : GS_MENU;
    s_focus = 0;
}

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------
void ui_update(void)
{
    // menu music lifecycle (not while playing, dead or paused)
    bool in_menu = (g.state != GS_PLAYING && g.state != GS_DEAD && g.state != GS_PAUSED);
    assets_menu_music(in_menu);
    if (in_menu) assets_menu_music_update();

    switch (g.state)
    {
        case GS_SPLASH:
        {
            s_splash_t += g.frame_time;
            if (s_splash_t > 1.6f || IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER))
            {
                g.state = GS_MENU;
                s_focus = 0;
            }
        } break;

        case GS_MENU:
        {
            menu_nav(5);
            if (IsKeyPressed(KEY_ESCAPE)) g.want_quit = true;
            if (ui_key_enter())
            {
                switch (s_focus)
                {
                    case 0: g.state = GS_MAPSELECT; s_focus = 0; break;
                    case 1: /* multiplayer: coming soon */ break;
                    case 2: g.state = GS_SETTINGS; s_settings_from = GS_MENU; s_focus = 0; break;
                    case 3: g.state = GS_EXTRAS; break;
                    case 4: g.want_quit = true; break;
                }
            }
        } break;

        case GS_MAPSELECT:
        {
            if (g.map_count == 0) { if (IsKeyPressed(KEY_ESCAPE)) g.state = GS_MENU; break; }
            menu_nav(g.map_count + 1);
            if (IsKeyPressed(KEY_ESCAPE)) { g.state = GS_MENU; break; }
            if (ui_key_enter())
            {
                if (s_focus < g.map_count)
                {
                    game_start_map(g.map_entries[s_focus].file);
                }
                else g.state = GS_MENU;
            }
        } break;

        case GS_SETTINGS:
        {
            menu_nav(SET_ROWS);
            if (IsKeyPressed(KEY_LEFT))  settings_adjust(s_focus, -1);
            if (IsKeyPressed(KEY_RIGHT)) settings_adjust(s_focus, +1);
            if (IsKeyPressed(KEY_ESCAPE) || (ui_key_enter() && s_focus == SET_ROW_BACK))
                settings_leave();
        } break;

        case GS_EXTRAS:
        {
            if (IsKeyPressed(KEY_ESCAPE) || ui_key_enter()) g.state = GS_MENU;
        } break;

        case GS_PLAYING:
        {
            // gameplay input lives in player.c; ESC opens the pause menu
            if (IsKeyPressed(KEY_ESCAPE))
            {
                g.state = GS_PAUSED;
                s_focus = 0;
                EnableCursor();
            }
        } break;

        case GS_PAUSED:
        {
            menu_nav(4);
            if (IsKeyPressed(KEY_ESCAPE)) { g.state = GS_PLAYING; DisableCursor(); }
            if (ui_key_enter())
            {
                if (s_focus == 0) { g.state = GS_PLAYING; DisableCursor(); }
                else if (s_focus == 1) { g.state = GS_SETTINGS; s_settings_from = GS_PAUSED; s_focus = 0; }
                else if (s_focus == 2) game_end_to_menu();
                else g.want_quit = true;
            }
        } break;

        case GS_DEAD:
        {
            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ESCAPE))
                game_end_to_menu();
        } break;
    }
}

// ---------------------------------------------------------------------------
// Draw helpers
// ---------------------------------------------------------------------------
static void draw_centered_text(const char *txt, int y, int size, Color c)
{
    int tw = MeasureText(txt, size);
    DrawText(txt, GetScreenWidth() / 2 - tw / 2, y, size, c);
}

static void draw_menu_background(void)
{
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), (Color){ 12, 12, 16, 255 });
    // faint noise stripes for atmosphere
    for (int i = 0; i < GetScreenWidth(); i += 64)
        DrawRectangle(i, 0, 2, GetScreenHeight(), (Color){ 18, 16, 20, 255 });
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------
void ui_draw(void)
{
    int W = GetScreenWidth(), H = GetScreenHeight();
    char buf[128];

    switch (g.state)
    {
        case GS_SPLASH:
        {
            DrawRectangle(0, 0, W, H, BLACK);
            int a = (int)clampf(s_splash_t * 255, 0, 255);
            draw_centered_text("FADED DREAM GAMES PROJECT", H / 2 - 40, 26,
                               (Color){ a, a, a, 255 });
            draw_centered_text("presents", H / 2 + 4, 18, (Color){ a / 2, a / 2, a / 2, 255 });
        } break;

        case GS_MENU:
        {
            draw_menu_background();
            draw_centered_text("DEAD ARRIVAL", H / 2 - 190, 64, (Color){ 190, 30, 30, 255 });
            draw_centered_text(APP_TAGLINE, H / 2 - 120, 18, (Color){ 150, 150, 156, 255 });

            Rectangle r = { W / 2.0f - 160, H / 2 - 40.0f, 320, 46 };
            const char *items[5] = {
                "SOLO",
                "MULTIPLAYER  [COMING SOON]",
                "SETTINGS",
                "EXTRAS",
                "QUIT"
            };
            for (int i = 0; i < 5; i++)
            {
                bool enabled = (i != 1);   // multiplayer disabled
                if (ui_button(items[i], r, enabled, i, 5))
                {
                    switch (i)
                    {
                        case 0: g.state = GS_MAPSELECT; s_focus = 0; break;
                        case 2: g.state = GS_SETTINGS; s_focus = 0; break;
                        case 3: g.state = GS_EXTRAS; break;
                        case 4: g.want_quit = true; break;
                    }
                }
                r.y += 56;
            }
            snprintf(buf, sizeof(buf), "%s build  -  %d maps installed  -  best round: %d",
                     APP_VERSION, g.map_count, g.best_round);
            draw_centered_text(buf, H - 34, 16, (Color){ 110, 110, 116, 255 });
        } break;

        case GS_MAPSELECT:
        {
            draw_menu_background();
            draw_centered_text("SELECT MAP", 40, 36, RAYWHITE);

            if (g.map_count == 0)
            {
                draw_centered_text("No maps found in assets/maps/", H / 2 - 20, 22,
                                   (Color){ 200, 80, 80, 255 });
                draw_centered_text("drop .map files there (see docs/MAP_FORMAT.md)", H / 2 + 14, 16,
                                   (Color){ 150, 150, 156, 255 });
            }
            else
            {
                Rectangle r = { W / 2.0f - 240, 100, 480, 64 };
                Vector2 m = GetMousePosition();
                for (int i = 0; i < g.map_count; i++)
                {
                    MapEntry *e = &g.map_entries[i];
                    // mouse hover selects the card
                    if (CheckCollisionPointRec(m, r)) s_focus = i;
                    bool hover = (s_focus == i);
                    DrawRectangleRec(r, hover ? (Color){ 56, 20, 20, 235 } : (Color){ 30, 30, 36, 220 });
                    DrawRectangleLinesEx(r, 2, hover ? (Color){ 255, 90, 70, 255 }
                                                     : (Color){ 80, 80, 86, 200 });
                    DrawText(e->name, (int)r.x + 14, (int)r.y + 8, 22, RAYWHITE);
                    DrawText(e->desc[0] ? e->desc : "no description", (int)r.x + 14, (int)r.y + 36, 15,
                             (Color){ 160, 160, 166, 255 });
                    // click to start
                    if (hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
                        game_start_map(e->file);
                    r.y += 74;
                }
            }

            Rectangle back = { W / 2.0f - 100, H - 90.0f, 200, 42 };
            if (ui_button("BACK", back, true, g.map_count, g.map_count + 1))
                g.state = GS_MENU;
        } break;

        case GS_SETTINGS:
        {
            draw_menu_background();
            draw_centered_text("SETTINGS", 46, 36, RAYWHITE);

            char rows[SET_ROWS][64];
            snprintf(rows[SET_ROW_SENS], 64, "Mouse sensitivity   < %.2f >", g_settings.sensitivity);
            snprintf(rows[SET_ROW_FOV], 64, "Field of view       < %d >", (int)g_settings.fov);
            snprintf(rows[SET_ROW_MASTER], 64, "Master volume       < %d%% >", (int)(g_settings.volume_master * 100));
            snprintf(rows[SET_ROW_SFX], 64, "SFX volume          < %d%% >", (int)(g_settings.volume_sfx * 100));
            snprintf(rows[SET_ROW_INVERT], 64, "Invert mouse Y      < %s >", g_settings.invert_y ? "ON" : "OFF");
            snprintf(rows[SET_ROW_FPS], 64, "Show FPS            < %s >", g_settings.show_fps ? "ON" : "OFF");
            snprintf(rows[SET_ROW_FULLSCREEN], 64, "Fullscreen          < %s >", g_settings.fullscreen ? "ON" : "OFF");
            snprintf(rows[SET_ROW_BACK], 64, "BACK  (settings save automatically)");

            Vector2 m = GetMousePosition();
            int y = 130;
            for (int i = 0; i < SET_ROWS; i++)
            {
                Rectangle row = { W / 2.0f - 300, (float)y - 6, 600, 34 };
                // mouse: hover selects, click adjusts (left half = -, right = +)
                if (CheckCollisionPointRec(m, row))
                {
                    s_focus = i;
                    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
                    {
                        if (i == SET_ROW_BACK) { settings_leave(); return; }
                        bool left = (m.x < row.x + row.width * 0.5f);
                        settings_adjust(i, left ? -1 : +1);
                    }
                }
                bool hi = (s_focus == i);
                if (hi) DrawRectangleRec(row, (Color){ 40, 16, 16, 200 });
                Color c = hi ? (Color){ 255, 120, 90, 255 } : (Color){ 190, 190, 196, 255 };
                draw_centered_text(rows[i], y, 22, c);
                y += 42;
            }
            draw_centered_text("LEFT/RIGHT or CLICK to adjust - ESC to go back", H - 60, 16,
                               (Color){ 120, 120, 126, 255 });
        } break;

        case GS_EXTRAS:
        {
            draw_menu_background();
            draw_centered_text("EXTRAS", 46, 36, RAYWHITE);

            draw_centered_text("MULTIPLAYER      --  COMING SOON --", 140, 26,
                               (Color){ 200, 170, 90, 255 });
            draw_centered_text("survive together with friends over LAN / online", 174, 16,
                               (Color){ 150, 150, 156, 255 });
            draw_centered_text("CUSTOM SKINS     --  COMING SOON --", 240, 26,
                               (Color){ 200, 170, 90, 255 });
            draw_centered_text("player and weapon skins, loaded from assets/skins", 274, 16,
                               (Color){ 150, 150, 156, 255 });
            draw_centered_text("CUSTOM MAPS      --  PLANNED --", 306, 26,
                               (Color){ 140, 200, 140, 255 });
            draw_centered_text("the map format is already data-driven: drop a .map into assets/maps and play",
                               340, 16, (Color){ 150, 150, 156, 255 });
            draw_centered_text("in-game map editor is planned for a future release", 362, 16,
                               (Color){ 150, 150, 156, 255 });

            Rectangle back = { W / 2.0f - 100, H - 100.0f, 200, 42 };
            if (ui_button("BACK", back, true, 0, 1)) g.state = GS_MENU;
        } break;

        case GS_PAUSED:
        {
            DrawRectangle(0, 0, W, H, (Color){ 0, 0, 0, 160 });
            draw_centered_text("PAUSED", H / 2 - 170, 44, RAYWHITE);

            Rectangle r = { W / 2.0f - 140, H / 2 - 70.0f, 280, 46 };
            const char *items[4] = { "RESUME", "SETTINGS", "QUIT TO MENU", "EXIT GAME" };
            for (int i = 0; i < 4; i++)
            {
                if (ui_button(items[i], r, true, i, 4))
                {
                    if (i == 0) { g.state = GS_PLAYING; DisableCursor(); }
                    else if (i == 1) { g.state = GS_SETTINGS; s_settings_from = GS_PAUSED; s_focus = 0; }
                    else if (i == 2) game_end_to_menu();
                    else g.want_quit = true;
                }
                r.y += 56;
            }
        } break;

        case GS_DEAD:
        {
            DrawRectangle(0, 0, W, H, (Color){ 20, 0, 0, 200 });
            draw_centered_text("GAME OVER", H / 2 - 120, 56, (Color){ 220, 40, 40, 255 });
            snprintf(buf, sizeof(buf), "You survived to round %d", g.round);
            draw_centered_text(buf, H / 2 - 40, 28, RAYWHITE);
            snprintf(buf, sizeof(buf), "%d kills   -   %d points earned", g_player.kills, g_player.points);
            draw_centered_text(buf, H / 2 + 4, 22, (Color){ 200, 200, 206, 255 });
            if (g.round >= g.best_round)
                draw_centered_text("NEW BEST ROUND!", H / 2 + 44, 22, (Color){ 255, 210, 90, 255 });
            draw_centered_text("[ENTER] back to menu", H - 120, 18, (Color){ 150, 150, 156, 255 });
        } break;

        case GS_PLAYING: break;   // HUD drawn separately via ui_draw_hud()
        default: break;
    }
}

// ---------------------------------------------------------------------------
// HUD
// ---------------------------------------------------------------------------
void ui_draw_hud(void)
{
    int W = GetScreenWidth(), H = GetScreenHeight();
    char buf[128];

    // ---- crosshair ----
    int cx = W / 2, cy = H / 2;
    Color chc = (Color){ 230, 230, 230, 220 };
    DrawLine(cx - 12, cy, cx - 4, cy, chc);
    DrawLine(cx + 4, cy, cx + 12, cy, chc);
    DrawLine(cx, cy - 12, cx, cy - 4, chc);
    DrawLine(cx, cy + 4, cx, cy + 12, chc);

    // ---- points (bottom left) ----
    snprintf(buf, sizeof(buf), "%d", g_player.points);
    DrawText(buf, 24, H - 58, 40, (Color){ 240, 210, 90, 255 });
    DrawText("points", 26, H - 20, 16, (Color){ 160, 150, 90, 255 });

    // ---- round (left middle-bottom, classic red) ----
    float round_pulse = 1.0f;
    if (g.round_active) round_pulse = 1.0f + 0.25f * sinf(g.time * 3.0f);
    int rs = (int)(34 * round_pulse);
    snprintf(buf, sizeof(buf), "%d", g.round);
    DrawText(buf, 30, H / 2 - 20, rs, (Color){ 180, 20, 20, 255 });
    DrawText("round", 30, H / 2 + 22, 14, (Color){ 120, 30, 30, 255 });

    // zombies left (small, next to round)
    snprintf(buf, sizeof(buf), "%d left", zombies_alive_count() + zombies_pending_count());
    DrawText(buf, 30, H / 2 + 42, 14, (Color){ 150, 60, 60, 255 });

    // ---- perk icons (bottom left, above points) ----
    {
        static const Color perk_col[PERK_TYPE_COUNT] = {
            { 90, 200, 255, 255 }, { 255, 160, 60, 255 },
            { 120, 220, 120, 255 }, { 220, 60, 60, 255 } };
        static const char perk_letter[PERK_TYPE_COUNT] = { 'Q', 'D', 'S', 'J' };
        int px = 24;
        for (int i = 0; i < PERK_TYPE_COUNT; i++)
        {
            if (!g_player.perks[i]) continue;
            DrawRectangle(px, H - 96, 24, 24, perk_col[i]);
            DrawRectangleLinesEx((Rectangle){ (float)px, (float)H - 96, 24, 24 }, 2,
                                 (Color){ 20, 20, 24, 255 });
            char lb[2] = { perk_letter[i], 0 };
            DrawText(lb, px + 8, H - 91, 18, (Color){ 20, 20, 24, 255 });
            px += 32;
        }
    }

    // ---- grenades (next to ammo, bottom right) ----
    if (g_player.grenades > 0)
    {
        int gx = W - 190, gy = H - 52;
        DrawCircle(gx, gy, 9, (Color){ 40, 70, 40, 255 });
        DrawCircle(gx, gy, 6, (Color){ 70, 110, 65, 255 });
        snprintf(buf, sizeof(buf), "x%d", g_player.grenades);
        DrawText(buf, gx + 14, gy - 8, 18, (Color){ 120, 200, 110, 255 });
    }

    // ---- ammo / weapon (bottom right) ----
    const WeaponDef *def = weapons_get(g_player.slots[g_player.cur_slot].def);
    if (def)
    {
        WeaponSlot *s = &g_player.slots[g_player.cur_slot];
        snprintf(buf, sizeof(buf), "%d / %d", s->ammo, s->reserve);
        int aw = MeasureText(buf, 36);
        DrawText(buf, W - aw - 26, H - 60, 36,
                 s->ammo == 0 ? (Color){ 230, 60, 60, 255 } : RAYWHITE);
        char nbuf[64];
        weapon_display_name(s, nbuf, sizeof(nbuf));
        int nw = MeasureText(nbuf, 18);
        DrawText(nbuf, W - nw - 26, H - 20, 18, (Color){ 170, 170, 176, 255 });

        // second slot indicator
        if (g_player.slots[1 - g_player.cur_slot].def >= 0)
        {
            const WeaponDef *od = weapons_get(g_player.slots[1 - g_player.cur_slot].def);
            if (od)
            {
                snprintf(buf, sizeof(buf), "[%d] %s%s", 1 - g_player.cur_slot + 1,
                         g_player.slots[1 - g_player.cur_slot].upgraded ? "* " : "", od->name);
                int ow = MeasureText(buf, 14);
                DrawText(buf, W - ow - 26, H - 84, 14, (Color){ 130, 130, 136, 255 });
            }
        }

        // reload bar
        if (s->reload_t > 0)
        {
            float t = 1.0f - s->reload_t / fmaxf(weapon_reload_time(s), 0.01f);
            DrawRectangle(W / 2 - 80, H - 130, 160, 8, (Color){ 30, 30, 30, 200 });
            DrawRectangle(W / 2 - 80, H - 130, (int)(160 * t), 8, (Color){ 220, 180, 90, 255 });
            draw_centered_text("RELOADING", H - 150, 14, (Color){ 220, 180, 90, 255 });
        }
        else if (s->ammo == 0 && s->reserve > 0)
        {
            draw_centered_text("[R] RELOAD", H - 150, 16, (Color){ 230, 80, 80, 255 });
        }
    }

    // ---- interact prompt ----
    const char *prompt = player_interact_prompt();
    if (prompt[0])
    {
        int pw = MeasureText(prompt, 20);
        DrawRectangle(W / 2 - pw / 2 - 12, H / 2 + 60, pw + 24, 30, (Color){ 0, 0, 0, 160 });
        DrawText(prompt, W / 2 - pw / 2, H / 2 + 66, 20, (Color){ 240, 230, 190, 255 });
    }

    // ---- health vignette ----
    float hp_frac = (float)g_player.hp / (float)player_max_hp();
    if (hp_frac < 0.45f)
    {
        float sev = 1.0f - hp_frac / 0.45f;
        // four edge gradients approximated with stacked rects
        for (int i = 0; i < 8; i++)
        {
            int a = (int)(40 * sev * (8 - i) / 8.0f);
            DrawRectangle(0, i * 14, W, 14, (Color){ 160, 0, 0, a });
            DrawRectangle(0, H - (i + 1) * 14, W, 14, (Color){ 160, 0, 0, a });
            DrawRectangle(i * 22, 0, 22, H, (Color){ 160, 0, 0, a / 2 });
            DrawRectangle(W - (i + 1) * 22, 0, 22, H, (Color){ 160, 0, 0, a / 2 });
        }
    }
    if (g_player.hurt_t > 0)
    {
        int a = (int)(110 * g_player.hurt_t);
        DrawRectangle(0, 0, W, H, (Color){ 190, 10, 10, a });
    }

    // ---- round break notice ----
    if (!g.round_active && g.round_break_t > 0 && g.state == GS_PLAYING)
    {
        snprintf(buf, sizeof(buf), "Round %d in %.0f s", g.round + 1, g.round_break_t);
        draw_centered_text(buf, H / 2 - 80, 24, (Color){ 200, 60, 60, 255 });
    }

    // ---- point popups ----
    for (int i = 0; i < MAX_POPUPS; i++)
    {
        if (s_popups[i].t <= 0) continue;
        s_popups[i].t -= g.frame_time;
        s_popups[i].screen.y -= 40 * g.frame_time;
        int a = (int)(255 * clampf(s_popups[i].t, 0, 1));
        DrawText(s_popups[i].text, (int)s_popups[i].screen.x, (int)s_popups[i].screen.y,
                 20, (Color){ 240, 210, 90, a });
    }

    if (g_settings.show_fps) DrawFPS(8, 8);
    if (g.autotest)
    {
        DrawText("AUTOTEST", 8, 30, 16, (Color){ 120, 220, 120, 255 });
    }
}
