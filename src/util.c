// util.c - small math / string helpers shared by all modules
#include "game.h"

float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

float lerpf(float a, float b, float t) { return a + (b - a) * t; }

float frand_range(float lo, float hi)
{
    return lo + (hi - lo) * ((float)GetRandomValue(0, 100000) / 100000.0f);
}

float v3_dist_xz(Vector3 a, Vector3 b)
{
    float dx = a.x - b.x, dz = a.z - b.z;
    return sqrtf(dx * dx + dz * dz);
}

float angle_lerp(float a, float b, float t)
{
    // shortest-path angle interpolation
    float d = fmodf(b - a, 2.0f * PI);
    if (d >  PI) d -= 2.0f * PI;
    if (d < -PI) d += 2.0f * PI;
    return a + d * t;
}
