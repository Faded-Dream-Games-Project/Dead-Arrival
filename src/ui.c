// ui.c - menus (title, map select, settings, extras), the in-game HUD and
// point popups. Menus are mouse+keyboard; HUD shows points, round, ammo,
// health vignette and interaction prompts.
#include "game.h"

static int  s_focus = 0;             // keyboard focus index for the current menu
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
// Update
// ---------------------------------------------------------------------------
void ui_update(void)
{
    // menu music lifecycle
    bool in_menu = (g.state != GS_PLAYING && g.state != GS_DEAD);
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
                    case 2: g.state = GS_SETTINGS; s_focus = 0; break;
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
            menu_nav(8);
            float step = (s_focus == 0) ? 0.1f : 1.0f;
            if (IsKeyPressed(KEY_LEFT))
            {
                switch (s_focus)
                {
                    case 0: g_settings.sensitivity = clampf(g_settings.sensitivity - 0.1f, 0.3f, 4.0f); break;
                    case 1: g_settings.fov -= 5; break;
                    case 2: g_settings.volume_master = clampf(g_settings.volume_master - 0.05f, 0, 1); break;
                    case 3: g_settings.volume_sfx = clampf(g_settings.volume_sfx - 0.05f, 0, 1); break;
                    case 4: g_settings.invert_y = !g_settings.invert_y; break;
                    case 5: g_settings.show_fps = !g_settings.show_fps; break;
                    case 6: g_settings.fullscreen = !g_settings.fullscreen; break;
                }
                (void)step;
            }
            if (IsKeyPressed(KEY_RIGHT))
            {
                switch (s_focus)
                {
                    case 0: g_settings.sensitivity = clampf(g_settings.sensitivity + 0.1f, 0.3f, 4.0f); break;
                    case 1: g_settings.fov += 5; break;
                    case 2: g_settings.volume_master = clampf(g_settings.volume_master + 0.05f, 0, 1); break;
                    case 3: g_settings.volume_sfx = clampf(g_settings.volume_sfx + 0.05f, 0, 1); break;
                    case 4: g_settings.invert_y = !g_settings.invert_y; break;
                    case 5: g_settings.show_fps = !g_settings.show_fps; break;
                    case 6: g_settings.fullscreen = !g_settings.fullscreen; break;
                }
            }
            // apply audio immediately
            SetMasterVolume(g_settings.volume_master);
            if (IsKeyPressed(KEY_ESCAPE) || (ui_key_enter() && s_focus == 7))
            {
                g_settings.fov = clampf((float)g_settings.fov, 60, 110);
                settings_save(&g_settings);
                g.state = GS_MENU;
            }
        } break;

        case GS_EXTRAS:
        {
            if (IsKeyPressed(KEY_ESCAPE) || ui_key_enter()) g.state = GS_MENU;
        } break;

        case GS_PLAYING:
        {
            if (IsKeyPressed(KEY_ESCAPE))
            {
                g.state = GS_PAUSED;
                EnableCursor();
            }
        } break;

        case GS_PAUSED:
        {
            menu_nav(3);
            if (IsKeyPressed(KEY_ESCAPE)) { g.state = GS_PLAYING; DisableCursor(); }
            if (ui_key_enter())
            {
                if (s_focus == 0) { g.state = GS_PLAYING; DisableCursor(); }
                else if (s_focus == 1) { g.state = GS_SETTINGS; s_focus = 0; }
                else game_end_to_menu();
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
            snprintf(buf, sizeof(buf), "v%s  -  %d maps installed  -  best round: %d",
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
                for (int i = 0; i < g.map_count; i++)
                {
                    MapEntry *e = &g.map_entries[i];
                    bool hover = (s_focus == i);
                    DrawRectangleRec(r, hover ? (Color){ 56, 20, 20, 235 } : (Color){ 30, 30, 36, 220 });
                    DrawRectangleLinesEx(r, 2, hover ? (Color){ 255, 90, 70, 255 }
                                                     : (Color){ 80, 80, 86, 200 });
                    DrawText(e->name, (int)r.x + 14, (int)r.y + 8, 22, RAYWHITE);
                    DrawText(e->desc[0] ? e->desc : "no description", (int)r.x + 14, (int)r.y + 36, 15,
                             (Color){ 160, 160, 166, 255 });
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

            const char *rows[8];
            rows[0] = TextFormat("Mouse sensitivity   < %.2f >", g_settings.sensitivity);
            rows[1] = TextFormat("Field of view       < %d >", (int)g_settings.fov);
            rows[2] = TextFormat("Master volume       < %d%% >", (int)(g_settings.volume_master * 100));
            rows[3] = TextFormat("SFX volume          < %d%% >", (int)(g_settings.volume_sfx * 100));
            rows[4] = TextFormat("Invert mouse Y      < %s >", g_settings.invert_y ? "ON" : "OFF");
            rows[5] = TextFormat("Show FPS            < %s >", g_settings.show_fps ? "ON" : "OFF");
            rows[6] = TextFormat("Fullscreen          < %s >", g_settings.fullscreen ? "ON" : "OFF");
            rows[7] = "BACK  (settings save automatically)";

            int y = 130;
            for (int i = 0; i < 8; i++)
            {
                Color c = (s_focus == i) ? (Color){ 255, 120, 90, 255 } : (Color){ 190, 190, 196, 255 };
                draw_centered_text(rows[i], y, 22, c);
                y += 42;
            }
            draw_centered_text("LEFT/RIGHT adjust - ESC back", H - 60, 16, (Color){ 120, 120, 126, 255 });
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
            draw_centered_text("PAUSED", H / 2 - 150, 44, RAYWHITE);

            Rectangle r = { W / 2.0f - 140, H / 2 - 60.0f, 280, 46 };
            const char *items[3] = { "RESUME", "SETTINGS", "QUIT TO MENU" };
            for (int i = 0; i < 3; i++)
            {
                if (ui_button(items[i], r, true, i, 3))
                {
                    if (i == 0) { g.state = GS_PLAYING; DisableCursor(); }
                    else if (i == 1) { g.state = GS_SETTINGS; s_focus = 0; }
                    else game_end_to_menu();
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

    // ---- ammo / weapon (bottom right) ----
    const WeaponDef *def = weapons_get(g_player.slots[g_player.cur_slot].def);
    if (def)
    {
        WeaponSlot *s = &g_player.slots[g_player.cur_slot];
        snprintf(buf, sizeof(buf), "%d / %d", s->ammo, s->reserve);
        int aw = MeasureText(buf, 36);
        DrawText(buf, W - aw - 26, H - 60, 36,
                 s->ammo == 0 ? (Color){ 230, 60, 60, 255 } : RAYWHITE);
        int nw = MeasureText(def->name, 18);
        DrawText(def->name, W - nw - 26, H - 20, 18, (Color){ 170, 170, 176, 255 });

        // second slot indicator
        if (g_player.slots[1 - g_player.cur_slot].def >= 0)
        {
            const WeaponDef *od = weapons_get(g_player.slots[1 - g_player.cur_slot].def);
            if (od)
            {
                snprintf(buf, sizeof(buf), "[%d] %s", 1 - g_player.cur_slot + 1, od->name);
                int ow = MeasureText(buf, 14);
                DrawText(buf, W - ow - 26, H - 84, 14, (Color){ 130, 130, 136, 255 });
            }
        }

        // reload bar
        if (s->reload_t > 0)
        {
            float t = 1.0f - s->reload_t / fmaxf(def->reload_time, 0.01f);
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
    if (g_player.hp < 45)
    {
        float sev = 1.0f - (float)g_player.hp / 45.0f;
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
