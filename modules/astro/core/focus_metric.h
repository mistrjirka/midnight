/* SPDX-License-Identifier: GPL-2.0-only */
/* Star-size focus score. Smaller is sharper. */
#ifndef ASTRO_FOCUS_METRIC_H
#define ASTRO_FOCUS_METRIC_H

#include <stdint.h>
#include "star.h"

/* Simple threshold/centroid detector used to pick a focus star (not the solver's). */
int astro_find_focus_stars(const uint8_t *image, int width, int height, int stride, astro_star *out,
                           int max_stars);

/* Flux-weighted mean radius squared of the strongest central star, times 16.
 * Updates (*star_x, *star_y) with its centroid. Returns -1 if no suitable star. */
int astro_focus_metric(const uint8_t *image, int width, int height, int stride, int *star_x,
                       int *star_y);

/* Same metric, but only for the star near (*star_x, *star_y) within radius pixels.
 * Rejects rather than silently switching to another star. */
int astro_focus_metric_locked(const uint8_t *image, int width, int height, int stride, int *star_x,
                              int *star_y, int radius);

#endif
