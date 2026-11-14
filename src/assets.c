// assets.c - runtime asset bank: textures & sounds are loaded BY NAME from
// ./assets at startup; nothing is baked into the binary. Missing textures
// fall back to a generated checker so modded maps never hard-crash the game.
#include "game.h"

#include <stdarg.h>

static AssetBank g_assets;

const char *game_asset_path(char *out, int outlen, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(out, outlen, fmt, args);
    va_end(args);
    return out;
}

static void strip_ext(char *name)
{
    char *dot = strrchr(name, '.');
    if (dot) *dot = '\0';
}

static void load_texture_dir(void)
{
    g_assets.tex_count = 0;

    // fallback: magenta/black checker for missing textures
    Image img = GenImageChecked(8, 8, 2, 2, MAGENTA, BLACK);
    g_assets.tex_fallback = LoadTextureFromImage(img);
    UnloadImage(img);
    SetTextureWrap(g_assets.tex_fallback, TEXTURE_WRAP_REPEAT);

    char dir[512];
    snprintf(dir, sizeof(dir), "%s/textures", ASSETS_DIR);
    FilePathList files = LoadDirectoryFiles(dir);
    for (unsigned i = 0; i < files.count && g_assets.tex_count < MAX_SOUNDS; i++)
    {
        if (!IsFileExtension(files.paths[i], ".png") &&
            !IsFileExtension(files.paths[i], ".jpg")) continue;

        Texture2D t = LoadTexture(files.paths[i]);
        SetTextureWrap(t, TEXTURE_WRAP_REPEAT);
        SetTextureFilter(t, TEXTURE_FILTER_POINT);

        const char *base = GetFileName(files.paths[i]);
        str_copy(g_assets.tex_names[g_assets.tex_count], MAX_TEXNAME, base);
        strip_ext(g_assets.tex_names[g_assets.tex_count]);
        g_assets.textures[g_assets.tex_count++] = t;
        TraceLog(LOG_INFO, "ASSETS: texture '%s' loaded (%dx%d)",
                 g_assets.tex_names[g_assets.tex_count - 1], t.width, t.height);
    }
    UnloadDirectoryFiles(files);
    TraceLog(LOG_INFO, "ASSETS: %d textures from %s", g_assets.tex_count, dir);
}

static void load_sound_dir(void)
{
    g_assets.snd_count = 0;
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/sounds", ASSETS_DIR);
    FilePathList files = LoadDirectoryFiles(dir);
    for (unsigned i = 0; i < files.count && g_assets.snd_count < MAX_SOUNDS; i++)
    {
        if (!IsFileExtension(files.paths[i], ".wav") &&
            !IsFileExtension(files.paths[i], ".ogg") &&
            !IsFileExtension(files.paths[i], ".mp3")) continue;

        Sound s = LoadSound(files.paths[i]);
        const char *base = GetFileName(files.paths[i]);
        str_copy(g_assets.snd_names[g_assets.snd_count], MAX_TEXNAME, base);
        strip_ext(g_assets.snd_names[g_assets.snd_count]);
        g_assets.sounds[g_assets.snd_count++] = s;
    }
    UnloadDirectoryFiles(files);
    TraceLog(LOG_INFO, "ASSETS: %d sounds from %s", g_assets.snd_count, dir);

    // optional menu music
    char music_path[512];
    snprintf(music_path, sizeof(music_path), "%s/sounds/menu_theme.ogg", ASSETS_DIR);
    if (!FileExists(music_path))
        snprintf(music_path, sizeof(music_path), "%s/sounds/menu_theme.wav", ASSETS_DIR);
    if (FileExists(music_path))
    {
        g_assets.menu_music = LoadMusicStream(music_path);
        g_assets.has_menu_music = true;
        SetMusicVolume(g_assets.menu_music, 0.45f);
    }
}

void assets_load_all(void)
{
    load_texture_dir();
    load_sound_dir();
}

void assets_unload_all(void)
{
    for (int i = 0; i < g_assets.tex_count; i++) UnloadTexture(g_assets.textures[i]);
    for (int i = 0; i < g_assets.snd_count; i++) UnloadSound(g_assets.sounds[i]);
    if (g_assets.has_menu_music)
    {
        StopMusicStream(g_assets.menu_music);
        UnloadMusicStream(g_assets.menu_music);
    }
    UnloadTexture(g_assets.tex_fallback);
}

Texture2D *assets_tex(const char *name)
{
    if (!name || !name[0]) return &g_assets.tex_fallback;
    for (int i = 0; i < g_assets.tex_count; i++)
        if (strcmp(g_assets.tex_names[i], name) == 0) return &g_assets.textures[i];
    TraceLog(LOG_WARNING, "ASSETS: texture '%s' missing, using fallback", name);
    return &g_assets.tex_fallback;
}

static Sound *find_sound(const char *name)
{
    for (int i = 0; i < g_assets.snd_count; i++)
        if (strcmp(g_assets.snd_names[i], name) == 0) return &g_assets.sounds[i];
    return NULL;
}

void sfx_play(const char *name, float volume, float pitch)
{
    Sound *s = find_sound(name);
    if (!s)
    {
        TraceLog(LOG_WARNING, "ASSETS: sound '%s' missing", name);
        return;
    }
    SetSoundVolume(*s, clampf(volume * g_settings.volume_sfx, 0.0f, 1.0f));
    SetSoundPitch(*s, pitch);
    PlaySound(*s);
}

void sfx_play_at(const char *name, Vector3 at, float base_vol)
{
    float d = Vector3Distance(at, g_camera.position);
    float vol = base_vol * clampf(1.0f - d / 45.0f, 0.0f, 1.0f);
    if (vol <= 0.01f) return;
    sfx_play(name, vol, frand_range(0.92f, 1.08f));
}

void assets_menu_music(bool play)
{
    if (!g_assets.has_menu_music) return;
    if (play && !IsMusicStreamPlaying(g_assets.menu_music)) PlayMusicStream(g_assets.menu_music);
    else if (!play && IsMusicStreamPlaying(g_assets.menu_music)) StopMusicStream(g_assets.menu_music);
}

void assets_menu_music_update(void)
{
    if (g_assets.has_menu_music && IsMusicStreamPlaying(g_assets.menu_music))
        UpdateMusicStream(g_assets.menu_music);
}
