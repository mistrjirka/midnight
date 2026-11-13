/* SPDX-License-Identifier: GPL-2.0-only */
/* Horizontal coordinates and rise/set state for an observer. */
#ifndef ASTRO_ALTAZ_H
#define ASTRO_ALTAZ_H

#include <stdint.h>

typedef struct {
    int32_t latitude;  /* 1e-4 degree, north positive */
    int32_t longitude; /* 1e-4 degree, east positive */
} astro_site;

typedef enum {
    ASTRO_SKY_UP = 0,      /* above the horizon, sets later */
    ASTRO_SKY_DOWN,        /* below the horizon, rises later */
    ASTRO_SKY_CIRCUMPOLAR, /* never sets */
    ASTRO_SKY_NEVER_RISES
} astro_visibility;

typedef struct {
    int32_t altitude; /* 1e-4 degree, geometric (no refraction) */
    uint32_t azimuth; /* 1e-4 degree from north through east */
    astro_visibility visibility;
    int32_t minutes_to_change; /* until it sets (UP) or rises (DOWN); 0 otherwise */
} astro_horizontal;

/* J2000 position -> horizontal coordinates at the given UTC time. */
void astro_altaz(const astro_site *site, int64_t seconds_since_j2000, uint32_t ra, int32_t dec,
                 astro_horizontal *out);

/* Local sidereal time, 1e-4 degree. */
uint32_t astro_local_sidereal_time(const astro_site *site, int64_t seconds_since_j2000);

#endif
