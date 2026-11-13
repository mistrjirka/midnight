/* SPDX-License-Identifier: GPL-2.0-only */
/* Civil calendar <-> seconds, GMST and IAU 1976 precession (Lieske 1977). */
#include "astro_time.h"
#include "fixed_math.h"
#include "sphere.h"

#define SECONDS_PER_DAY     86400
#define DAYS_1970_TO_J2000  10957 /* 2000-01-01 */
#define J2000_NOON_SECONDS  (SECONDS_PER_DAY / 2)
#define SECONDS_PER_CENTURY 3155760000LL /* 36525 days */

/* GMST = 280.46061837 deg + 360.98564736629 deg/day * d (IAU 1982, T^2 term < 0.001 deg
 * this century and omitted). */
#define GMST_AT_J2000_1E4DEG     2804606LL
#define GMST_DAY_RATE_1E11DEG    36098564736629LL /* 1e-4 degree * 1e7 per day */
#define GMST_SECOND_RATE_1E12DEG 4178074622LL     /* 1e-4 degree * 1e8 per second */

/* Precession angles in 1e-4 degree per Julian century (linear + quadratic terms):
 * zeta = 2306.2181" T + 0.30188" T^2, z = 2306.2181" T + 1.09468" T^2,
 * theta = 2004.3109" T - 0.42665" T^2. Arcseconds * 10000 / 3600. */
#define ZETA_T_1E4DEG  6406161LL /* 1e-3 * 1e-4 degree per century */
#define ZETA_T2        839LL     /* 1e-3 * 1e-4 degree per century^2 */
#define Z_T2           3041LL
#define THETA_T_1E4DEG 5567530LL
#define THETA_T2       (-1185LL)

/* Howard Hinnant's days_from_civil: days since 1970-01-01, proleptic Gregorian. */
static int64_t days_from_civil(int64_t y, int m, int d)
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void civil_from_days(int64_t z, astro_datetime *out)
{
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    out->day = (int)(doy - (153 * mp + 2) / 5 + 1);
    out->month = (int)(mp < 10 ? mp + 3 : mp - 9);
    out->year = (int)(yoe + era * 400 + (out->month <= 2));
}

int64_t astro_seconds_since_j2000(const astro_datetime *t)
{
    int64_t days = days_from_civil(t->year, t->month, t->day) - DAYS_1970_TO_J2000;
    return days * SECONDS_PER_DAY + t->hour * 3600 + t->minute * 60 + t->second -
           J2000_NOON_SECONDS;
}

void astro_datetime_from_seconds(int64_t seconds, astro_datetime *out)
{
    int64_t since_midnight = seconds + J2000_NOON_SECONDS;
    int64_t days = since_midnight / SECONDS_PER_DAY;
    int64_t rest = since_midnight % SECONDS_PER_DAY;

    if (rest < 0) {
        rest += SECONDS_PER_DAY;
        days--;
    }
    civil_from_days(days + DAYS_1970_TO_J2000, out);
    out->hour = (int)(rest / 3600);
    out->minute = (int)(rest / 60 % 60);
    out->second = (int)(rest % 60);
}

static int parse_digits(const char *text, int count, int *value)
{
    *value = 0;
    for (int i = 0; i < count; i++) {
        if (text[i] < '0' || text[i] > '9') {
            return 0;
        }
        *value = *value * 10 + (text[i] - '0');
    }
    return 1;
}

int astro_parse_exif_datetime(const char *s, astro_datetime *out)
{
    static const int days_in_month[12] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    if (s[4] != ':' || s[7] != ':' || s[10] != ' ' || s[13] != ':' || s[16] != ':') {
        return 0;
    }
    if (!parse_digits(s, 4, &out->year) || !parse_digits(s + 5, 2, &out->month) ||
        !parse_digits(s + 8, 2, &out->day) || !parse_digits(s + 11, 2, &out->hour) ||
        !parse_digits(s + 14, 2, &out->minute) || !parse_digits(s + 17, 2, &out->second)) {
        return 0;
    }
    return out->year >= 2000 && out->month >= 1 && out->month <= 12 && out->day >= 1 &&
           out->day <= days_in_month[out->month - 1] && out->hour < 24 && out->minute < 60 &&
           out->second < 61;
}

uint32_t astro_gmst(int64_t seconds)
{
    int64_t days = seconds / SECONDS_PER_DAY;
    int64_t rest = seconds % SECONDS_PER_DAY;
    int64_t angle = GMST_AT_J2000_1E4DEG + (days * GMST_DAY_RATE_1E11DEG) / 10000000LL +
                    (rest * GMST_SECOND_RATE_1E12DEG) / 100000000LL;

    angle %= ASTRO_FULL_CIRCLE;
    if (angle < 0) {
        angle += ASTRO_FULL_CIRCLE;
    }
    return (uint32_t)angle;
}

/* Precession angles in 1e-4 degree for the given epoch. */
static void precession_angles(int64_t seconds, int32_t *zeta, int32_t *z, int32_t *theta)
{
    /* T in units of 1e-6 century keeps the quadratic terms inside int64. */
    int64_t t_micro = seconds * 1000000LL / SECONDS_PER_CENTURY;
    int64_t t2_micro = t_micro * t_micro / 1000000LL;

    *zeta = (int32_t)((ZETA_T_1E4DEG * t_micro + ZETA_T2 * t2_micro) / 1000000000LL);
    *z = (int32_t)((ZETA_T_1E4DEG * t_micro + Z_T2 * t2_micro) / 1000000000LL);
    *theta = (int32_t)((THETA_T_1E4DEG * t_micro + THETA_T2 * t2_micro) / 1000000000LL);
}

/* Passive rotations: P = R3(-z) R2(theta) R3(-zeta). */
void astro_precess_to_date(int64_t seconds, uint32_t ra, int32_t dec, uint32_t *ra_date,
                           int32_t *dec_date)
{
    int32_t zeta, z, theta;
    precession_angles(seconds, &zeta, &z, &theta);

    astro_vec3 v = astro_vec_from_radec(ra, dec);
    v = astro_vec_rotate(v, 3, -zeta);
    v = astro_vec_rotate(v, 2, theta);
    v = astro_vec_rotate(v, 3, -z);
    astro_vec_to_radec(v, ra_date, dec_date);
}

void astro_precess_to_j2000(int64_t seconds, uint32_t ra_date, int32_t dec_date, uint32_t *ra,
                            int32_t *dec)
{
    int32_t zeta, z, theta;
    precession_angles(seconds, &zeta, &z, &theta);

    astro_vec3 v = astro_vec_from_radec(ra_date, dec_date);
    v = astro_vec_rotate(v, 3, z);
    v = astro_vec_rotate(v, 2, -theta);
    v = astro_vec_rotate(v, 3, zeta);
    astro_vec_to_radec(v, ra, dec);
}

void astro_pole_of_date(int64_t seconds, int north, uint32_t *ra, int32_t *dec)
{
    astro_precess_to_j2000(seconds, 0, north ? ASTRO_QUARTER_CIRCLE : -ASTRO_QUARTER_CIRCLE, ra,
                           dec);
}
