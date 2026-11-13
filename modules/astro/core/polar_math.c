/* SPDX-License-Identifier: GPL-2.0-only */
/* Polar alignment geometry: rotation axis from two solutions, error against the pole. */
#include "polar_math.h"
#include "astro_time.h"
#include "fixed_math.h"
#include "sphere.h"

#define SAMPLES 4

static int64_t abs64(int64_t x)
{
    return x < 0 ? -x : x;
}

/* Cross product of two small difference vectors without losing their low bits: halve the
 * components (|d| <= 2 in Q30) so the raw products and their difference fit in int64. */
static astro_vec3 halved(astro_vec3 v)
{
    astro_vec3 h = {v.x / 2, v.y / 2, v.z / 2};
    return h;
}

static astro_vec3 cross_raw(astro_vec3 a, astro_vec3 b)
{
    astro_vec3 c;
    a = halved(a);
    b = halved(b);
    c.x = a.y * b.z - a.z * b.y;
    c.y = a.z * b.x - a.x * b.z;
    c.z = a.x * b.y - a.y * b.x;
    return c;
}

static int64_t dot_raw_sign(astro_vec3 a, astro_vec3 b)
{
    /* Only the sign is used; scale both down so the products cannot overflow. */
    while (abs64(a.x) > (1LL << 30) || abs64(a.y) > (1LL << 30) || abs64(a.z) > (1LL << 30)) {
        a.x /= 2;
        a.y /= 2;
        a.z /= 2;
    }
    return a.x * (b.x >> 1) + a.y * (b.y >> 1) + a.z * (b.z >> 1);
}

static astro_vec3 normalized(astro_vec3 v)
{
    uint32_t ra;
    int32_t dec;
    astro_vec_to_radec(v, &ra, &dec);
    return astro_vec_from_radec(ra, dec);
}

static int sample_pixels(const astro_wcs *wcs, int width, int height, astro_vec3 out[SAMPLES])
{
    for (int i = 0; i < SAMPLES; i++) {
        int32_t x_q4 = (i & 1 ? 3 * width : width) * 4; /* quarter points, Q4 */
        int32_t y_q4 = (i & 2 ? 3 * height : height) * 4;
        uint32_t ra;
        int32_t dec;
        if (!astro_wcs_unproject(wcs, x_q4, y_q4, &ra, &dec)) {
            return 0;
        }
        out[i] = astro_vec_from_radec(ra, dec);
    }
    return 1;
}

/* v without its component along the unit vector axis. */
static astro_vec3 perpendicular(astro_vec3 v, astro_vec3 axis)
{
    int64_t along = astro_vec_dot(v, axis);
    astro_vec3 p = {v.x - ((along * axis.x) >> 30), v.y - ((along * axis.y) >> 30),
                    v.z - ((along * axis.z) >> 30)};
    return p;
}

/* Signed angle of the rotation about `axis` that takes `from` to `to`. */
static int32_t rotation_about(astro_vec3 axis, astro_vec3 from, astro_vec3 to)
{
    astro_vec3 a = perpendicular(from, axis);
    astro_vec3 b = perpendicular(to, axis);
    int64_t sine = astro_vec_dot(astro_vec_cross(a, b), axis);
    int64_t cosine = astro_vec_dot(a, b);
    return astro_atan2(sine, cosine);
}

astro_polar_status astro_polar_find_axis(const astro_wcs *first, const astro_wcs *second, int width,
                                         int height, astro_polar_axis *out)
{
    astro_vec3 before[SAMPLES], after[SAMPLES], moved[SAMPLES];
    astro_vec3 axis = {0, 0, 0};
    uint32_t centre_ra;
    int32_t centre_dec;

    if (!sample_pixels(first, width, height, before) ||
        !sample_pixels(second, width, height, after) ||
        !astro_wcs_unproject(first, width * 8, height * 8, &centre_ra, &centre_dec)) {
        return ASTRO_POLAR_BAD_SOLUTION;
    }
    for (int i = 0; i < SAMPLES; i++) {
        moved[i] = astro_vec_sub(after[i], before[i]);
    }

    /* Sum the cross products of all displacement pairs, oriented towards the frame. */
    astro_vec3 centre = astro_vec_from_radec(centre_ra, centre_dec);
    for (int i = 0; i < SAMPLES; i++) {
        for (int j = i + 1; j < SAMPLES; j++) {
            astro_vec3 c = cross_raw(moved[i], moved[j]);
            int sign = dot_raw_sign(c, centre) < 0 ? -1 : 1;
            /* Scale down before summing so six terms cannot overflow. */
            axis.x += sign * (c.x / 8);
            axis.y += sign * (c.y / 8);
            axis.z += sign * (c.z / 8);
        }
    }
    if (!astro_vec_to_radec(axis, &out->axis_ra, &out->axis_dec)) {
        return ASTRO_POLAR_SMALL_ROTATION;
    }

    astro_vec3 unit_axis = normalized(axis);
    out->rotation = rotation_about(unit_axis, before[0], after[0]);
    int32_t magnitude = out->rotation < 0 ? -out->rotation : out->rotation;
    if (magnitude < ASTRO_POLAR_MIN_ROTATION) {
        return ASTRO_POLAR_SMALL_ROTATION;
    }
    if (!astro_wcs_project(second, out->axis_ra, out->axis_dec, &out->axis_x_q4, &out->axis_y_q4)) {
        return ASTRO_POLAR_AXIS_OFF_FRAME_FAR;
    }
    return ASTRO_POLAR_OK;
}

static int32_t wrap_signed(int64_t angle)
{
    angle %= ASTRO_FULL_CIRCLE;
    if (angle > ASTRO_HALF_CIRCLE) {
        angle -= ASTRO_FULL_CIRCLE;
    } else if (angle <= -ASTRO_HALF_CIRCLE) {
        angle += ASTRO_FULL_CIRCLE;
    }
    return (int32_t)angle;
}

astro_polar_status astro_polar_measure(const astro_wcs *current, int32_t axis_x_q4,
                                       int32_t axis_y_q4, const astro_site *site, int64_t seconds,
                                       astro_polar_error *out)
{
    uint32_t axis_ra, pole_ra;
    int32_t axis_dec, pole_dec;
    astro_horizontal axis_h, pole_h;
    int32_t sin_alt, cos_alt;

    if (!astro_wcs_unproject(current, axis_x_q4, axis_y_q4, &axis_ra, &axis_dec)) {
        return ASTRO_POLAR_BAD_SOLUTION;
    }
    astro_pole_of_date(seconds, site->latitude >= 0, &pole_ra, &pole_dec);
    out->separation = astro_separation(axis_ra, axis_dec, pole_ra, pole_dec);
    out->pole_projectable =
        astro_wcs_project(current, pole_ra, pole_dec, &out->pole_x_q4, &out->pole_y_q4);

    astro_altaz(site, seconds, axis_ra, axis_dec, &axis_h);
    astro_altaz(site, seconds, pole_ra, pole_dec, &pole_h);
    out->altitude_error = axis_h.altitude - pole_h.altitude;

    /* Azimuth difference is shortened by cos(altitude) to an on-sky angle. */
    int32_t azimuth = wrap_signed((int64_t)axis_h.azimuth - (int64_t)pole_h.azimuth);
    astro_sincos(pole_h.altitude, &sin_alt, &cos_alt);
    out->azimuth_error = (int32_t)(((int64_t)azimuth * cos_alt) >> 30);
    return ASTRO_POLAR_OK;
}
