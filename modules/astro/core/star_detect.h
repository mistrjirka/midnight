/* SPDX-License-Identifier: GPL-2.0-only */
/* Star candidates for plate solving: difference-of-Gaussians peak detection. */
#ifndef ASTRO_STAR_DETECT_H
#define ASTRO_STAR_DETECT_H

#include <stdint.h>
#include "star.h"

#define ASTRO_DETECT_MAX_WIDTH  360
#define ASTRO_DETECT_MAX_HEIGHT 240
#define ASTRO_DETECT_RING_ROWS  31 /* rows of the wide Gaussian kernel */

/* About 216 KB. Allocate it only for the duration of a solve. */
typedef struct {
    uint16_t small_rows[ASTRO_DETECT_RING_ROWS][ASTRO_DETECT_MAX_WIDTH];
    uint16_t large_rows[ASTRO_DETECT_RING_ROWS][ASTRO_DETECT_MAX_WIDTH];
    int16_t response[ASTRO_DETECT_MAX_WIDTH * ASTRO_DETECT_MAX_HEIGHT];
} astro_detect_scratch;

/* Finds compact star-shaped peaks, brightest first. Returns the number written (at most
 * max_stars and ASTRO_MAX_STARS). The image is not modified. */
int astro_detect_stars(const uint8_t *image, int width, int height, int stride,
                       astro_detect_scratch *scratch, astro_star *out, int max_stars);

#endif
