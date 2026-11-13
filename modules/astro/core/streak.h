/* SPDX-License-Identifier: GPL-2.0-only */
/* Satellite/aircraft trail rejection before star detection. */
#ifndef ASTRO_STREAK_H
#define ASTRO_STREAK_H

#include <stdint.h>

#define ASTRO_STREAK_MAX_WIDTH  720
#define ASTRO_STREAK_MAX_HEIGHT 480

/* Finds one long, continuous bright line and paints it with the background level.
 * Modifies only the caller's analysis copy. Returns 1 if a trail was masked. */
int astro_mask_streak(uint8_t *image, int width, int height, int stride);

#endif
