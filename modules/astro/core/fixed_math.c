/* SPDX-License-Identifier: GPL-2.0-only */
/* CORDIC sine/cosine/atan2 and integer square root. */
#include "fixed_math.h"

#define CORDIC_ITERATIONS 24

/* 1 / CORDIC gain (0.6072529350) in Q30: rotation then needs no final scaling. */
#define CORDIC_INV_GAIN_Q30 652032874

/* CORDIC works in binary angle units, where a full circle is 2^32. */
#define BINARY_FULL_CIRCLE 4294967296LL
#define BINARY_HALF_CIRCLE 2147483648LL

/* atan2 input range: big enough to keep precision in the y >> i steps, small enough that
 * the 1.65x CORDIC gain cannot overflow int64. */
#define ATAN2_MAX_INPUT (1LL << 40)
#define ATAN2_MIN_INPUT (1LL << 30)

/* atan(2^-i) in binary angle units. */
static const int32_t cordic_angles[CORDIC_ITERATIONS] = {
    536870912, 316933406, 167458907, 85004756, 42667331, 21354465, 10679838, 5340245,
    2670163,   1335087,   667544,    333772,   166886,   83443,    41722,    20861,
    10430,     5215,      2608,      1304,     652,      326,      163,      81,
};

static int64_t abs64(int64_t x)
{
    return x < 0 ? -x : x;
}

uint32_t astro_isqrt64(uint64_t x)
{
    uint64_t result = 0;
    uint64_t bit = 1ULL << 62;

    while (bit > x) {
        bit >>= 2;
    }
    while (bit) {
        if (x >= result + bit) {
            x -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t)result;
}

/* Rotation-mode CORDIC. Converges for |angle| up to about 99 degrees. */
static void cordic_rotate(int32_t angle_1e4deg, int32_t *sin_q30, int32_t *cos_q30)
{
    int64_t x = CORDIC_INV_GAIN_Q30;
    int64_t y = 0;
    int64_t z = (int64_t)angle_1e4deg * BINARY_FULL_CIRCLE / ASTRO_FULL_CIRCLE;

    for (int i = 0; i < CORDIC_ITERATIONS; i++) {
        int64_t x_prev = x;
        if (z >= 0) {
            x -= y >> i;
            y += x_prev >> i;
            z -= cordic_angles[i];
        } else {
            x += y >> i;
            y -= x_prev >> i;
            z += cordic_angles[i];
        }
    }
    *sin_q30 = (int32_t)y;
    *cos_q30 = (int32_t)x;
}

void astro_sincos(int32_t angle_1e4deg, int32_t *sin_q30, int32_t *cos_q30)
{
    int32_t angle = angle_1e4deg % ASTRO_FULL_CIRCLE;

    if (angle >= ASTRO_HALF_CIRCLE) {
        angle -= ASTRO_FULL_CIRCLE;
    }
    if (angle < -ASTRO_HALF_CIRCLE) {
        angle += ASTRO_FULL_CIRCLE;
    }

    /* Fold the outer half-circle into CORDIC's range: sin(180-a) = sin(a), cos flips. */
    if (angle > ASTRO_QUARTER_CIRCLE) {
        cordic_rotate(ASTRO_HALF_CIRCLE - angle, sin_q30, cos_q30);
        *cos_q30 = -*cos_q30;
    } else if (angle < -ASTRO_QUARTER_CIRCLE) {
        cordic_rotate(-ASTRO_HALF_CIRCLE - angle, sin_q30, cos_q30);
        *cos_q30 = -*cos_q30;
    } else {
        cordic_rotate(angle, sin_q30, cos_q30);
    }
}

int32_t astro_atan2(int64_t y, int64_t x)
{
    int64_t z = 0;

    while (abs64(x) > ATAN2_MAX_INPUT || abs64(y) > ATAN2_MAX_INPUT) {
        x >>= 1;
        y >>= 1;
    }
    if (!x && !y) {
        return 0;
    }
    /* Multiply rather than shift: left-shifting a negative value is undefined in C. */
    while (abs64(x) < ATAN2_MIN_INPUT && abs64(y) < ATAN2_MIN_INPUT) {
        x *= 2;
        y *= 2;
    }

    /* Vectoring mode only converges for x > 0: rotate the left half-plane by 180 degrees. */
    if (x < 0) {
        z = y >= 0 ? BINARY_HALF_CIRCLE : -BINARY_HALF_CIRCLE;
        x = -x;
        y = -y;
    }
    for (int i = 0; i < CORDIC_ITERATIONS; i++) {
        int64_t x_prev = x;
        if (y > 0) {
            x += y >> i;
            y -= x_prev >> i;
            z += cordic_angles[i];
        } else {
            x -= y >> i;
            y += x_prev >> i;
            z -= cordic_angles[i];
        }
    }

    int32_t angle = (int32_t)(z * ASTRO_FULL_CIRCLE / BINARY_FULL_CIRCLE);
    if (angle <= -ASTRO_HALF_CIRCLE) {
        angle += ASTRO_FULL_CIRCLE;
    }
    return angle;
}
