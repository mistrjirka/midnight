/* SPDX-License-Identifier: GPL-2.0-only */
/* Detected star: integer pixel position in the 8-bit analysis image. */
#ifndef ASTRO_STAR_H
#define ASTRO_STAR_H

#include <stdint.h>

/* The solver tracks matches in 32-bit masks, so this is a hard upper bound. */
#define ASTRO_MAX_STARS 32

typedef struct {
    int16_t x;
    int16_t y;
    uint16_t flux; /* detector-specific brightness; stars are sorted by it, brightest first */
} astro_star;

/* Median of a sparse 5-pixel grid sample: the sky background level. */
int astro_sky_background(const uint8_t *image, int width, int height, int stride);

#endif
