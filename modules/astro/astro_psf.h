/* Portable star candidate selection for plate solving only.
 * Does not change the lens focus metric or LiveView source buffer.
 */
#ifndef ASTRO_PSF_H
#define ASTRO_PSF_H
#include "astro_core.h"
/* Detect star-shaped peaks by a difference of two Gaussian blurs.
 * Returns brightest compact peaks, never more than max_stars. w<=360,
 * h<=240. No heap allocation; uses bounded module scratch RAM. */
int astro_find_stars_psf(const uint8_t *im,int w,int h,int stride,
                        astro_star *out,int max_stars);
#endif
