/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * TIFF IFD walker for EXIF. JPEG: SOI, then segments until APP1 "Exif\0\0" holds a TIFF
 * block. CR2 is a TIFF file itself. Only IFD0, the EXIF sub-IFD and the GPS sub-IFD are read.
 */
#include <string.h>
#include "exif.h"

#define TIFF_HEADER       8
#define IFD_ENTRY         12
#define MAX_IFD_ENTRIES   512
#define TAG_DATETIME      0x0132
#define TAG_EXIF_IFD      0x8769
#define TAG_GPS_IFD       0x8825
#define TAG_DATETIME_ORIG 0x9003
#define TAG_GPS_LAT_REF   0x0001
#define TAG_GPS_LAT       0x0002
#define TAG_GPS_LON_REF   0x0003
#define TAG_GPS_LON       0x0004
#define TAG_GPS_ALT_REF   0x0005
#define TAG_GPS_ALT       0x0006
#define TAG_GPS_TIME      0x0007
#define TAG_GPS_DATE      0x001D
#define TYPE_ASCII        2
#define TYPE_RATIONAL     5
#define EXIF_TIME_CHARS   19

typedef struct {
    const uint8_t *base; /* start of the TIFF header */
    uint32_t size;
    int big_endian;
} tiff;

static uint16_t get16(const tiff *t, uint32_t at)
{
    const uint8_t *p = t->base + at;
    return t->big_endian ? (uint16_t)(p[0] << 8 | p[1]) : (uint16_t)(p[1] << 8 | p[0]);
}

static uint32_t get32(const tiff *t, uint32_t at)
{
    const uint8_t *p = t->base + at;
    if (t->big_endian) {
        return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
    }
    return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
}

static int in_bounds(const tiff *t, uint32_t at, uint32_t bytes)
{
    return at <= t->size && bytes <= t->size - at;
}

typedef struct {
    uint16_t tag;
    uint16_t type;
    uint32_t count;
    uint32_t value_offset; /* where the value bytes are (inline values: the entry itself) */
} ifd_entry;

static uint32_t type_size(uint16_t type)
{
    static const uint8_t sizes[] = {0, 1, 1, 2, 4, 8, 1, 1, 2, 4, 8, 4, 8};
    return type < sizeof(sizes) ? sizes[type] : 0;
}

/* Finds tag in the IFD at `ifd`. Returns 1 and fills entry when present and valid. */
static int find_tag(const tiff *t, uint32_t ifd, uint16_t tag, ifd_entry *entry)
{
    if (!in_bounds(t, ifd, 2)) {
        return 0;
    }
    uint16_t count = get16(t, ifd);
    if (count > MAX_IFD_ENTRIES || !in_bounds(t, ifd + 2, (uint32_t)count * IFD_ENTRY)) {
        return 0;
    }
    for (uint16_t i = 0; i < count; i++) {
        uint32_t at = ifd + 2 + (uint32_t)i * IFD_ENTRY;
        if (get16(t, at) != tag) {
            continue;
        }
        entry->tag = tag;
        entry->type = get16(t, at + 2);
        entry->count = get32(t, at + 4);
        uint32_t size = type_size(entry->type);
        if (!size || entry->count > t->size / size) {
            return 0;
        }
        entry->value_offset = size * entry->count <= 4 ? at + 8 : get32(t, at + 8);
        return in_bounds(t, entry->value_offset, size * entry->count);
    }
    return 0;
}

static int read_ascii(const tiff *t, uint32_t ifd, uint16_t tag, char *out, uint32_t length)
{
    ifd_entry e;
    if (!find_tag(t, ifd, tag, &e) || e.type != TYPE_ASCII || e.count < length) {
        return 0;
    }
    memcpy(out, t->base + e.value_offset, length);
    out[length] = 0;
    return 1;
}

/* Three rationals (degrees, minutes, seconds or h, m, s) as value * scale. */
static int read_triplet(const tiff *t, uint32_t ifd, uint16_t tag, int64_t scale,
                        int64_t parts_out[3])
{
    ifd_entry e;
    if (!find_tag(t, ifd, tag, &e) || e.type != TYPE_RATIONAL || e.count != 3) {
        return 0;
    }
    for (int i = 0; i < 3; i++) {
        uint32_t num = get32(t, e.value_offset + 8 * (uint32_t)i);
        uint32_t den = get32(t, e.value_offset + 8 * (uint32_t)i + 4);
        if (!den) {
            return 0;
        }
        parts_out[i] = (int64_t)num * scale / den;
    }
    return 1;
}

/* Degrees/minutes/seconds rationals plus an 'N'/'S' or 'E'/'W' reference -> 1e-4 degree. */
static int read_coordinate(const tiff *t, uint32_t gps, uint16_t ref_tag, uint16_t tag,
                           char negative_ref, int32_t limit, int32_t *out)
{
    char ref[2];
    int64_t dms[3];
    if (!read_ascii(t, gps, ref_tag, ref, 1) || !read_triplet(t, gps, tag, 10000, dms)) {
        return 0;
    }
    int64_t value = dms[0] + dms[1] / 60 + dms[2] / 3600;
    if (value > limit) {
        return 0;
    }
    *out = (int32_t)(ref[0] == negative_ref ? -value : value);
    return 1;
}

static void read_gps(const tiff *t, uint32_t gps, astro_exif *out)
{
    ifd_entry alt;
    char date[11];
    int64_t hms[3];

    out->has_gps =
        read_coordinate(t, gps, TAG_GPS_LAT_REF, TAG_GPS_LAT, 'S', 900000, &out->latitude) &&
        read_coordinate(t, gps, TAG_GPS_LON_REF, TAG_GPS_LON, 'W', 1800000, &out->longitude);
    if (find_tag(t, gps, TAG_GPS_ALT, &alt) && alt.type == TYPE_RATIONAL && alt.count == 1) {
        uint32_t den = get32(t, alt.value_offset + 4);
        ifd_entry ref;
        int below = find_tag(t, gps, TAG_GPS_ALT_REF, &ref) && t->base[ref.value_offset] == 1;
        if (den) {
            int32_t metres = (int32_t)(get32(t, alt.value_offset) / den);
            out->altitude_m = below ? -metres : metres;
        }
    }
    if (read_ascii(t, gps, TAG_GPS_DATE, date, 10) && read_triplet(t, gps, TAG_GPS_TIME, 1, hms)) {
        char text[EXIF_TIME_CHARS + 1];
        memcpy(text, date, 10);
        text[10] = ' ';
        text[11] = (char)('0' + hms[0] / 10 % 10);
        text[12] = (char)('0' + hms[0] % 10);
        text[13] = ':';
        text[14] = (char)('0' + hms[1] / 10 % 10);
        text[15] = (char)('0' + hms[1] % 10);
        text[16] = ':';
        text[17] = (char)('0' + hms[2] / 10 % 10);
        text[18] = (char)('0' + hms[2] % 10);
        text[19] = 0;
        out->has_gps_time = hms[0] < 24 && hms[1] < 60 && hms[2] < 61 &&
                            astro_parse_exif_datetime(text, &out->gps_utc);
    }
}

static int parse_tiff(const uint8_t *data, uint32_t size, astro_exif *out)
{
    tiff t = {data, size, 0};
    char when[EXIF_TIME_CHARS + 1];
    ifd_entry e;

    if (size < TIFF_HEADER) {
        return 0;
    }
    if (!memcmp(data, "MM\0*", 4)) {
        t.big_endian = 1;
    } else if (memcmp(data, "II*\0", 4)) {
        return 0;
    }
    uint32_t ifd0 = get32(&t, 4);
    if (find_tag(&t, ifd0, TAG_EXIF_IFD, &e) && e.count == 1) {
        uint32_t exif_ifd = get32(&t, e.value_offset);
        if (read_ascii(&t, exif_ifd, TAG_DATETIME_ORIG, when, EXIF_TIME_CHARS)) {
            out->has_capture_time = astro_parse_exif_datetime(when, &out->capture_local);
        }
    }
    if (!out->has_capture_time && read_ascii(&t, ifd0, TAG_DATETIME, when, EXIF_TIME_CHARS)) {
        out->has_capture_time = astro_parse_exif_datetime(when, &out->capture_local);
    }
    if (find_tag(&t, ifd0, TAG_GPS_IFD, &e) && e.count == 1) {
        read_gps(&t, get32(&t, e.value_offset), out);
    }
    return 1;
}

/* Walks JPEG markers up to the first APP1 Exif segment. */
static int parse_jpeg(const uint8_t *data, uint32_t size, astro_exif *out)
{
    uint32_t at = 2;

    while (at + 4 <= size && data[at] == 0xFF) {
        uint8_t marker = data[at + 1];
        uint32_t length = (uint32_t)data[at + 2] << 8 | data[at + 3];
        if (marker == 0xDA || marker == 0xD9 || length < 2 || length > size - at - 2) {
            return 0; /* image data or end: no EXIF before it */
        }
        if (marker == 0xE1 && length >= 8 && !memcmp(data + at + 4, "Exif\0\0", 6)) {
            return parse_tiff(data + at + 10, length - 8, out);
        }
        at += 2 + length;
    }
    return 0;
}

int astro_exif_parse(const uint8_t *data, uint32_t size, astro_exif *out)
{
    memset(out, 0, sizeof(*out));
    if (!data || size < 4) {
        return 0;
    }
    if (data[0] == 0xFF && data[1] == 0xD8) {
        return parse_jpeg(data, size, out);
    }
    return parse_tiff(data, size, out);
}
