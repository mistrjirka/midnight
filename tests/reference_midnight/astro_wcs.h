/* Sky <-> image projection for a verified plate solution.
 * GPL-2.0-or-later. Integer/CORDIC only: no floating point on the camera.
 *
 * Angles: RA in 1e-4 degree [0,3600000), Dec in 1e-4 degree.
 * Image positions are Q4 (1/16 pixel) in the solver's image coordinates.
 */
#ifndef ASTRO_WCS_H
#define ASTRO_WCS_H
#include <stdint.h>

#define ASTRO_FULL_CIRCLE 3600000
/* Gnomonic tangent point plus the affine map from tangent-plane
 * millidegrees (x=east, y=north) to pixels. m is Q24 pixels per
 * millidegree (precision for wide lenses), t is Q16 pixels:
 *   px*65536 = (m[0]*x + m[1]*y)/256 + t[0],  likewise py with m[2], m[3]. */
typedef struct {
    uint32_t ra0;
    int32_t dec0;
    int32_t m[4];
    int32_t t[2];
} astro_wcs;

/* Q30 sine and cosine of an angle in 1e-4 degree (any value). */
void astro_sincos(int32_t angle, int32_t *sin_q30, int32_t *cos_q30);
/* atan2(y,x) in 1e-4 degree, range (-180,180]. */
int32_t astro_atan2(int64_t y, int64_t x);
uint32_t astro_isqrt64(uint64_t x);

/* Projects a sky position. Returns 0 when the point is more than ~78 degrees
 * from the tangent point (gnomonic projection undefined or useless). */
int astro_wcs_project(const astro_wcs *w, uint32_t ra, int32_t dec,
                      int32_t *x16, int32_t *y16);
/* Inverse of astro_wcs_project. Returns 0 for a degenerate transform. */
int astro_wcs_unproject(const astro_wcs *w, int32_t x16, int32_t y16,
                        uint32_t *ra, int32_t *dec);
/* Pixel displacement (Q4) of one degree towards north and towards east
 * at a sky position. Robust at the celestial poles. */
int astro_wcs_axes(const astro_wcs *w, uint32_t ra, int32_t dec,
                   int32_t north16[2], int32_t east16[2]);
/* Great-circle separation in 1e-4 degree. */
uint32_t astro_separation(uint32_t ra1, int32_t dec1, uint32_t ra2, int32_t dec2);
/* Image direction (unit vector, Q14) from one sky position towards another,
 * valid for any separation, e.g. an arrow towards an off-frame target.
 * Returns 0 if the direction is undefined (identical points or bad WCS). */
int astro_wcs_direction(const astro_wcs *w, uint32_t ra_from, int32_t dec_from,
                        uint32_t ra_to, int32_t dec_to,
                        int32_t *dx_q14, int32_t *dy_q14);
/* Horizontal field of view (centidegrees) across the middle of the image. */
int astro_wcs_fov_cdeg(const astro_wcs *w, int width, int height);
/* Rectilinear lens horizontal FOV in centidegrees (exact atan form),
 * and its inverse: focal length in 0.1 mm from FOV. */
int astro_lens_fov_cdeg(int sensor_width_x10, int focal_mm);
int astro_lens_focal_x10(int sensor_width_x10, int fov_cdeg);
#endif
