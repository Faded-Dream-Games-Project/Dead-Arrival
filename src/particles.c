// particles.c - blood, sparks, tracers, muzzle flashes and wood debris
#include "game.h"

static Particle g_particles[MAX_PARTICLES];

void particles_reset(void)
{
    memset(g_particles, 0, sizeof(g_particles));
}

static void spawn(ParticleKind kind, Vector3 pos, Vector3 vel,
                  float life, float size, Color color)
{
    for (int i = 0; i < MAX_PARTICLES; i++)
    {
        Particle *p = &g_particles[i];
        if (p->active) continue;
        p->active = true;
        p->kind = kind;
        p->pos = p->prev = pos;
        p->vel = vel;
        p->life = p->max_life = life;
        p->size = size;
        p->color = color;
        return;
    }
}

void particles_spawn_blood(Vector3 at, int n)
{
    for (int i = 0; i < n; i++)
    {
        Vector3 vel = { frand_range(-2.2f, 2.2f), frand_range(0.5f, 3.4f), frand_range(-2.2f, 2.2f) };
        Color c = (GetRandomValue(0, 1) == 0) ?
            (Color){ 130, 15, 15, 255 } : (Color){ 90, 10, 10, 255 };
        spawn(P_BLOOD, at, vel, frand_range(0.3f, 0.7f), frand_range(0.03f, 0.08f), c);
    }
}

void particles_spawn_sparks(Vector3 at, int n)
{
    for (int i = 0; i < n; i++)
    {
        Vector3 vel = { frand_range(-3.5f, 3.5f), frand_range(0.5f, 3.0f), frand_range(-3.5f, 3.5f) };
        spawn(P_SPARK, at, vel, frand_range(0.1f, 0.3f), 0.02f, (Color){ 255, 220, 140, 255 });
    }
}

void particles_spawn_tracer(Vector3 from, Vector3 to)
{
    Particle *p = NULL;
    for (int i = 0; i < MAX_PARTICLES; i++)
        if (!g_particles[i].active) { p = &g_particles[i]; break; }
    if (!p) return;
    p->active = true;
    p->kind = P_TRACER;
    p->prev = from;
    p->pos = to;
    p->life = p->max_life = 0.06f;
    p->size = 0.015f;
    p->color = (Color){ 255, 230, 160, 255 };
}

void particles_spawn_muzzle(Vector3 at)
{
    Vector3 vel = { 0, 0, 0 };
    spawn(P_MUZZLE, at, vel, 0.05f, 0.16f, (Color){ 255, 235, 170, 255 });
}

void particles_spawn_debris(Vector3 at, int n)
{
    for (int i = 0; i < n; i++)
    {
        Vector3 vel = { frand_range(-2.0f, 2.0f), frand_range(1.0f, 3.0f), frand_range(-2.0f, 2.0f) };
        spawn(P_DEBRIS, at, vel, frand_range(0.4f, 0.9f), frand_range(0.03f, 0.07f),
              (Color){ 120, 85, 50, 255 });
    }
}

void particles_update(float dt)
{
    for (int i = 0; i < MAX_PARTICLES; i++)
    {
        Particle *p = &g_particles[i];
        if (!p->active) continue;
        p->life -= dt;
        if (p->life <= 0) { p->active = false; continue; }
        if (p->kind == P_TRACER || p->kind == P_MUZZLE) continue;

        p->prev = p->pos;
        p->vel.y -= 12.0f * dt;
        p->pos = Vector3Add(p->pos, Vector3Scale(p->vel, dt));
    }
}

void particles_draw(void)
{
    for (int i = 0; i < MAX_PARTICLES; i++)
    {
        Particle *p = &g_particles[i];
        if (!p->active) continue;
        float t = p->life / p->max_life;
        Color c = p->color;
        c.a = (unsigned char)(255 * clampf(t, 0, 1));

        switch (p->kind)
        {
            case P_TRACER:
            {
                Vector3 dir = Vector3Subtract(p->pos, p->prev);
                float len = Vector3Length(dir);
                if (len < 0.01f) break;
                DrawLine3D(p->prev, p->pos, c);
            } break;
            case P_MUZZLE:
            {
                DrawSphere(p->pos, p->size * (0.6f + 0.4f * t), c);
            } break;
            case P_SPARK:
            {
                DrawLine3D(p->pos, Vector3Add(p->pos, Vector3Scale(p->vel, 0.02f)), c);
            } break;
            default:
            {
                DrawCube(p->pos, p->size, p->size, p->size, c);
            } break;
        }
    }
}
