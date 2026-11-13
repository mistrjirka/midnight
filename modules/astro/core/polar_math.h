/* SPDX-License-Identifier: GPL-2.0-only */
/* Mount RA-axis direction from two plate solutions, and its error against the pole. */
#ifndef ASTRO_POLAR_MATH_H
#define ASTRO_POLAR_MATH_H

#include <stdint.h>
#include "altaz.h"
#include "wcs.h"

/* Rotations smaller than this give a poorly conditioned axis. */
#define ASTRO_POLAR_MIN_ROTATION 150000 /* 15 degrees, 1e-4 degree */

typedef enum {
    ASTRO_POLAR_OK = 0,
    ASTRO_POLAR_SMALL_ROTATION, /* rotate the RA axis further between the two frames */
    ASTRO_POLAR_BAD_SOLUTION,
    ASTRO_POLAR_AXIS_OFF_FRAME_FAR /* axis > ~78 degrees from the image: not near a pole */
} astro_polar_status;

typedef struct {
    uint32_t axis_ra; /* J2000, 1e-4 degree */
    int32_t axis_dec;
    int32_t rotation;  /* 1e-4 degree between the two frames */
    int32_t axis_x_q4; /* the axis' fixed pixel in the image (may lie outside it) */
    int32_t axis_y_q4;
} astro_polar_axis;

/*
 * The camera turns rigidly about the mount axis, so the sky point seen at any pixel moves
 * on a circle around that axis: its displacement between the two frames is perpendicular
 * to the axis. The cross product of two displacements is therefore the axis direction,
 * exactly, whatever the lens projection.
 */
astro_polar_status astro_polar_find_axis(const astro_wcs *first, const astro_wcs *second, int width,
                                         int height, astro_polar_axis *out);

typedef struct {
    uint32_t separation;    /* axis to pole, 1e-4 degree */
    int32_t altitude_error; /* axis altitude minus pole altitude: positive means lower it */
    int32_t azimuth_error;  /* on-sky azimuth offset: positive means move the axis west */
    int32_t pole_x_q4;      /* where the true pole is in the current image */
    int32_t pole_y_q4;
    int pole_projectable;
} astro_polar_error;

/* Error of a known axis pixel against the celestial pole of date, using the current
 * solution (live adjustment re-solves and calls this with the same axis pixel). */
astro_polar_status astro_polar_measure(const astro_wcs *current, int32_t axis_x_q4,
                                       int32_t axis_y_q4, const astro_site *site,
                                       int64_t seconds_since_j2000, astro_polar_error *out);

#endif
