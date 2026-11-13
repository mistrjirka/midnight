/* SPDX-License-Identifier: GPL-2.0-only */
/* Sky <-> image projection of a verified plate solution (gnomonic + affine). */
#ifndef ASTRO_WCS_H
#define ASTRO_WCS_H

#include <stdint.h>
#include "fixed_math.h"

/*
 * Tangent point (ra0, dec0) in 1e-4 degree, plus an affine map from tangent-plane
 * millidegrees (x = east, y = north) to image pixels:
 *
 *   px * 65536 = (m_q24[0] * x + m_q24[1] * y) / 256 + t_q16[0]
 *   py * 65536 = (m_q24[2] * x + m_q24[3] * y) / 256 + t_q16[1]
 *
 * Image positions passed in and out are Q4 (1/16 pixel).
 */
typedef struct {
    uint32_t ra0;
    int32_t dec0;
    int32_t m_q24[4];
    int32_t t_q16[2];
} astro_wcs;

/* Returns 0 when the point is more than ~78 degrees from the tangent point. */
int astro_wcs_project(const astro_wcs *wcs, uint32_t ra, int32_t dec, int32_t *x_q4, int32_t *y_q4);

/* Inverse of astro_wcs_project. Returns 0 for a degenerate transform. */
int astro_wcs_unproject(const astro_wcs *wcs, int32_t x_q4, int32_t y_q4, uint32_t *ra,
                        int32_t *dec);

/* Image displacement (Q4) of one degree towards north and towards east at a sky position.
 * Robust at the celestial poles. */
int astro_wcs_axes(const astro_wcs *wcs, uint32_t ra, int32_t dec, int32_t north_q4[2],
                   int32_t east_q4[2]);

/* Great-circle separation in 1e-4 degree. */
uint32_t astro_separation(uint32_t ra1, int32_t dec1, uint32_t ra2, int32_t dec2);

/* Image direction (Q14 unit vector) from one sky position towards another, valid for any
 * separation. Returns 0 if undefined (identical points or a bad transform). */
int astro_wcs_direction(const astro_wcs *wcs, uint32_t ra_from, int32_t dec_from, uint32_t ra_to,
                        int32_t dec_to, int32_t *dx_q14, int32_t *dy_q14);

/* Horizontal field of view across the middle of the image, in centidegrees. */
int astro_wcs_fov_cdeg(const astro_wcs *wcs, int width, int height);

/* Rectilinear lens: horizontal FOV from focal length, and focal length (0.1 mm) from FOV. */
int astro_lens_fov_cdeg(int sensor_width_x10, int focal_mm);
int astro_lens_focal_x10(int sensor_width_x10, int fov_cdeg);

#endif
