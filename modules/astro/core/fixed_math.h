/* SPDX-License-Identifier: GPL-2.0-only */
/* Integer trigonometry for the camera's FPU-less ARM926. Angles are in 1e-4 degree. */
#ifndef ASTRO_FIXED_MATH_H
#define ASTRO_FIXED_MATH_H

#include <stdint.h>

#define ASTRO_FULL_CIRCLE    3600000 /* 360 degrees in 1e-4 degree */
#define ASTRO_HALF_CIRCLE    (ASTRO_FULL_CIRCLE / 2)
#define ASTRO_QUARTER_CIRCLE (ASTRO_FULL_CIRCLE / 4)
#define ASTRO_Q30_ONE        1073741824LL

/* Q30 sine and cosine of any angle. */
void astro_sincos(int32_t angle_1e4deg, int32_t *sin_q30, int32_t *cos_q30);

/* atan2(y, x) in 1e-4 degree, range (-180, 180]. Inputs may use any common scale. */
int32_t astro_atan2(int64_t y, int64_t x);

/* floor(sqrt(x)). */
uint32_t astro_isqrt64(uint64_t x);

#endif
