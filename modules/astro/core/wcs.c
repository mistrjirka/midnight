/* SPDX-License-Identifier: GPL-2.0-only */
/* Gnomonic projection with integer/CORDIC arithmetic only. */
#include "wcs.h"

/* Q30 tangent-plane value -> 1/16 millidegree: 16 * 180000 / pi. */
#define TANGENT_Q30_TO_MDEG_Q4 916732

/* 1/16 millidegree -> Q30 tangent: 2^30 * pi / 180000 / 16, as a ratio. */
#define MDEG_Q4_TO_Q30_NUM 18740330
#define MDEG_Q4_TO_Q30_DEN 16000

/* Reject points whose cos(distance from tangent point) is below 0.2 (~78 degrees). */
#define MIN_COS_DISTANCE_Q30 (ASTRO_Q30_ONE / 5)

/* Q24 matrix times Q16 translation alignment: 2^(24-16) * 16 (Q4 output). */
#define TRANSLATION_TO_Q28 4096
#define ROUND_Q24          (1 << 23)

#define PROJECT_LIMIT   (1LL << 30)
#define UNPROJECT_LIMIT (1LL << 33)
#define ONE_DEGREE_1E4  10000
#define NORTH_POLE_1E4  900000
#define UNIT_VECTOR_Q14 16384
#define CDEG_PER_1E4DEG 100

static int64_t abs64(int64_t x)
{
    return x < 0 ? -x : x;
}

static int64_t mul_q30(int64_t a, int64_t b)
{
    return (a * b) >> 30;
}

int astro_wcs_project(const astro_wcs *wcs, uint32_t ra, int32_t dec, int32_t *x_q4, int32_t *y_q4)
{
    int32_t sin_dec, cos_dec;
    int32_t sin_dec0, cos_dec0;
    int32_t sin_dra, cos_dra;

    astro_sincos(dec, &sin_dec, &cos_dec);
    astro_sincos(wcs->dec0, &sin_dec0, &cos_dec0);
    astro_sincos((int32_t)ra - (int32_t)wcs->ra0, &sin_dra, &cos_dra);

    int64_t cos_dec_cos_dra = mul_q30(cos_dec, cos_dra);
    int64_t cos_distance = mul_q30(sin_dec0, sin_dec) + mul_q30(cos_dec0, cos_dec_cos_dra);
    if (cos_distance < MIN_COS_DISTANCE_Q30) {
        return 0;
    }

    int64_t xi = mul_q30(cos_dec, sin_dra);
    int64_t eta = mul_q30(cos_dec0, sin_dec) - mul_q30(sin_dec0, cos_dec_cos_dra);
    int64_t x_mdeg_q4 = xi * TANGENT_Q30_TO_MDEG_Q4 / cos_distance;
    int64_t y_mdeg_q4 = eta * TANGENT_Q30_TO_MDEG_Q4 / cos_distance;

    int64_t px = (int64_t)wcs->m_q24[0] * x_mdeg_q4 + (int64_t)wcs->m_q24[1] * y_mdeg_q4 +
                 (int64_t)wcs->t_q16[0] * TRANSLATION_TO_Q28;
    int64_t py = (int64_t)wcs->m_q24[2] * x_mdeg_q4 + (int64_t)wcs->m_q24[3] * y_mdeg_q4 +
                 (int64_t)wcs->t_q16[1] * TRANSLATION_TO_Q28;
    px = (px + ROUND_Q24) >> 24;
    py = (py + ROUND_Q24) >> 24;
    if (abs64(px) > PROJECT_LIMIT || abs64(py) > PROJECT_LIMIT) {
        return 0;
    }

    *x_q4 = (int32_t)px;
    *y_q4 = (int32_t)py;
    return 1;
}

int astro_wcs_unproject(const astro_wcs *wcs, int32_t x_q4, int32_t y_q4, uint32_t *ra,
                        int32_t *dec)
{
    const int32_t *m = wcs->m_q24;
    int64_t u = (int64_t)x_q4 * TRANSLATION_TO_Q28 - wcs->t_q16[0];
    int64_t v = (int64_t)y_q4 * TRANSLATION_TO_Q28 - wcs->t_q16[1];
    int64_t det = ((int64_t)m[0] * m[3] - (int64_t)m[1] * m[2]) / TRANSLATION_TO_Q28;
    if (!det) {
        return 0;
    }

    /* Invert the affine map, then convert tangent millidegrees to Q30 tangent units. */
    int64_t x_mdeg_q4 = ((int64_t)m[3] * u - (int64_t)m[1] * v) / det;
    int64_t y_mdeg_q4 = ((int64_t)m[0] * v - (int64_t)m[2] * u) / det;
    int64_t xi = x_mdeg_q4 * MDEG_Q4_TO_Q30_NUM / MDEG_Q4_TO_Q30_DEN;
    int64_t eta = y_mdeg_q4 * MDEG_Q4_TO_Q30_NUM / MDEG_Q4_TO_Q30_DEN;
    if (abs64(xi) > UNPROJECT_LIMIT || abs64(eta) > UNPROJECT_LIMIT) {
        return 0;
    }

    int32_t sin_dec0, cos_dec0;
    astro_sincos(wcs->dec0, &sin_dec0, &cos_dec0);
    int64_t horizontal = cos_dec0 - ((eta * sin_dec0) >> 30);
    int64_t vertical = sin_dec0 + ((eta * cos_dec0) >> 30);

    /* Pre-shift by 2 so the squares stay inside 64 bits. */
    int64_t h_quarter = horizontal >> 2;
    int64_t xi_quarter = xi >> 2;
    int64_t rho = astro_isqrt64((uint64_t)(h_quarter * h_quarter + xi_quarter * xi_quarter));

    int64_t ra_out = (int64_t)wcs->ra0 + astro_atan2(xi, horizontal);
    while (ra_out < 0) {
        ra_out += ASTRO_FULL_CIRCLE;
    }
    while (ra_out >= ASTRO_FULL_CIRCLE) {
        ra_out -= ASTRO_FULL_CIRCLE;
    }
    *ra = (uint32_t)ra_out;
    *dec = astro_atan2(vertical >> 2, rho);
    return 1;
}

int astro_wcs_axes(const astro_wcs *wcs, uint32_t ra, int32_t dec, int32_t north_q4[2],
                   int32_t east_q4[2])
{
    int32_t x0, y0, x1, y1;
    int32_t sign = 1;
    int32_t dec_step = dec + ONE_DEGREE_1E4;

    /* Past the pole, step south and flip the sign instead. */
    if (dec_step > NORTH_POLE_1E4) {
        dec_step = dec - ONE_DEGREE_1E4;
        sign = -1;
    }
    if (!astro_wcs_project(wcs, ra, dec, &x0, &y0) ||
        !astro_wcs_project(wcs, ra, dec_step, &x1, &y1)) {
        return 0;
    }
    north_q4[0] = sign * (x1 - x0);
    north_q4[1] = sign * (y1 - y0);

    /* The local map is a similarity: east is north turned by 90 degrees, clockwise without
     * a mirror image, anticlockwise with one. */
    int64_t det = (int64_t)wcs->m_q24[0] * wcs->m_q24[3] - (int64_t)wcs->m_q24[1] * wcs->m_q24[2];
    if (det > 0) {
        east_q4[0] = north_q4[1];
        east_q4[1] = -north_q4[0];
    } else {
        east_q4[0] = -north_q4[1];
        east_q4[1] = north_q4[0];
    }
    return 1;
}

/* Local east/north components (Q30) of the great circle from point 1 to point 2, and the
 * cosine of their separation. */
static void local_offset(uint32_t ra1, int32_t dec1, uint32_t ra2, int32_t dec2, int64_t *east,
                         int64_t *north, int64_t *cos_sep)
{
    int32_t sin1, cos1, sin2, cos2, sin_dra, cos_dra;

    astro_sincos(dec1, &sin1, &cos1);
    astro_sincos(dec2, &sin2, &cos2);
    astro_sincos((int32_t)ra2 - (int32_t)ra1, &sin_dra, &cos_dra);

    int64_t cos2_cos_dra = mul_q30(cos2, cos_dra);
    *east = mul_q30(cos2, sin_dra);
    *north = mul_q30(cos1, sin2) - mul_q30(sin1, cos2_cos_dra);
    *cos_sep = mul_q30(sin1, sin2) + mul_q30(cos1, cos2_cos_dra);
}

uint32_t astro_separation(uint32_t ra1, int32_t dec1, uint32_t ra2, int32_t dec2)
{
    int64_t east, north, cos_sep;

    local_offset(ra1, dec1, ra2, dec2, &east, &north, &cos_sep);
    int64_t sin_sep = astro_isqrt64((uint64_t)(east * east + north * north));
    int32_t angle = astro_atan2(sin_sep, cos_sep);
    return (uint32_t)(angle < 0 ? -angle : angle);
}

int astro_wcs_direction(const astro_wcs *wcs, uint32_t ra_from, int32_t dec_from, uint32_t ra_to,
                        int32_t dec_to, int32_t *dx_q14, int32_t *dy_q14)
{
    int32_t north[2], east[2];
    int64_t e, n, cos_sep;

    if (!astro_wcs_axes(wcs, ra_from, dec_from, north, east)) {
        return 0;
    }
    local_offset(ra_from, dec_from, ra_to, dec_to, &e, &n, &cos_sep);
    e >>= 14;
    n >>= 14;

    int64_t vx = e * east[0] + n * north[0];
    int64_t vy = e * east[1] + n * north[1];
    while (abs64(vx) > (1LL << 30) || abs64(vy) > (1LL << 30)) {
        vx >>= 1;
        vy >>= 1;
    }
    int64_t length = astro_isqrt64((uint64_t)(vx * vx + vy * vy));
    if (!length) {
        return 0;
    }
    *dx_q14 = (int32_t)(vx * UNIT_VECTOR_Q14 / length);
    *dy_q14 = (int32_t)(vy * UNIT_VECTOR_Q14 / length);
    return 1;
}

int astro_wcs_fov_cdeg(const astro_wcs *wcs, int width, int height)
{
    uint32_t ra_left, ra_right;
    int32_t dec_left, dec_right;

    /* Left and right edge midpoints, in Q4 pixels. */
    if (!astro_wcs_unproject(wcs, 0, height * 8, &ra_left, &dec_left) ||
        !astro_wcs_unproject(wcs, width * 16, height * 8, &ra_right, &dec_right)) {
        return 0;
    }
    uint32_t separation = astro_separation(ra_left, dec_left, ra_right, dec_right);
    return (int)((separation + CDEG_PER_1E4DEG / 2) / CDEG_PER_1E4DEG);
}

int astro_lens_fov_cdeg(int sensor_width_x10, int focal_mm)
{
    if (sensor_width_x10 < 100 || sensor_width_x10 > 600 || focal_mm < 4 || focal_mm > 3000) {
        return 0;
    }
    /* FOV = 2 * atan(sensor / (2 f)); sensor is in 0.1 mm, hence f * 20. */
    int32_t half_angle = astro_atan2(sensor_width_x10, (int64_t)focal_mm * 20);
    return (2 * half_angle + CDEG_PER_1E4DEG / 2) / CDEG_PER_1E4DEG;
}

int astro_lens_focal_x10(int sensor_width_x10, int fov_cdeg)
{
    int32_t sin_half, cos_half;

    if (sensor_width_x10 < 100 || sensor_width_x10 > 600 || fov_cdeg < 10 || fov_cdeg > 17000) {
        return 0;
    }
    /* Half the FOV in 1e-4 degree: centidegrees * 100 / 2. */
    astro_sincos(fov_cdeg * 50, &sin_half, &cos_half);
    if (sin_half <= 0) {
        return 0;
    }
    /* f = sensor / (2 tan(half)) */
    return (int)(((int64_t)sensor_width_x10 * cos_half + sin_half) / (2 * (int64_t)sin_half));
}
