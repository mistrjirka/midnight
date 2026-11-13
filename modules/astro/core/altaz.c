/* SPDX-License-Identifier: GPL-2.0-only */
/* Alt/az from hour angle, and horizon crossings of the precessed position. */
#include "altaz.h"
#include "astro_time.h"
#include "fixed_math.h"

/* Rising/setting altitude: -34' refraction at the horizon (stars, no semi-diameter). */
#define HORIZON_1E4DEG (-5667)

/* Sidereal minutes per 1e-4 degree of hour angle: 1436.07 minutes per 360 degrees. */
#define SIDEREAL_MINUTES_PER_TURN 143607LL /* x100 */

static int64_t mul_q30(int64_t a, int64_t b)
{
    return (a * b) >> 30;
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

uint32_t astro_local_sidereal_time(const astro_site *site, int64_t seconds)
{
    int64_t lst = (int64_t)astro_gmst(seconds) + site->longitude;
    lst %= ASTRO_FULL_CIRCLE;
    return (uint32_t)(lst < 0 ? lst + ASTRO_FULL_CIRCLE : lst);
}

/* Hour angle (1e-4 degree, positive) at which the object crosses the rising/setting
 * altitude, or -1 if it never sets and -2 if it never rises. */
static int32_t horizon_hour_angle(int32_t latitude, int32_t dec)
{
    int32_t sin_lat, cos_lat, sin_dec, cos_dec, sin_h0, cos_h0;

    astro_sincos(latitude, &sin_lat, &cos_lat);
    astro_sincos(dec, &sin_dec, &cos_dec);
    astro_sincos(HORIZON_1E4DEG, &sin_h0, &cos_h0);

    /* cos(H0) = (sin h0 - sin lat sin dec) / (cos lat cos dec) */
    int64_t denominator = mul_q30(cos_lat, cos_dec);
    if (denominator <= 0) {
        return sin_lat * (int64_t)sin_dec > 0 ? -1 : -2;
    }
    int64_t cos_h = ((int64_t)sin_h0 - mul_q30(sin_lat, sin_dec)) * ASTRO_Q30_ONE / denominator;
    if (cos_h <= -ASTRO_Q30_ONE) {
        return -1;
    }
    if (cos_h >= ASTRO_Q30_ONE) {
        return -2;
    }
    int64_t sin_h = astro_isqrt64((uint64_t)(ASTRO_Q30_ONE * ASTRO_Q30_ONE - cos_h * cos_h));
    return astro_atan2(sin_h, cos_h);
}

static int32_t sidereal_minutes(int32_t hour_angle)
{
    return (int32_t)((int64_t)hour_angle * SIDEREAL_MINUTES_PER_TURN / 100 / ASTRO_FULL_CIRCLE);
}

void astro_altaz(const astro_site *site, int64_t seconds, uint32_t ra, int32_t dec,
                 astro_horizontal *out)
{
    uint32_t ra_date;
    int32_t dec_date;
    int32_t sin_lat, cos_lat, sin_dec, cos_dec, sin_ha, cos_ha;

    astro_precess_to_date(seconds, ra, dec, &ra_date, &dec_date);
    int32_t hour_angle =
        wrap_signed((int64_t)astro_local_sidereal_time(site, seconds) - (int64_t)ra_date);

    astro_sincos(site->latitude, &sin_lat, &cos_lat);
    astro_sincos(dec_date, &sin_dec, &cos_dec);
    astro_sincos(hour_angle, &sin_ha, &cos_ha);

    int64_t sin_alt = mul_q30(sin_lat, sin_dec) + mul_q30(mul_q30(cos_lat, cos_dec), cos_ha);
    int64_t east = -mul_q30(cos_dec, sin_ha);
    int64_t north = mul_q30(sin_dec, cos_lat) - mul_q30(mul_q30(cos_dec, sin_lat), cos_ha);
    int64_t horizontal = astro_isqrt64((uint64_t)(east * east + north * north));
    int32_t azimuth = astro_atan2(east, north);

    out->altitude = astro_atan2(sin_alt, horizontal);
    out->azimuth = (uint32_t)(azimuth < 0 ? azimuth + ASTRO_FULL_CIRCLE : azimuth);
    out->minutes_to_change = 0;

    int32_t crossing = horizon_hour_angle(site->latitude, dec_date);
    if (crossing == -1) {
        out->visibility = ASTRO_SKY_CIRCUMPOLAR;
    } else if (crossing == -2) {
        out->visibility = ASTRO_SKY_NEVER_RISES;
    } else if (hour_angle > -crossing && hour_angle < crossing) {
        out->visibility = ASTRO_SKY_UP; /* sets when the hour angle reaches +crossing */
        out->minutes_to_change = sidereal_minutes(crossing - hour_angle);
    } else {
        out->visibility = ASTRO_SKY_DOWN; /* rises at hour angle -crossing */
        int32_t until_rise = wrap_signed((int64_t)-crossing - hour_angle);
        if (until_rise < 0) {
            until_rise += ASTRO_FULL_CIRCLE;
        }
        out->minutes_to_change = sidereal_minutes(until_rise);
    }
}
