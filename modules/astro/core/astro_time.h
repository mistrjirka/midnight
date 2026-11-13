/* SPDX-License-Identifier: GPL-2.0-only */
/* Calendar, sidereal time and precession in integer arithmetic. */
#ifndef ASTRO_TIME_H
#define ASTRO_TIME_H

#include <stdint.h>

typedef struct {
    int year;  /* e.g. 2026 */
    int month; /* 1..12 */
    int day;   /* 1..31 */
    int hour;
    int minute;
    int second;
} astro_datetime;

/* Seconds since J2000.0 (2000-01-01 12:00:00 UTC; UT1 - UTC and TT - UTC are ignored,
 * which is far below the precision needed here). */
int64_t astro_seconds_since_j2000(const astro_datetime *utc);

/* Inverse of astro_seconds_since_j2000. */
void astro_datetime_from_seconds(int64_t seconds, astro_datetime *utc);

/* Strict "YYYY:MM:DD HH:MM:SS" (EXIF) parser. Returns 0 on any malformed field. */
int astro_parse_exif_datetime(const char *text, astro_datetime *out);

/* Greenwich mean sidereal time, 1e-4 degree in [0, 360). */
uint32_t astro_gmst(int64_t seconds_since_j2000);

/* IAU 1976 precession of a J2000 position to the mean equator of date, and back. */
void astro_precess_to_date(int64_t seconds_since_j2000, uint32_t ra, int32_t dec, uint32_t *ra_date,
                           int32_t *dec_date);
void astro_precess_to_j2000(int64_t seconds_since_j2000, uint32_t ra_date, int32_t dec_date,
                            uint32_t *ra, int32_t *dec);

/* The celestial pole of date (north if north != 0), in J2000 coordinates. */
void astro_pole_of_date(int64_t seconds_since_j2000, int north, uint32_t *ra, int32_t *dec);

#endif
