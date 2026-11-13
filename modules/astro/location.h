/* SPDX-License-Identifier: GPL-2.0-only */
/* Observer location from the newest photo's EXIF GPS fix, and the current UTC time. */
#ifndef ASTRO_LOCATION_H
#define ASTRO_LOCATION_H

#include <stdint.h>
#include "core/altaz.h"

/* A stored fix this old gets a warning: the camera may have moved since. */
#define ASTRO_LOCATION_STALE_HOURS 12

/* Worker: reads the newest JPG/CR2 in the current DCIM folder and stores its GPS fix. */
void astro_location_read_last_photo(void);

/* Returns 0 if no location has been stored yet. */
int astro_location_site(astro_site *site);

/* Camera clock converted to UTC with the offset learned from the GPS photo. */
int64_t astro_now_utc(void);

/* "IMG_1234 at 2026-11-09 21:14, 3 h 12 min ago" (or "none: take a photo with GPS on"). */
void astro_location_source(char *out, int size);

/* Age of the stored fix in minutes (negative if unknown). */
int astro_location_age_minutes(void);

#endif
