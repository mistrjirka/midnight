/* SPDX-License-Identifier: GPL-2.0-only */
/* Q30 vector helpers for precession, alt/az and polar-axis geometry. */
#include "sphere.h"
#include "fixed_math.h"

static int64_t mul_q30(int64_t a, int64_t b)
{
    return (a * b) >> 30;
}

astro_vec3 astro_vec_from_radec(uint32_t ra, int32_t dec)
{
    int32_t sin_ra, cos_ra, sin_dec, cos_dec;
    astro_vec3 v;

    astro_sincos((int32_t)ra, &sin_ra, &cos_ra);
    astro_sincos(dec, &sin_dec, &cos_dec);
    v.x = mul_q30(cos_dec, cos_ra);
    v.y = mul_q30(cos_dec, sin_ra);
    v.z = sin_dec;
    return v;
}

int astro_vec_to_radec(astro_vec3 v, uint32_t *ra, int32_t *dec)
{
    /* Scale down until the squares below fit 64 bits; 31 bits keep ample precision. */
    while (v.x > (1LL << 31) || v.x < -(1LL << 31) || v.y > (1LL << 31) || v.y < -(1LL << 31) ||
           v.z > (1LL << 31) || v.z < -(1LL << 31)) {
        v.x /= 2;
        v.y /= 2;
        v.z /= 2;
    }
    uint64_t horizontal_sq = (uint64_t)(v.x * v.x + v.y * v.y);
    if (!horizontal_sq && !v.z) {
        return 0;
    }
    int32_t angle = astro_atan2(v.y, v.x);
    *ra = (uint32_t)(angle < 0 ? angle + ASTRO_FULL_CIRCLE : angle);
    *dec = astro_atan2(v.z, astro_isqrt64(horizontal_sq));
    return 1;
}

astro_vec3 astro_vec_sub(astro_vec3 a, astro_vec3 b)
{
    astro_vec3 d = {a.x - b.x, a.y - b.y, a.z - b.z};
    return d;
}

int64_t astro_vec_dot(astro_vec3 a, astro_vec3 b)
{
    return mul_q30(a.x, b.x) + mul_q30(a.y, b.y) + mul_q30(a.z, b.z);
}

astro_vec3 astro_vec_cross(astro_vec3 a, astro_vec3 b)
{
    astro_vec3 c;
    c.x = mul_q30(a.y, b.z) - mul_q30(a.z, b.y);
    c.y = mul_q30(a.z, b.x) - mul_q30(a.x, b.z);
    c.z = mul_q30(a.x, b.y) - mul_q30(a.y, b.x);
    return c;
}

uint32_t astro_vec_length(astro_vec3 v)
{
    int shift = 0;
    while (v.x > (1LL << 31) || v.x < -(1LL << 31) || v.y > (1LL << 31) || v.y < -(1LL << 31) ||
           v.z > (1LL << 31) || v.z < -(1LL << 31)) {
        v.x /= 2;
        v.y /= 2;
        v.z /= 2;
        shift++;
    }
    uint32_t length = astro_isqrt64((uint64_t)(v.x * v.x + v.y * v.y + v.z * v.z));
    return length << shift;
}

astro_vec3 astro_vec_rotate(astro_vec3 v, int axis, int32_t angle)
{
    int32_t s, c;
    astro_vec3 r = v;

    astro_sincos(angle, &s, &c);
    if (axis == 1) {
        r.y = mul_q30(c, v.y) + mul_q30(s, v.z);
        r.z = -mul_q30(s, v.y) + mul_q30(c, v.z);
    } else if (axis == 2) {
        r.x = mul_q30(c, v.x) - mul_q30(s, v.z);
        r.z = mul_q30(s, v.x) + mul_q30(c, v.z);
    } else {
        r.x = mul_q30(c, v.x) + mul_q30(s, v.y);
        r.y = -mul_q30(s, v.x) + mul_q30(c, v.y);
    }
    return r;
}
