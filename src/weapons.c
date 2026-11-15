// weapons.c - data-driven weapon definitions loaded from assets/weapons.cfg
#include "game.h"

static WeaponDef g_weapons[MAX_WEAPONS];
static int g_weapon_count = 0;

const WeaponDef *weapons_get(int idx)
{
    if (idx < 0 || idx >= g_weapon_count) return NULL;
    return &g_weapons[idx];
}

int weapons_find(const char *id)
{
    for (int i = 0; i < g_weapon_count; i++)
        if (strcmp(g_weapons[i].id, id) == 0) return i;
    return -1;
}

static void weapon_defaults(WeaponDef *w)
{
    memset(w, 0, sizeof(*w));
    w->cls = WCLASS_PISTOL;
    w->damage = 30; w->head_mult = HIT_HEAD_MULT_FALLBACK;
    w->mag = 8; w->reserve = 64; w->rpm = 300;
    w->reload_time = 1.8f; w->spread = 0.012f;
    w->ads_spread_mult = 0.4f;
    w->recoil = 1.5f; w->ads_zoom = 55.0f;
    w->pellets = 1; w->penetrate = 1; w->is_auto = 0;
    w->range = 120.0f;
    w->wallbuyable = 0; w->price = 500; w->box_weight = 10;
    w->start_weapon = 0;
    w->tint = (Color){ 70, 70, 74, 255 };
    w->vm_len = 0.55f; w->vm_barrel = 0.05f;
    snprintf(w->sound, sizeof(w->sound), "shot_pistol");
}

int weapons_load(const char *path)
{
    g_weapon_count = 0;
    FILE *f = fopen(path, "rb");
    if (!f)
    {
        TraceLog(LOG_ERROR, "WEAPONS: cannot open %s", path);
        return 0;
    }

    static WeaponDef cur;
    bool have = false;
    char section[64] = "", key[64], value[192];
    int r;

    while ((r = cfg_read(f, section, sizeof(section), key, sizeof(key),
                         value, sizeof(value))) != 0)
    {
        if (r < 0)
        {
            // new section: flush the previous weapon
            if (have && g_weapon_count < MAX_WEAPONS)
                g_weapons[g_weapon_count++] = cur;
            have = false;
            if (strncmp(section, "weapon.", 7) == 0)
            {
                weapon_defaults(&cur);
                str_copy(cur.id, sizeof(cur.id), section + 7);
                have = true;
            }
            continue;
        }
        if (!have) continue;

        float fv[4];
        if      (strcmp(key, "name") == 0)         str_copy(cur.name, sizeof(cur.name), value);
        else if (strcmp(key, "class") == 0)
        {
            if      (strcmp(value, "pistol") == 0)   cur.cls = WCLASS_PISTOL;
            else if (strcmp(value, "smg") == 0)      cur.cls = WCLASS_SMG;
            else if (strcmp(value, "rifle") == 0)    cur.cls = WCLASS_RIFLE;
            else if (strcmp(value, "ar") == 0)       cur.cls = WCLASS_AR;
            else if (strcmp(value, "shotgun") == 0)  cur.cls = WCLASS_SHOTGUN;
            else if (strcmp(value, "sniper") == 0)   cur.cls = WCLASS_SNIPER;
            else if (strcmp(value, "lmg") == 0)      cur.cls = WCLASS_LMG;
            else if (strcmp(value, "special") == 0)  cur.cls = WCLASS_SPECIAL;
        }
        else if (strcmp(key, "sound") == 0)        str_copy(cur.sound, sizeof(cur.sound), value);
        else if (strcmp(key, "damage") == 0)       cur.damage   = strtof(value, NULL);
        else if (strcmp(key, "head_mult") == 0)    cur.head_mult= strtof(value, NULL);
        else if (strcmp(key, "mag") == 0)          cur.mag      = atoi(value);
        else if (strcmp(key, "reserve") == 0)      cur.reserve  = atoi(value);
        else if (strcmp(key, "rpm") == 0)          cur.rpm      = atoi(value);
        else if (strcmp(key, "reload") == 0)       cur.reload_time = strtof(value, NULL);
        else if (strcmp(key, "spread") == 0)       cur.spread   = strtof(value, NULL);
        else if (strcmp(key, "ads_spread_mult") == 0) cur.ads_spread_mult = strtof(value, NULL);
        else if (strcmp(key, "recoil") == 0)       cur.recoil   = strtof(value, NULL);
        else if (strcmp(key, "ads_zoom") == 0)     cur.ads_zoom = strtof(value, NULL);
        else if (strcmp(key, "pellets") == 0)      cur.pellets  = atoi(value);
        else if (strcmp(key, "penetrate") == 0)    cur.penetrate= atoi(value);
        else if (strcmp(key, "auto") == 0)         cur.is_auto  = cfg_bool(value);
        else if (strcmp(key, "range") == 0)        cur.range    = strtof(value, NULL);
        else if (strcmp(key, "wallbuy") == 0)      cur.wallbuyable = cfg_bool(value);
        else if (strcmp(key, "price") == 0)        cur.price    = atoi(value);
        else if (strcmp(key, "box_weight") == 0)   cur.box_weight = atoi(value);
        else if (strcmp(key, "start") == 0)        cur.start_weapon = cfg_bool(value);
        else if (strcmp(key, "tint") == 0)
        {
            if (cfg_floats(value, fv, 4) >= 3)
                cur.tint = (Color){ (unsigned char)fv[0], (unsigned char)fv[1],
                                    (unsigned char)fv[2], 255 };
        }
        else if (strcmp(key, "vm_len") == 0)       cur.vm_len = strtof(value, NULL);
        else if (strcmp(key, "vm_barrel") == 0)    cur.vm_barrel = strtof(value, NULL);
    }
    if (have && g_weapon_count < MAX_WEAPONS)
        g_weapons[g_weapon_count++] = cur;
    fclose(f);

    // sanity pass
    for (int i = 0; i < g_weapon_count; i++)
    {
        WeaponDef *w = &g_weapons[i];
        if (w->mag < 1) w->mag = 1;
        if (w->damage <= 0) w->damage = 20;
        if (w->rpm < 1) w->rpm = 60;
        if (w->pellets < 1) w->pellets = 1;
        if (w->penetrate < 1) w->penetrate = 1;
        // precision caps the copy at the destination size (silences
        // -Wformat-truncation: id is 64 bytes, name only 48)
        if (!w->name[0]) snprintf(w->name, sizeof(w->name), "%.47s", w->id);
    }

    TraceLog(LOG_INFO, "WEAPONS: %d weapons loaded from %s", g_weapon_count, path);
    return g_weapon_count;
}

void weapons_log_summary(void)
{
    int wall = 0, box = 0;
    for (int i = 0; i < g_weapon_count; i++)
    {
        if (g_weapons[i].wallbuyable) wall++;
        if (g_weapons[i].box_weight > 0) box++;
    }
    TraceLog(LOG_INFO, "WEAPONS: %d total, %d wall-buyable, %d in mystery box",
             g_weapon_count, wall, box);
}

int weapons_random_box_pick(void)
{
    int total = 0;
    for (int i = 0; i < g_weapon_count; i++)
        if (g_weapons[i].box_weight > 0) total += g_weapons[i].box_weight;
    if (total == 0) return -1;

    int roll = GetRandomValue(1, total);
    for (int i = 0; i < g_weapon_count; i++)
    {
        if (g_weapons[i].box_weight <= 0) continue;
        roll -= g_weapons[i].box_weight;
        if (roll <= 0) return i;
    }
    return g_weapon_count - 1;
}
