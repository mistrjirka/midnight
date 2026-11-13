/* SPDX-License-Identifier: GPL-2.0-only */
/* Minimal, bounds-checked EXIF reader: capture time and GPS fix from a JPEG or CR2 header. */
#ifndef ASTRO_EXIF_H
#define ASTRO_EXIF_H

#include <stdint.h>
#include "astro_time.h"

/* Callers pass the first bytes of the file; all EXIF/GPS data of Canon files lies here. */
#define ASTRO_EXIF_HEAD_BYTES 65536

typedef struct {
    int has_capture_time;
    astro_datetime capture_local; /* DateTimeOriginal: camera clock, local time */
    int has_gps;
    int32_t latitude;  /* 1e-4 degree, north positive */
    int32_t longitude; /* 1e-4 degree, east positive */
    int32_t altitude_m;
    int has_gps_time;
    astro_datetime gps_utc; /* GPSDateStamp + GPSTimeStamp */
} astro_exif;

/* Returns 1 if the buffer is a JPEG or TIFF/CR2 with an EXIF block (fields that are absent
 * are flagged in out), 0 otherwise. Never reads outside [data, data + size). */
int astro_exif_parse(const uint8_t *data, uint32_t size, astro_exif *out);

#endif
