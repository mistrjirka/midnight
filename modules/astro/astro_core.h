/* Magic Lantern Astro: portable, allocation-free star detection and matching.
 * GPL-2.0-or-later.  All coordinates are integers. */
#ifndef ASTRO_CORE_H
#define ASTRO_CORE_H
#include <stdint.h>
#define ASTRO_MAX_STARS 32
#define ASTRO_MAX_CATALOG 64
typedef struct { int16_t x, y; uint16_t flux; } astro_star;
typedef struct { int16_t x, y; uint16_t mag10; } astro_catalog;
typedef struct {
    int32_t center_x_cdeg, center_y_cdeg;
    int matched;
    int rms_x10;  /* Mean squared residual in pixels^2 * 100 */
} astro_solution;

/* 8-bit grayscale. Writes brightest stars first. No allocations. */
int astro_find_stars(const uint8_t *pixels, int width, int height, int pitch,
                     astro_star *out, int max_stars);
/* Star sharpness: flux-weighted radius squared, multiplied by 16.
 * Smaller is better. Returns -1 if no suitable star. */
int astro_focus_metric(const uint8_t *pixels, int width, int height,
                       int pitch, int *star_x, int *star_y);
/* Lock to the same star between frames. The caller must provide a
 * previous centroid. Reject rather than silently switching targets. */
int astro_focus_metric_locked(const uint8_t *pixels, int width, int height,
                              int pitch, int *star_x, int *star_y, int radius);
/* Approximate horizontal FOV in centidegrees for a rectilinear lens.
 * Valid for narrow/medium fields; extreme wide angles need atan projection.
 * sensor_width_x10 is sensor width in tenths of mm. */
int astro_fov_cdeg(int sensor_width_x10,int focal_mm);
/* Hinted pattern matching. Catalog coordinates in centidegrees relative
 * to the user's approximate pointing; +x is east, +y is north.
 * fov_width_cdeg is the image width in centidegrees.
 * Results are relative to the supplied catalog coordinate origin. */
int astro_solve_hinted(const astro_star *obs, int nobs,
                       const astro_catalog *cat, int ncat,
                       int width, int height, int fov_width_cdeg,
                       astro_solution *result);
#endif
