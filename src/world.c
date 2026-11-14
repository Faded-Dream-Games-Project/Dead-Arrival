// world.c - static world rendering: textured map boxes, boarded barriers,
// wall-buy chalk outlines, mystery boxes with their roll animation.
#include "game.h"

MysteryBoxState g_mbox[MAX_MYSTERY_BOXES];

static Material s_mat = { 0 };
static bool s_mat_ready = false;
static Mesh s_unit_quad = { 0 };
static bool s_quad_ready = false;
static Mesh s_unit_cube = { 0 };
static bool s_cube_ready = false;

void world_init(void)
{
    memset(g_mbox, 0, sizeof(g_mbox));
    s_mat_ready = false;
    s_quad_ready = false;
    s_cube_ready = false;
}

void world_unload(void)
{
    if (s_quad_ready) { UnloadMesh(s_unit_quad); s_quad_ready = false; }
    if (s_cube_ready) { UnloadMesh(s_unit_cube); s_cube_ready = false; }
}

static void ensure_material(void)
{
    if (!s_mat_ready)
    {
        s_mat = LoadMaterialDefault();
        s_mat_ready = true;
    }
}

// textured axis-aligned box via a shared unit cube mesh
// (raylib 6.x removed DrawCubeTexture, this is the replacement)
static void ensure_cube(void)
{
    if (s_cube_ready) return;
    static Vector3 verts[24], norms[24];
    static Vector2 uvs[24];
    static unsigned short idx[36];
    struct { Vector3 n; Vector3 a, b, c, d; } faces[6] = {
        { {0,0, 1}, {-0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f, 0.5f}, { 0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f} },
        { {0,0,-1}, { 0.5f,-0.5f,-0.5f}, {-0.5f,-0.5f,-0.5f}, {-0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f,-0.5f} },
        { { 1,0,0}, { 0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f,-0.5f}, { 0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f, 0.5f} },
        { {-1,0,0}, {-0.5f,-0.5f,-0.5f}, {-0.5f,-0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f,-0.5f} },
        { {0, 1,0}, {-0.5f, 0.5f, 0.5f}, { 0.5f, 0.5f, 0.5f}, { 0.5f, 0.5f,-0.5f}, {-0.5f, 0.5f,-0.5f} },
        { {0,-1,0}, {-0.5f,-0.5f,-0.5f}, { 0.5f,-0.5f,-0.5f}, { 0.5f,-0.5f, 0.5f}, {-0.5f,-0.5f, 0.5f} },
    };
    for (int i = 0; i < 6; i++)
    {
        Vector3 *v = &verts[i * 4];
        v[0] = faces[i].a; v[1] = faces[i].b; v[2] = faces[i].c; v[3] = faces[i].d;
        for (int j = 0; j < 4; j++)
        {
            norms[i * 4 + j] = faces[i].n;
            uvs[i * 4 + j] = (Vector2){ (j == 1 || j == 2) ? 1.0f : 0.0f,
                                        (j >= 2) ? 1.0f : 0.0f };
        }
        unsigned short *ix = &idx[i * 6];
        ix[0] = i * 4 + 0; ix[1] = i * 4 + 1; ix[2] = i * 4 + 2;
        ix[3] = i * 4 + 0; ix[4] = i * 4 + 2; ix[5] = i * 4 + 3;
    }
    Mesh m = { 0 };
    m.vertexCount = 24;
    m.triangleCount = 12;
    m.vertices = RL_MALLOC(24 * sizeof(Vector3));
    m.normals = RL_MALLOC(24 * sizeof(Vector3));
    m.texcoords = RL_MALLOC(24 * sizeof(Vector2));
    m.indices = RL_MALLOC(36 * sizeof(unsigned short));
    memcpy(m.vertices, verts, sizeof(verts));
    memcpy(m.normals, norms, sizeof(norms));
    memcpy(m.texcoords, uvs, sizeof(uvs));
    memcpy(m.indices, idx, sizeof(idx));
    UploadMesh(&m, false);
    s_unit_cube = m;
    s_cube_ready = true;
}

static void draw_textured_box(Vector3 center, Vector3 size, Texture2D *tex, Color tint)
{
    ensure_material();
    ensure_cube();
    s_mat.maps[MATERIAL_MAP_DIFFUSE].texture = *tex;
    s_mat.maps[MATERIAL_MAP_DIFFUSE].color = tint;
    Matrix mat = MatrixMultiply(MatrixScale(size.x, size.y, size.z),
                                MatrixTranslate(center.x, center.y, center.z));
    DrawMesh(s_unit_cube, s_mat, mat);
}

// billboard quad helper (faces the camera)
static void ensure_quad(void)
{
    if (s_quad_ready) return;
    static Vector3 verts[4] = {
        { -0.5f, -0.5f, 0 }, { 0.5f, -0.5f, 0 }, { 0.5f, 0.5f, 0 }, { -0.5f, 0.5f, 0 }
    };
    static Vector3 norms[4] = { {0,0,1},{0,0,1},{0,0,1},{0,0,1} };
    static Vector2 uvs[4] = { {0,1},{1,1},{1,0},{0,0} };
    static unsigned short idx[6] = { 0,1,2, 0,2,3 };

    Mesh m = { 0 };
    m.vertexCount = 4;
    m.triangleCount = 2;
    m.vertices = RL_MALLOC(4 * sizeof(Vector3));
    m.normals = RL_MALLOC(4 * sizeof(Vector3));
    m.texcoords = RL_MALLOC(4 * sizeof(Vector2));
    m.indices = RL_MALLOC(6 * sizeof(unsigned short));
    memcpy(m.vertices, verts, sizeof(verts));
    memcpy(m.normals, norms, sizeof(norms));
    memcpy(m.texcoords, uvs, sizeof(uvs));
    memcpy(m.indices, idx, sizeof(idx));
    UploadMesh(&m, false);
    s_unit_quad = m;
    s_quad_ready = true;
}

static void draw_billboard_quad(Vector3 center, float size, Texture2D *tex, Color tint)
{
    ensure_quad();
    s_mat.maps[MATERIAL_MAP_DIFFUSE].texture = *tex;
    s_mat.maps[MATERIAL_MAP_DIFFUSE].color = tint;

    // face the camera
    Vector3 dir = Vector3Subtract(g_camera.position, center);
    float yaw = atan2f(dir.x, dir.z);
    Matrix mat = MatrixMultiply(
        MatrixScale(size, size, 1.0f),
        MatrixMultiply(MatrixRotateY(yaw), MatrixTranslate(center.x, center.y, center.z)));
    DrawMesh(s_unit_quad, s_mat, mat);
}

void world_update(float dt)
{
    for (int i = 0; i < g.map.mbox_count; i++)
    {
        MysteryBoxState *st = &g_mbox[i];
        if (st->roll_t > 0)
        {
            st->roll_t -= dt;
            if (st->roll_t <= 0)
            {
                st->offer_t = MYSTERY_OFFER_TIME;
                sfx_play("box_open", 0.9f, 1.0f);
            }
        }
        else if (st->offer_t > 0)
        {
            st->offer_t -= dt;
            if (st->offer_t <= 0)
            {
                st->offer_weapon = -1;   // offer expired, box closed
                TraceLog(LOG_INFO, "BOX: offer expired");
            }
        }
        st->lid_angle = lerpf(st->lid_angle, (st->roll_t > 0 || st->offer_t > 0) ? 1.0f : 0.0f,
                              clampf(dt * 6.0f, 0, 1));
    }
}

void world_draw(const GameMap *m)
{
    ensure_material();

    // ---- map boxes ----
    for (int i = 0; i < m->box_count; i++)
    {
        const MapBox *b = &m->boxes[i];
        s_mat.maps[MATERIAL_MAP_DIFFUSE].texture = *assets_tex(b->tex);
        s_mat.maps[MATERIAL_MAP_DIFFUSE].color = m->ambient;
        Matrix mat = MatrixTranslate(b->pos.x, b->pos.y + b->size.y * 0.5f, b->pos.z);
        DrawMesh(m->meshes[i], s_mat, mat);
    }

    // ---- barriers: boarded openings ----
    // face 0/1 = wall runs along X, 2/3 = wall runs along Z (see game.h).
    // Boards face the room; the dark backing sits BEHIND them (zombie side).
    Texture2D *board_tex = assets_tex("barrier_board");
    for (int i = 0; i < m->barrier_count; i++)
    {
        const Barrier *b = &m->barriers[i];
        Vector3 out = barrier_outward(b);   // direction the zombies come from

        // dark opening backing: thin slab, resting ON the floor (not sunk),
        // pushed to the zombie side so the boards are in front of it
        Vector3 back = { b->pos.x + out.x * 0.10f, b->pos.y + 1.05f, b->pos.z + out.z * 0.10f };
        if (b->face <= 1)
            DrawCube(back, b->size.x * 0.98f, 2.1f, 0.10f, (Color){ 8, 6, 6, 255 });
        else
            DrawCube(back, 0.10f, 2.1f, b->size.x * 0.98f, (Color){ 8, 6, 6, 255 });

        // boards: stacked planks, fewer as they get torn off
        float board_h = 2.1f / BARRIER_MAX_BOARDS;
        for (int k = 0; k < b->boards; k++)
        {
            float y = b->pos.y + board_h * (k + 0.5f);
            float wob = (k % 2 == 0) ? 0.08f : -0.06f;   // slight misalignment along the wall
            Vector3 pos = {
                b->pos.x - out.x * 0.05f + (b->face <= 1 ? wob : 0.0f), y,
                b->pos.z - out.z * 0.05f + (b->face >= 2 ? wob : 0.0f) };
            if (b->face <= 1)
                draw_textured_box(pos, (Vector3){ b->size.x * 0.96f, board_h * 0.92f, 0.12f },
                                  board_tex, WHITE);
            else
                draw_textured_box(pos, (Vector3){ 0.12f, board_h * 0.92f, b->size.x * 0.96f },
                                  board_tex, WHITE);
        }
    }

    // ---- wall buys: glowing chalk icons ----
    Texture2D *chalk = assets_tex("wallbuy_chalk");
    for (int i = 0; i < m->wallbuy_count; i++)
    {
        const WallBuy *w = &m->wallbuys[i];
        const WeaponDef *def = weapons_get(w->weapon);
        if (!def) continue;

        // a chalk gun outline quad floating on the wall + a price plate
        Vector3 c = w->pos;
        // push slightly off the wall along its normal
        float eps = 0.06f;
        if (w->face == 0) c.z += eps;
        else if (w->face == 1) c.z -= eps;
        else if (w->face == 2) c.x += eps;
        else c.x -= eps;

        // orient quad to the wall (billboard looks fine on straight walls)
        draw_billboard_quad(c, WALLBUY_ICON_SIZE, chalk, (Color){ 235, 230, 210, 200 });

        // soft glow pulse (plate oriented to the wall, not sticking out)
        float pulse = 0.5f + 0.5f * sinf(g.time * 2.0f + i);
        Vector3 glow = c;
        if (w->face == 0) glow.z += 0.03f;
        else if (w->face == 1) glow.z -= 0.03f;
        else if (w->face == 2) glow.x += 0.03f;
        else glow.x -= 0.03f;
        Color glow_col = { 120 + (int)(60 * pulse), 100, 40, 60 };
        if (w->face == 0 || w->face == 1)
            DrawCube(glow, 1.0f, 0.7f, 0.02f, glow_col);
        else
            DrawCube(glow, 0.02f, 0.7f, 1.0f, glow_col);
    }

    // ---- mystery boxes ----
    Texture2D *box_tex = assets_tex("mystery_box");
    for (int i = 0; i < m->mbox_count; i++)
    {
        const MysteryBoxLoc *loc = &m->mboxes[i];
        MysteryBoxState *st = &g_mbox[i];

        // box body
        draw_textured_box(loc->pos, (Vector3){ 1.1f, 0.7f, 0.75f }, box_tex, WHITE);

        // beacon: bobbing glowing shard in a wire cage above the box, so the
        // box reads as special/interactable from anywhere on the map
        {
            float bob = 0.10f * sinf(g.time * 2.2f + i * 1.3f);
            Vector3 at = { loc->pos.x, loc->pos.y + 1.15f + bob, loc->pos.z };
            DrawCubeWires(at, 0.55f, 1.1f, 0.55f, (Color){ 90, 200, 255, 255 });
            DrawCube(at, 0.16f, 0.30f, 0.16f, (Color){ 150, 220, 255, 255 });
        }

        // glowing seam when active
        if (st->roll_t > 0 || st->offer_t > 0)
        {
            float pulse = 0.6f + 0.4f * sinf(g.time * 8.0f);
            Color glow = (Color){ 120, 200, 255, (unsigned char)(120 * pulse) };
            DrawCubeWires((Vector3){ loc->pos.x, loc->pos.y + 0.4f, loc->pos.z },
                          1.15f, 0.85f, 0.8f, glow);
        }

        // the weapon on offer rises out of the box
        if (st->offer_t > 0)
        {
            const WeaponDef *def = weapons_get(st->offer_weapon);
            if (def)
            {
                float rise = clampf(1.0f - st->offer_t / MYSTERY_OFFER_TIME + 0.2f, 0.15f, 1.0f);
                float bob = 0.06f * sinf(g.time * 3.0f);
                Vector3 at = { loc->pos.x, loc->pos.y + 0.45f + rise * 0.55f + bob, loc->pos.z };
                float size = 0.5f + 0.2f * def->vm_len;
                DrawCube(at, 0.10f, 0.10f, size, def->tint);
                DrawCubeWires(at, 0.12f, 0.12f, size + 0.02f, (Color){ 140, 210, 255, 160 });
            }
        }
        else if (st->roll_t > 0)
        {
            // cycling: rapidly flipping weapon shapes
            int idx = (int)(st->roll_t * 12.0f) % 40;
            const WeaponDef *def = weapons_get(idx);
            if (def)
            {
                Vector3 at = { loc->pos.x, loc->pos.y + 0.6f, loc->pos.z };
                DrawCube(at, 0.10f, 0.10f, 0.4f + 0.4f * def->vm_len, def->tint);
            }
        }
    }
}
