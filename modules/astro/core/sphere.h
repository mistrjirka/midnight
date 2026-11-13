/* SPDX-License-Identifier: GPL-2.0-only */
/* Unit vectors on the celestial sphere in Q30 fixed point. */
#ifndef ASTRO_SPHERE_H
#define ASTRO_SPHERE_H

#include <stdint.h>

typedef struct {
    int64_t x, y, z; /* Q30; |v| = 1 for directions, but any length is accepted */
} astro_vec3;

astro_vec3 astro_vec_from_radec(uint32_t ra, int32_t dec);

/* Direction of v (any non-zero length) as RA [0, 360) and Dec, in 1e-4 degree.
 * Returns 0 for a zero vector. */
int astro_vec_to_radec(astro_vec3 v, uint32_t *ra, int32_t *dec);

astro_vec3 astro_vec_sub(astro_vec3 a, astro_vec3 b);
int64_t astro_vec_dot(astro_vec3 a, astro_vec3 b);      /* Q30 for Q30 inputs */
astro_vec3 astro_vec_cross(astro_vec3 a, astro_vec3 b); /* Q30 for Q30 inputs */
uint32_t astro_vec_length(astro_vec3 v);                /* same scale as the input */

/* Rotates v about the x (1), y (2) or z (3) axis by angle (1e-4 degree). */
astro_vec3 astro_vec_rotate(astro_vec3 v, int axis, int32_t angle);

#endif
