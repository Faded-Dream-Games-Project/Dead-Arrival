// config.c - tiny INI-style reader/writer used by weapons.cfg and settings.ini
#include "game.h"

// ---------------------------------------------------------------------------
// Generic line reader for "key = value" files with "[section]" headers.
// Returns:  1 = pair read (key/value filled)
//           0 = end of file
//          -1 = section header consumed (section updated, call again)
// ---------------------------------------------------------------------------
int cfg_read(FILE *f, char *section, size_t sec_size,
             char *key, size_t key_size, char *value, size_t val_size)
{
    char line[512];
    while (fgets(line, sizeof(line), f))
    {
        // strip comments and whitespace
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';
        char *semi = strchr(line, ';');
        if (semi) *semi = '\0';

        char *s = line;
        while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
        if (*s == '\0') continue;

        if (*s == '[')
        {
            char *end = strchr(s, ']');
            if (!end) continue;
            *end = '\0';
            snprintf(section, sec_size, "%s", s + 1);
            return -1;
        }

        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = '\0';

        char *k = s, *kend = eq - 1;
        while (kend > k && (*kend == ' ' || *kend == '\t')) *kend-- = '\0';
        char *v = eq + 1;
        while (*v == ' ' || *v == '\t') v++;
        char *vend = v + strlen(v) - 1;
        while (vend >= v && (*vend == ' ' || *vend == '\t' || *vend == '\r' || *vend == '\n'))
            *vend-- = '\0';

        snprintf(key, key_size, "%s", k);
        snprintf(value, val_size, "%s", v);
        return 1;
    }
    return 0;
}

// parse "a b c ..." into floats, returns count parsed
int cfg_floats(const char *v, float *out, int max)
{
    int n = 0;
    while (*v && n < max)
    {
        char *end = NULL;
        float f = strtof(v, &end);
        if (end == v) break;
        out[n++] = f;
        v = end;
        while (*v == ' ' || *v == '\t') v++;
    }
    return n;
}

bool cfg_bool(const char *v) { return (v[0] == '1' || v[0] == 'y' || v[0] == 'Y' ||
                                       v[0] == 't' || v[0] == 'T'); }

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------
Settings g_settings;

void settings_load(Settings *s)
{
    // defaults
    s->sensitivity  = 1.6f;
    s->fov          = 75.0f;
    s->invert_y     = false;
    s->volume_master= 0.9f;
    s->volume_sfx   = 1.0f;
    s->fullscreen   = false;
    s->show_fps     = false;

    char path[512];
    snprintf(path, sizeof(path), "%s/settings.ini", ASSETS_DIR);
    FILE *f = fopen(path, "rb");
    if (!f) return;

    char section[64] = "", key[64], value[192];
    int r;
    while ((r = cfg_read(f, section, sizeof(section), key, sizeof(key),
                         value, sizeof(value))) != 0)
    {
        if (r != 1) continue;
        if      (strcmp(key, "sensitivity") == 0)   s->sensitivity  = clampf(strtof(value, NULL), 0.2f, 6.0f);
        else if (strcmp(key, "fov") == 0)           s->fov          = clampf(strtof(value, NULL), 60.0f, 110.0f);
        else if (strcmp(key, "invert_y") == 0)      s->invert_y     = cfg_bool(value);
        else if (strcmp(key, "volume_master") == 0) s->volume_master= clampf(strtof(value, NULL), 0.0f, 1.0f);
        else if (strcmp(key, "volume_sfx") == 0)    s->volume_sfx   = clampf(strtof(value, NULL), 0.0f, 1.0f);
        else if (strcmp(key, "fullscreen") == 0)    s->fullscreen   = cfg_bool(value);
        else if (strcmp(key, "show_fps") == 0)      s->show_fps     = cfg_bool(value);
    }
    fclose(f);
}

void settings_save(const Settings *s)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/settings.ini", ASSETS_DIR);
    FILE *f = fopen(path, "wb");
    if (!f)
    {
        TraceLog(LOG_WARNING, "SETTINGS: cannot write %s", path);
        return;
    }
    fprintf(f, "# Dead Arrival settings (generated)\n");
    fprintf(f, "sensitivity = %.3f\n",  s->sensitivity);
    fprintf(f, "fov = %.1f\n",          s->fov);
    fprintf(f, "invert_y = %d\n",       s->invert_y ? 1 : 0);
    fprintf(f, "volume_master = %.2f\n", s->volume_master);
    fprintf(f, "volume_sfx = %.2f\n",    s->volume_sfx);
    fprintf(f, "fullscreen = %d\n",      s->fullscreen ? 1 : 0);
    fprintf(f, "show_fps = %d\n",        s->show_fps ? 1 : 0);
    fclose(f);
}
