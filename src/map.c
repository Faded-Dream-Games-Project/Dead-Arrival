// map.c - data-driven map loading (assets/maps/*.map), AABB collision world
// and per-box mesh building with world-scaled tiled UVs.
// See docs/MAP_FORMAT.md for the format description.
#include "game.h"
#include <ctype.h>

// ---------------------------------------------------------------------------
// Forward decls (file-local)
// ---------------------------------------------------------------------------
static void map_add_box(GameMap *m, Vector3 pos, Vector3 size, const char *tex);
static Mesh build_box_mesh(Vector3 size);
static BoundingBox box_bb(const MapBox *b);
static BoundingBox entity_bb(Vector3 pos, float radius, float height);

// ---------------------------------------------------------------------------
// Directory listing: every .map file in assets/maps is a playable map
// ---------------------------------------------------------------------------
int map_list_dir(char out[][MAX_NAME], int max)
{
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/maps", ASSETS_DIR);
    if (!DirectoryExists(dir)) return 0;

    FilePathList files = LoadDirectoryFiles(dir);
    int n = 0;
    for (unsigned i = 0; i < files.count && n < max; i++)
    {
        if (!IsFileExtension(files.paths[i], ".map")) continue;
        str_copy(out[n], MAX_NAME, GetFileName(files.paths[i]));
        n++;
    }
    UnloadDirectoryFiles(files);
    return n;
}

// read only name/desc for the map selection screen (no geometry parsing)
void map_peek(const char *file, char *name, int name_len, char *desc, int desc_len)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/maps/%s", ASSETS_DIR, file);
    name[0] = desc[0] = '\0';

    FILE *f = fopen(path, "rb");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof(line), f))
    {
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *k = line;
        while (*k == ' ' || *k == '\t') k++;
        char *kend = eq - 1;
        while (kend > k && (*kend == ' ' || *kend == '\t' || *kend == '\r' || *kend == '\n')) *kend-- = '\0';
        char *v = eq + 1;
        while (*v == ' ' || *v == '\t') v++;
        char *vend = v + strlen(v) - 1;
        while (vend >= v && (*vend == ' ' || *vend == '\t' || *vend == '\r' || *vend == '\n')) *vend-- = '\0';

        if (strcmp(k, "name") == 0) snprintf(name, name_len, "%.*s", name_len - 1, v);
        else if (strcmp(k, "desc") == 0) snprintf(desc, desc_len, "%.*s", desc_len - 1, v);
    }
    fclose(f);
    if (!name[0]) snprintf(name, name_len, "%.*s", name_len - 1, file);
}

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------
static void map_defaults(GameMap *m)
{
    memset(m, 0, sizeof(*m));
    m->spawn_mode = SPAWN_BARRIERS;
    m->ambient = (Color){ 200, 200, 200, 255 };
    m->sky = (Color){ 12, 14, 20, 255 };
    m->player_spawn = (Vector3){ 0, 0, 0 };
    m->player_yaw = 90.0f;
}

bool map_load(GameMap *m, const char *path)
{
    map_defaults(m);
    str_copy(m->file, sizeof(m->file), path);

    FILE *f = fopen(path, "rb");
    if (!f)
    {
        TraceLog(LOG_ERROR, "MAP: cannot open %s", path);
        return false;
    }

    char line[512], key[64], value[384];
    int lineno = 0;
    bool ok = true;
    float fv[8];

    while (fgets(line, sizeof(line), f))
    {
        lineno++;
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';
        char *s = line;
        while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
        if (*s == '\0') continue;

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
        str_copy(key, sizeof(key), k);
        str_copy(value, sizeof(value), v);

        if (strcmp(key, "name") == 0)            str_copy(m->name, sizeof(m->name), value);
        else if (strcmp(key, "desc") == 0)       str_copy(m->desc, sizeof(m->desc), value);
        else if (strcmp(key, "author") == 0)     str_copy(m->author, sizeof(m->author), value);
        else if (strcmp(key, "spawn_mode") == 0)
        {
            if      (strcmp(value, "barriers") == 0) m->spawn_mode = SPAWN_BARRIERS;
            else if (strcmp(value, "graves") == 0)   m->spawn_mode = SPAWN_GRAVES;
            else if (strcmp(value, "drop") == 0)     m->spawn_mode = SPAWN_DROP;
            else TraceLog(LOG_WARNING, "MAP %s:%d: unknown spawn_mode '%s'", m->file, lineno, value);
        }
        else if (strcmp(key, "ambient") == 0 && cfg_floats(value, fv, 3) == 3)
            m->ambient = (Color){ (unsigned char)fv[0], (unsigned char)fv[1], (unsigned char)fv[2], 255 };
        else if (strcmp(key, "sky") == 0 && cfg_floats(value, fv, 3) == 3)
            m->sky = (Color){ (unsigned char)fv[0], (unsigned char)fv[1], (unsigned char)fv[2], 255 };
        else if (strcmp(key, "player_spawn") == 0)
        {
            int n = cfg_floats(value, fv, 4);
            if (n >= 3) m->player_spawn = (Vector3){ fv[0], fv[1], fv[2] };
            if (n >= 4) m->player_yaw = fv[3];
        }
        else if (strcmp(key, "box") == 0)
        {
            // box = x y z w h d [tex]
            if (cfg_floats(value, fv, 6) == 6)
            {
                const char *tex = "missing";
                char *sp = strrchr(value, ' ');
                if (sp && !isdigit((unsigned char)sp[1]) && sp[1] != '-' && sp[1] != '.')
                    tex = sp + 1;
                if (m->box_count < MAX_MAP_BOXES)
                    map_add_box(m, (Vector3){ fv[0], fv[1], fv[2] },
                                (Vector3){ fv[3], fv[4], fv[5] }, tex);
            }
            else { TraceLog(LOG_WARNING, "MAP %s:%d: bad box line", m->file, lineno); ok = false; }
        }
        else if (strcmp(key, "barrier") == 0)
        {
            // barrier = x y z face width
            if (cfg_floats(value, fv, 5) == 5 && m->barrier_count < MAX_BARRIERS)
            {
                Barrier *b = &m->barriers[m->barrier_count++];
                b->pos = (Vector3){ fv[0], fv[1], fv[2] };
                b->face = (int)fv[3];
                b->size = (Vector2){ fv[4], 0.5f };
                b->boards = BARRIER_MAX_BOARDS;
                b->tear_accum = 0.0f;
                b->repair_cd = 0.0f;
                b->used = 0;
            }
        }
        else if ((strcmp(key, "grave") == 0 || strcmp(key, "drop") == 0) &&
                 cfg_floats(value, fv, 3) == 3 && m->spawn_count < MAX_SPAWN_POINTS)
        {
            SpawnPoint *p = &m->spawns[m->spawn_count++];
            p->pos = (Vector3){ fv[0], fv[1], fv[2] };
            p->used = 0;
        }
        else if (strcmp(key, "wallbuy") == 0)
        {
            // wallbuy = x y z face weapon_id [price]
            if (cfg_floats(value, fv, 4) == 4 && m->wallbuy_count < MAX_WALLBUYS)
            {
                WallBuy *w = &m->wallbuys[m->wallbuy_count++];
                w->pos = (Vector3){ fv[0], fv[1], fv[2] };
                w->face = (int)fv[3];
                // weapon id = 5th token
                char *tok = value;
                for (int t = 0; t < 4; t++)
                {
                    while (*tok == ' ') tok++;
                    while (*tok && *tok != ' ') tok++;
                }
                while (*tok == ' ') tok++;
                char wid[64] = "";
                int price = 0;
                sscanf(tok, "%63s %d", wid, &price);
                w->weapon = weapons_find(wid);
                w->price = (price > 0) ? price : 0;
                if (w->weapon < 0)
                {
                    TraceLog(LOG_WARNING, "MAP %s:%d: wallbuy weapon '%s' unknown", m->file, lineno, wid);
                    m->wallbuy_count--;
                }
            }
        }
        else if (strcmp(key, "mysterybox") == 0 &&
                 cfg_floats(value, fv, 3) == 3 && m->mbox_count < MAX_MYSTERY_BOXES)
        {
            m->mboxes[m->mbox_count++].pos = (Vector3){ fv[0], fv[1], fv[2] };
        }
        else if (strcmp(key, "mystery_box") == 0 &&
                 cfg_floats(value, fv, 3) == 3 && m->mbox_count < MAX_MYSTERY_BOXES)
        {
            m->mboxes[m->mbox_count++].pos = (Vector3){ fv[0], fv[1], fv[2] };
        }
    }
    fclose(f);

    if (!m->name[0]) str_copy(m->name, sizeof(m->name), m->file);

    TraceLog(LOG_INFO, "MAP: '%s' (%s) by %s - mode=%s boxes=%d barriers=%d spawns=%d buys=%d mboxes=%d",
             m->name, m->file, m->author[0] ? m->author : "unknown",
             m->spawn_mode == SPAWN_BARRIERS ? "barriers" :
             m->spawn_mode == SPAWN_GRAVES ? "graves" : "drop",
             m->box_count, m->barrier_count, m->spawn_count,
             m->wallbuy_count, m->mbox_count);

    if (m->spawn_mode == SPAWN_BARRIERS && m->barrier_count == 0)
    {
        TraceLog(LOG_WARNING, "MAP: barrier spawn mode but no barriers defined");
        ok = false;
    }
    if (m->spawn_mode != SPAWN_BARRIERS && m->spawn_count == 0)
    {
        TraceLog(LOG_WARNING, "MAP: grave/drop spawn mode but no spawn points");
        ok = false;
    }

    map_build_meshes(m);
    return ok;
}

static void map_add_box(GameMap *m, Vector3 pos, Vector3 size, const char *tex)
{
    MapBox *b = &m->boxes[m->box_count++];
    b->pos = pos;
    b->size = size;
    snprintf(b->tex, MAX_TEXNAME, "%s", tex);
}

void map_unload(GameMap *m)
{
    if (m->mesh_built)
    {
        for (int i = 0; i < m->box_count; i++) UnloadMesh(m->meshes[i]);
        m->mesh_built = false;
    }
}

// ---------------------------------------------------------------------------
// Mesh building: one cube mesh per box, UVs scaled so textures tile every
// 2 metres of world space regardless of box size.
// ---------------------------------------------------------------------------
static Mesh build_box_mesh(Vector3 size)
{
    Mesh mesh = { 0 };
    static Vector3 verts[24];
    static Vector3 norms[24];
    static Vector2 uvs[24];
    static unsigned short idx[36];

    float hx = size.x * 0.5f, hy = size.y * 0.5f, hz = size.z * 0.5f;
    // tile scale: one texture repeat per 2m
    float tw = 2.0f;

    struct { Vector3 n; Vector3 a, b, c, d; float u, v; } faces[6] = {
        // +Z
        { {0,0,1},  {-hx,-hy, hz}, { hx,-hy, hz}, { hx, hy, hz}, {-hx, hy, hz},
          size.x / tw, size.y / tw },
        // -Z
        { {0,0,-1}, { hx,-hy,-hz}, {-hx,-hy,-hz}, {-hx, hy,-hz}, { hx, hy,-hz},
          size.x / tw, size.y / tw },
        // +X
        { {1,0,0},  { hx,-hy, hz}, { hx,-hy,-hz}, { hx, hy,-hz}, { hx, hy, hz},
          size.z / tw, size.y / tw },
        // -X
        { {-1,0,0}, {-hx,-hy,-hz}, {-hx,-hy, hz}, {-hx, hy, hz}, {-hx, hy,-hz},
          size.z / tw, size.y / tw },
        // +Y
        { {0,1,0},  {-hx, hy, hz}, { hx, hy, hz}, { hx, hy,-hz}, {-hx, hy,-hz},
          size.x / tw, size.z / tw },
        // -Y
        { {0,-1,0}, {-hx,-hy,-hz}, { hx,-hy,-hz}, { hx,-hy, hz}, {-hx,-hy, hz},
          size.x / tw, size.z / tw },
    };

    for (int i = 0; i < 6; i++)
    {
        Vector3 *v = &verts[i * 4];
        v[0] = faces[i].a; v[1] = faces[i].b; v[2] = faces[i].c; v[3] = faces[i].d;
        for (int j = 0; j < 4; j++)
        {
            norms[i * 4 + j] = faces[i].n;
            uvs[i * 4 + j] = (Vector2){ 0, 0 };
        }
        uvs[i * 4 + 0] = (Vector2){ 0, 0 };
        uvs[i * 4 + 1] = (Vector2){ faces[i].u, 0 };
        uvs[i * 4 + 2] = (Vector2){ faces[i].u, faces[i].v };
        uvs[i * 4 + 3] = (Vector2){ 0, faces[i].v };

        unsigned short *ix = &idx[i * 6];
        ix[0] = i * 4 + 0; ix[1] = i * 4 + 1; ix[2] = i * 4 + 2;
        ix[3] = i * 4 + 0; ix[4] = i * 4 + 2; ix[5] = i * 4 + 3;
    }

    mesh.vertexCount = 24;
    mesh.triangleCount = 12;
    mesh.vertices = RL_MALLOC(24 * sizeof(Vector3));
    mesh.normals  = RL_MALLOC(24 * sizeof(Vector3));
    mesh.texcoords= RL_MALLOC(24 * sizeof(Vector2));
    mesh.indices  = RL_MALLOC(36 * sizeof(unsigned short));
    memcpy(mesh.vertices,  verts, sizeof(verts));
    memcpy(mesh.normals,   norms, sizeof(norms));
    memcpy(mesh.texcoords, uvs,   sizeof(uvs));
    memcpy(mesh.indices,   idx,   sizeof(idx));
    UploadMesh(&mesh, false);
    return mesh;
}

void map_build_meshes(GameMap *m)
{
    if (m->mesh_built) map_unload(m);
    for (int i = 0; i < m->box_count; i++)
        m->meshes[i] = build_box_mesh(m->boxes[i].size);
    m->mesh_built = true;
}

// ---------------------------------------------------------------------------
// Collision
// ---------------------------------------------------------------------------
static BoundingBox box_bb(const MapBox *b)
{
    return (BoundingBox){
        (Vector3){ b->pos.x - b->size.x * 0.5f, b->pos.y, b->pos.z - b->size.z * 0.5f },
        (Vector3){ b->pos.x + b->size.x * 0.5f, b->pos.y + b->size.y, b->pos.z + b->size.z * 0.5f }
    };
}

// MapBox.pos is the BOTTOM-center; meshes are centered, so world transform
// needs the geometric center:
Vector3 map_box_center(const MapBox *b)
{
    return (Vector3){ b->pos.x, b->pos.y + b->size.y * 0.5f, b->pos.z };
}

static BoundingBox entity_bb(Vector3 pos, float radius, float height)
{
    return (BoundingBox){
        (Vector3){ pos.x - radius, pos.y,          pos.z - radius },
        (Vector3){ pos.x + radius, pos.y + height, pos.z + radius }
    };
}

static bool bb_overlap(BoundingBox a, BoundingBox b)
{
    return (a.min.x < b.max.x && a.max.x > b.min.x &&
            a.min.y < b.max.y && a.max.y > b.min.y &&
            a.min.z < b.max.z && a.max.z > b.min.z);
}

// Try stepping the entity up onto a box top (returns true if stepped)
static bool try_step_up(GameMap *m, Vector3 *pos, float radius, float height,
                        float step_height, BoundingBox hit_box)
{
    float rise = hit_box.max.y - pos->y;
    if (rise <= 0.0f || rise > step_height) return false;

    Vector3 up = { pos->x, hit_box.max.y + 0.001f, pos->z };
    BoundingBox test = entity_bb(up, radius, height);
    for (int i = 0; i < m->box_count; i++)
    {
        BoundingBox bb = box_bb(&m->boxes[i]);
        if (bb_overlap(test, bb)) return false;   // no headroom
    }
    *pos = up;
    return true;
}

bool map_move_aabb(GameMap *m, Vector3 *pos, Vector3 *vel, float dt,
                   float radius, float height, float step_height, bool *on_ground)
{
    bool collided = false;
    *on_ground = false;

    // --- X axis ---
    pos->x += vel->x * dt;
    for (int i = 0; i < m->box_count; i++)
    {
        BoundingBox bb = box_bb(&m->boxes[i]);
        BoundingBox e = entity_bb(*pos, radius, height);
        if (!bb_overlap(e, bb)) continue;
        if (try_step_up(m, pos, radius, height, step_height, bb)) continue;
        if (vel->x > 0) pos->x = bb.min.x - radius - 0.001f;
        else            pos->x = bb.max.x + radius + 0.001f;
        vel->x = 0;
        collided = true;
    }

    // --- Z axis ---
    pos->z += vel->z * dt;
    for (int i = 0; i < m->box_count; i++)
    {
        BoundingBox bb = box_bb(&m->boxes[i]);
        BoundingBox e = entity_bb(*pos, radius, height);
        if (!bb_overlap(e, bb)) continue;
        if (try_step_up(m, pos, radius, height, step_height, bb)) continue;
        if (vel->z > 0) pos->z = bb.min.z - radius - 0.001f;
        else            pos->z = bb.max.z + radius + 0.001f;
        vel->z = 0;
        collided = true;
    }

    // --- Y axis ---
    pos->y += vel->y * dt;
    for (int i = 0; i < m->box_count; i++)
    {
        BoundingBox bb = box_bb(&m->boxes[i]);
        BoundingBox e = entity_bb(*pos, radius, height);
        if (!bb_overlap(e, bb)) continue;
        if (vel->y <= 0)
        {
            pos->y = bb.max.y;
            *on_ground = true;
            vel->y = 0;
        }
        else
        {
            pos->y = bb.min.y - height - 0.001f;
            vel->y = 0;
        }
        collided = true;
    }

    // ground probe (standing still on a surface)
    if (!*on_ground)
    {
        Vector3 probe = { pos->x, pos->y - 0.08f, pos->z };
        BoundingBox e = entity_bb(probe, radius, height);
        for (int i = 0; i < m->box_count; i++)
        {
            if (bb_overlap(e, box_bb(&m->boxes[i])))
            {
                if (vel->y <= 0.01f) { *on_ground = true; vel->y = 0; }
                break;
            }
        }
    }
    return collided;
}

RayCollision map_raycast(GameMap *m, Ray ray, float max_dist)
{
    RayCollision best = { 0 };
    best.hit = false;
    best.distance = max_dist;

    for (int i = 0; i < m->box_count; i++)
    {
        BoundingBox bb = box_bb(&m->boxes[i]);
        // cheap reject
        if (ray.position.x < bb.min.x - max_dist && ray.direction.x <= 0) continue;
        if (ray.position.x > bb.max.x + max_dist && ray.direction.x >= 0) continue;
        if (ray.position.z < bb.min.z - max_dist && ray.direction.z <= 0) continue;
        if (ray.position.z > bb.max.z + max_dist && ray.direction.z >= 0) continue;

        RayCollision hit = GetRayCollisionBox(ray, bb);
        if (hit.hit && hit.distance < best.distance) best = hit;
    }
    return best;
}

// ---------------------------------------------------------------------------
// Barrier helpers (player-blocking volumes in wall openings)
// ---------------------------------------------------------------------------
BoundingBox barrier_bb(const Barrier *b)
{
    // opening: width along the wall axis, ~2.1m tall
    if (b->face == 0 || b->face == 1)       // wall along X, zombies from +-Z
        return (BoundingBox){
            (Vector3){ b->pos.x - b->size.x * 0.5f, b->pos.y, b->pos.z - 0.25f },
            (Vector3){ b->pos.x + b->size.x * 0.5f, b->pos.y + 2.1f, b->pos.z + 0.25f } };
    else                                    // wall along Z, zombies from +-X
        return (BoundingBox){
            (Vector3){ b->pos.x - 0.25f, b->pos.y, b->pos.z - b->size.x * 0.5f },
            (Vector3){ b->pos.x + 0.25f, b->pos.y + 2.1f, b->pos.z + b->size.x * 0.5f } };
}

Vector3 barrier_outward(const Barrier *b)
{
    switch (b->face)
    {
        case 0: return (Vector3){ 0, 0,  1 };
        case 1: return (Vector3){ 0, 0, -1 };
        case 2: return (Vector3){  1, 0, 0 };
        default:return (Vector3){ -1, 0, 0 };
    }
}
