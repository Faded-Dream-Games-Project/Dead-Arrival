// nav.c - navigation for zombie AI: a walkability grid built from the map
// boxes plus a BFS flow field from the player. Zombies descend the field's
// gradient instead of walking straight into walls.
//
// Grid: 0.5 m XZ cells over the map bounds. Each cell can hold up to
// NAV_MAX_LEVELS "standable" ground heights (trench floor vs parapet top vs
// bunker roof), so multi-height maps work. Transitions between neighbour
// cells may step up to ZOMBIE_STEP_HEIGHT and drop up to NAV_MAX_DROP
// (zombies happily tumble off parapets, they just cannot climb back out).
//
// The field is rebuilt from the player's position at most every
// NAV_REBUILD_T seconds (or when the player changes cell / a door opens),
// and shared by every zombie - no per-zombie A*.
#include "game.h"

#define NAV_CELL          0.50f
#define NAV_MAX_LEVELS    4
#define NAV_MAX_DIM       96              // cells per axis (48 m at 0.5 m)
#define NAV_MAX_CELLS     (NAV_MAX_DIM * NAV_MAX_DIM)
#define NAV_NODES         (NAV_MAX_CELLS * NAV_MAX_LEVELS)
#define NAV_CLEARANCE     (ZOMBIE_HEIGHT + 0.10f)   // headroom needed to stand
#define NAV_STEP_UP       (ZOMBIE_STEP_HEIGHT - 0.03f)
#define NAV_MAX_DROP      3.2f            // ledges zombies may walk off
#define NAV_INFLATE       0.20f           // wall margin for standability.
                                          // Must stay below ~0.3: wider
                                          // margins seal walkable ~1 m
                                          // gaps (e.g. between seat rows
                                          // and a wall); jambs the margin
                                          // cannot cover are handled by
                                          // the ring-search fallback in
                                          // nav_steer instead
#define NAV_REBUILD_T     0.15f
#define NAV_INF           0x7fffffff

static bool  s_built = false;
static float s_x0, s_z0;                  // world origin of cell (0,0)
static int   s_w, s_h;                    // grid dimensions

static float s_ground[NAV_MAX_CELLS][NAV_MAX_LEVELS];
static int   s_nlev[NAV_MAX_CELLS];       // 0 = not standable at all
static int   s_dist[NAV_NODES];           // BFS steps, NAV_INF = unreached

// BFS bookkeeping
static int   s_queue[NAV_NODES];
static int   s_qhead, s_qtail;
static float s_rebuild_t = 0.0f;
static int   s_last_pcell = -1;
static unsigned s_door_hash = 0;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static int cell_index(float x, float z)
{
    int cx = (int)floorf((x - s_x0) / NAV_CELL);
    int cz = (int)floorf((z - s_z0) / NAV_CELL);
    if (cx < 0 || cx >= s_w || cz < 0 || cz >= s_h) return -1;
    return cz * s_w + cx;
}

static Vector3 cell_center(int c)
{
    return (Vector3){ s_x0 + ((c % s_w) + 0.5f) * NAV_CELL, 0.0f,
                      s_z0 + ((c / s_w) + 0.5f) * NAV_CELL };
}

// level whose ground is closest to y (zombies stand ON a ground, so the
// closest one below-or-near their feet is the level they occupy)
static int level_at(int c, float y)
{
    int best = -1;
    float bestd = 1.2f;                   // feet must be near a ground
    for (int l = 0; l < s_nlev[c]; l++)
    {
        float d = fabsf(s_ground[c][l] - y);
        if (d < bestd) { bestd = d; best = l; }
    }
    return best;
}

// does a closed door block standing at cell c on ground g?
static bool door_blocks(const GameMap *m, int c, float g)
{
    if (m->door_count == 0) return false;
    Vector3 cc = cell_center(c);
    for (int i = 0; i < m->door_count; i++)
    {
        if (m->doors[i].open) continue;
        BoundingBox bb = door_bb(&m->doors[i]);
        if (cc.x > bb.min.x - 0.30f && cc.x < bb.max.x + 0.30f &&
            cc.z > bb.min.z - 0.30f && cc.z < bb.max.z + 0.30f &&
            g > bb.min.y - 0.20f && g < bb.max.y - 0.20f)
            return true;
    }
    return false;
}

static unsigned doors_hash(const GameMap *m)
{
    unsigned h = 2166136261u;
    for (int i = 0; i < m->door_count; i++)
        h = (h ^ (unsigned)(m->doors[i].open ? 7 : 13)) * 16777619u;
    return h;
}

// ---------------------------------------------------------------------------
// Grid build (once per map load)
// ---------------------------------------------------------------------------
void nav_build(const GameMap *m)
{
    s_built = false;
    if (m->box_count == 0) return;

    float minx = 1e9f, minz = 1e9f, maxx = -1e9f, maxz = -1e9f;
    for (int i = 0; i < m->box_count; i++)
    {
        const MapBox *b = &m->boxes[i];
        float x0 = b->pos.x - b->size.x * 0.5f, x1 = b->pos.x + b->size.x * 0.5f;
        float z0 = b->pos.z - b->size.z * 0.5f, z1 = b->pos.z + b->size.z * 0.5f;
        if (x0 < minx) minx = x0;
        if (x1 > maxx) maxx = x1;
        if (z0 < minz) minz = z0;
        if (z1 > maxz) maxz = z1;
    }
    // pad so entities at the very edge still have cells
    s_x0 = floorf((minx - 0.75f) / NAV_CELL) * NAV_CELL;
    s_z0 = floorf((minz - 0.75f) / NAV_CELL) * NAV_CELL;
    s_w = (int)ceilf((maxx + 0.75f - s_x0) / NAV_CELL);
    s_h = (int)ceilf((maxz + 0.75f - s_z0) / NAV_CELL);
    if (s_w > NAV_MAX_DIM || s_h > NAV_MAX_DIM || s_w * s_h > NAV_MAX_CELLS)
    {
        TraceLog(LOG_WARNING, "NAV: map spans %dx%d cells (max %d) - no grid, "
                              "zombies use direct steering", s_w, s_h, NAV_MAX_DIM);
        return;
    }

    for (int cz = 0; cz < s_h; cz++)
    for (int cx = 0; cx < s_w; cx++)
    {
        int c = cz * s_w + cx;
        float px = s_x0 + (cx + 0.5f) * NAV_CELL;
        float pz = s_z0 + (cz + 0.5f) * NAV_CELL;
        s_nlev[c] = 0;

        // candidate ground heights: tops of boxes under the cell centre.
        // Footprints are expanded by half a cell so doorways with a thin
        // floor gap between two slabs (threshold stone vs hall floor) do
        // not seal the grid - the AABB physics lets entities cross such
        // gaps, so the nav grid must too.
        const float EXPAND = NAV_CELL * 0.5f;
        for (int i = 0; i < m->box_count; i++)
        {
            const MapBox *b = &m->boxes[i];
            float bx0 = b->pos.x - b->size.x * 0.5f - EXPAND;
            float bx1 = b->pos.x + b->size.x * 0.5f + EXPAND;
            float bz0 = b->pos.z - b->size.z * 0.5f - EXPAND;
            float bz1 = b->pos.z + b->size.z * 0.5f + EXPAND;
            if (px < bx0 || px > bx1 || pz < bz0 || pz > bz1) continue;
            float top = b->pos.y + b->size.y;

            // headroom: nothing may occupy the band above the top within
            // NAV_INFLATE horizontally (keeps zombies off wall faces)
            bool clear = true;
            for (int j = 0; j < m->box_count && clear; j++)
            {
                const MapBox *o = &m->boxes[j];
                float ox0 = o->pos.x - o->size.x * 0.5f - NAV_INFLATE;
                float ox1 = o->pos.x + o->size.x * 0.5f + NAV_INFLATE;
                float oz0 = o->pos.z - o->size.z * 0.5f - NAV_INFLATE;
                float oz1 = o->pos.z + o->size.z * 0.5f + NAV_INFLATE;
                float oy0 = o->pos.y, oy1 = o->pos.y + o->size.y;
                if (px < ox0 || px > ox1 || pz < oz0 || pz > oz1) continue;
                if (oy0 < top + NAV_CLEARANCE - 0.01f && oy1 > top + 0.02f)
                    clear = false;        // something sits on/above this top
            }
            if (!clear) continue;

            // keep levels sorted, deduped, capped
            int k = 0;
            while (k < s_nlev[c] && s_ground[c][k] > top + 0.05f) k++;
            if (k < s_nlev[c] && fabsf(s_ground[c][k] - top) <= 0.05f) continue;
            if (k >= NAV_MAX_LEVELS) continue;
            for (int q = s_nlev[c]; q > k; q--) s_ground[c][q] = s_ground[c][q - 1];
            s_ground[c][k] = top;
            if (s_nlev[c] < NAV_MAX_LEVELS) s_nlev[c]++;
        }
    }

    s_built = true;
    s_last_pcell = -1;
    s_rebuild_t = 0.0f;
    TraceLog(LOG_INFO, "NAV: grid %dx%d cells, origin (%.1f, %.1f)",
             s_w, s_h, s_x0, s_z0);
}

void nav_unload(void)
{
    s_built = false;
    s_w = s_h = 0;
}

// ---------------------------------------------------------------------------
// Flow field: BFS from the player over (cell, level) nodes
// ---------------------------------------------------------------------------
// level reachable in cell nc when walking from ground g; -1 if none
static int step_level(const GameMap *m, int nc, float g)
{
    int best = -1;
    float bestd = 1e9f;
    for (int l = 0; l < s_nlev[nc]; l++)
    {
        if (door_blocks(m, nc, s_ground[nc][l])) continue;
        float d = s_ground[nc][l] - g;
        if (d > NAV_STEP_UP || d < -NAV_MAX_DROP) continue;
        float ad = fabsf(d);
        if (ad < bestd) { bestd = ad; best = l; }
    }
    return best;
}

static void field_rebuild(const GameMap *m, Vector3 ppos)
{
    for (int i = 0; i < NAV_NODES; i++) s_dist[i] = NAV_INF;

    int pc = cell_index(ppos.x, ppos.z);
    if (pc < 0) return;
    int pl = level_at(pc, ppos.y);
    if (pl < 0 || door_blocks(m, pc, s_ground[pc][pl])) return;

    s_qhead = s_qtail = 0;
    s_dist[pc * NAV_MAX_LEVELS + pl] = 0;
    s_queue[s_qtail++] = pc * NAV_MAX_LEVELS + pl;

    while (s_qhead < s_qtail)
    {
        int node = s_queue[s_qhead++];
        int c = node / NAV_MAX_LEVELS, l = node % NAV_MAX_LEVELS;
        int nd = s_dist[node] + 1;
        int cx = c % s_w, cz = c / s_w;
        float g = s_ground[c][l];

        static const int dx[4] = { 1, -1, 0, 0 };
        static const int dz[4] = { 0, 0, 1, -1 };
        for (int d = 0; d < 4; d++)
        {
            int nx = cx + dx[d], nz = cz + dz[d];
            if (nx < 0 || nx >= s_w || nz < 0 || nz >= s_h) continue;
            int nc = nz * s_w + nx;
            int nl = step_level(m, nc, g);
            if (nl < 0) continue;
            int nn = nc * NAV_MAX_LEVELS + nl;
            if (s_dist[nn] != NAV_INF) continue;
            s_dist[nn] = nd;
            s_queue[s_qtail++] = nn;
        }
    }
}

// call once per frame; internally throttled
void nav_update(const GameMap *m, Vector3 player_pos, float dt)
{
    if (!s_built) return;
    s_rebuild_t -= dt;
    int pc = cell_index(player_pos.x, player_pos.z);
    unsigned dh = doors_hash(m);
    if (s_rebuild_t <= 0.0f || pc != s_last_pcell || dh != s_door_hash)
    {
        field_rebuild(m, player_pos);
        s_rebuild_t = NAV_REBUILD_T;
        s_last_pcell = pc;
        s_door_hash = dh;
    }
}

// is the straight line from..to passable for a zombie-sized body?
// casts the centre ray plus two parallel rays offset by the zombie radius -
// a single centre ray would call jambs and wall-hugging lines "clear" even
// though the zombie's body clips the wall beside it.
static bool body_line_clear(const GameMap *m, Vector3 from, Vector3 to)
{
    Vector3 dv = { to.x - from.x, 0.0f, to.z - from.z };
    float dl = Vector3Length(dv);
    if (dl < 0.01f) return true;
    Vector3 nd = { dv.x / dl, 0.0f, dv.z / dl };
    Vector3 perp = { -nd.z, 0.0f, nd.x };
    float off = ZOMBIE_RADIUS * 0.9f;

    for (int i = -1; i <= 1; i++)
    {
        Vector3 o = { from.x + perp.x * off * i, from.y, from.z + perp.z * off * i };
        Ray ray = { o, nd };
        RayCollision rc = map_raycast((GameMap *)m, ray, dl + 0.20f);
        if (rc.hit) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Steering query: where should a zombie at pos walk to make progress toward
// the player? Returns false when the field has no opinion (unreachable level
// or already at the player) - callers fall back to direct steering.
// ---------------------------------------------------------------------------
bool nav_steer(const GameMap *m, Vector3 pos, Vector3 *out_target)
{
    if (!s_built) return false;
    int c = cell_index(pos.x, pos.z);
    if (c < 0) return false;
    int l = level_at(c, pos.y);

    // Zombie standing in an unwalkable cell (pressed against a wall face,
    // on top of a prop): its own cell has no level or no field distance.
    // Scan outward for the nearest cell with a finite distance and steer
    // there - without this the caller falls back to direct steering and
    // the zombie grinds straight into the wall it is hugging.
    int d = (l >= 0) ? s_dist[c * NAV_MAX_LEVELS + l] : NAV_INF;
    if (d == NAV_INF)
    {
        int cx = c % s_w, cz = c / s_w;
        for (int r = 1; r <= 3; r++)
        {
            int bestc = -1, bestd2 = 0;
            for (int nz = cz - r; nz <= cz + r; nz++)
            for (int nx = cx - r; nx <= cx + r; nx++)
            {
                if (nx < 0 || nx >= s_w || nz < 0 || nz >= s_h) continue;
                int ring = (nx - cx > 0 ? nx - cx : cx - nx);
                if (ring < (nz - cz > 0 ? nz - cz : cz - nz)) ring = (nz - cz > 0 ? nz - cz : cz - nz);
                if (ring != r) continue;  // scan one ring at a time
                int nc = nz * s_w + nx;
                for (int nl = 0; nl < s_nlev[nc]; nl++)
                {
                    int nd = s_dist[nc * NAV_MAX_LEVELS + nl];
                    if (nd == NAV_INF) continue;
                    // match the zombie's height so we do not lure it onto
                    // a ledge it cannot actually stand on
                    if (fabsf(s_ground[nc][nl] - pos.y) > 1.0f) continue;
                    // must be reachable in a straight line - the nearest
                    // reached cell is often on the far side of the wall the
                    // zombie is pressed against
                    Vector3 t = cell_center(nc);
                    Vector3 chest = { pos.x, pos.y + 0.90f, pos.z };
                    if (!body_line_clear(m, chest, t)) continue;
                    int d2 = (nx - cx) * (nx - cx) + (nz - cz) * (nz - cz);
                    if (bestc < 0 || d2 < bestd2) { bestc = nc; bestd2 = d2; }
                }
            }
            if (bestc >= 0)
            {
                Vector3 t = cell_center(bestc);
                t.y = pos.y;
                *out_target = t;
                return true;
            }
        }
        return false;
    }
    if (d == 0) return false;   // already at the player's cell

    float g = s_ground[c][l];
    int cx = c % s_w, cz = c / s_w;

    // Pick a strictly-closer neighbour to move toward. Hugging a grid
    // staircase path makes zombies crawl at a fraction of their speed, so
    // among the closer neighbours prefer one with straight-line raycast
    // clearance; the look-ahead below then runs straight along it.
    static const int ddx[4] = { 1, -1, 0, 0 };
    static const int ddz[4] = { 0, 0, 1, -1 };
    int bd[2] = { 0, 0 };
    int bestd = d;
    bool found = false;
    // equal-distance ties break toward the player: keeps lanes centred in
    // openings instead of beelining to the outermost (jamb-hugging) column
    float pdx = g_player.pos.x - pos.x, pdz = g_player.pos.z - pos.z;
    float plen = sqrtf(pdx * pdx + pdz * pdz);
    if (plen > 0.001f) { pdx /= plen; pdz /= plen; }
    float best_align = -2.0f;

    for (int pass = 0; pass < 2; pass++)
    {
        // pass 0: clear-line neighbours only; pass 1: any (fallback)
        for (int k = 0; k < 4; k++)
        {
            int dx = ddx[k], dz = ddz[k];
            int nx = cx + dx, nz = cz + dz;
            if (nx < 0 || nx >= s_w || nz < 0 || nz >= s_h) continue;
            int nc = nz * s_w + nx;
            int nl = step_level(m, nc, g);
            if (nl < 0) continue;
            int nd = s_dist[nc * NAV_MAX_LEVELS + nl];
            if (nd > bestd) continue;
            float align = dx * pdx + dz * pdz;
            if (nd == bestd && align <= best_align) continue;

            if (pass == 0)
            {
                Vector3 cc = cell_center(nc);
                Vector3 chest = { pos.x, pos.y + 0.90f, pos.z };
                if (!body_line_clear(m, chest, cc)) continue;
            }
            bestd = nd;
            best_align = align;
            bd[0] = dx; bd[1] = dz;
            found = true;
        }
        if (found) break;   // clear-line winner (or fallback) found
    }
    if (!found) return false;

    // look ahead along the chosen direction while the field keeps improving;
    // targets a few cells out straighten the path nicely
    int fx = cx + bd[0], fz = cz + bd[1];
    for (int step = 0; step < 3; step++)
    {
        int nx = fx + bd[0], nz = fz + bd[1];
        if (nx < 0 || nx >= s_w || nz < 0 || nz >= s_h) break;
        int nc = nz * s_w + nx;
        int nl = step_level(m, nc, g);
        if (nl < 0) break;
        int nd = s_dist[nc * NAV_MAX_LEVELS + nl];
        if (nd >= bestd) break;
        bestd = nd;
        fx = nx; fz = nz;
    }

    Vector3 t = cell_center(fz * s_w + fx);
    t.y = pos.y;
    *out_target = t;
    return true;
}

// Debug: print levels + field distances of one cell
void nav_debug_cell(float x, float z)
{
    if (!s_built) { printf("NAVCELL: no grid\n"); return; }
    int c = cell_index(x, z);
    if (c < 0) { printf("NAVCELL: outside grid\n"); return; }
    printf("NAVCELL (%.2f, %.2f) cell=%d levels=%d:", x, z, c, s_nlev[c]);
    for (int l = 0; l < s_nlev[c]; l++)
        printf(" [g=%.2f d=%d]", s_ground[c][l],
               s_dist[c * NAV_MAX_LEVELS + l]);
    printf("\n");
}

// ---------------------------------------------------------------------------
// Debug: ASCII dump of the field from the last nav_update player position.
// Characters: '#' not standable, '.' standable but unreached, 0-9/A-Z+
// reached (BFS steps, base-36, capped). One char per cell, north = first row.
// ---------------------------------------------------------------------------
void nav_debug_dump(void)
{
    if (!s_built) { printf("NAVDUMP: no grid\n"); return; }
    printf("NAVDUMP grid %dx%d origin (%.1f, %.1f)\n", s_w, s_h, s_x0, s_z0);
    for (int cz = s_h - 1; cz >= 0; cz--)
    {
        for (int cx = 0; cx < s_w; cx++)
        {
            int c = cz * s_w + cx;
            if (s_nlev[c] == 0) { putchar('#'); continue; }
            int best = NAV_INF;
            for (int l = 0; l < s_nlev[c]; l++)
                if (s_dist[c * NAV_MAX_LEVELS + l] < best)
                    best = s_dist[c * NAV_MAX_LEVELS + l];
            if (best == NAV_INF) { putchar('.'); continue; }
            int v = best % 36;
            putchar(v < 10 ? '0' + v : 'A' + v - 10);
        }
        putchar('\n');
    }
}
