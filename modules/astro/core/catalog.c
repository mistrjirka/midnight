/* SPDX-License-Identifier: GPL-2.0-only */
/* DSO designations, binary search in DSO.KEY, common names from DSO.NAM. */
#include <string.h>
#include "catalog.h"
#include "fixed_math.h"

#define MAX_RECORDS     200000
#define KEY_RECORD      8
#define FILE_VERSION    1
#define DSO_HEADER_SIZE 24
#define NAME_HEADER     16

const uint16_t astro_catalogue_max[ASTRO_CAT_COUNT + 1] = {0, 110, 7840, 5386, 109, 370, 313};

void astro_designation(int catalogue, int number, char *out, int size)
{
    static const char *const prefixes[] = {"?", "M", "NGC ", "IC ", "C ", "B ", "Sh2-"};
    const char *prefix =
        (catalogue >= 1 && catalogue <= ASTRO_CAT_COUNT) ? prefixes[catalogue] : prefixes[0];
    char digits[8];
    int ndigits = 0;
    unsigned value = (unsigned)number;
    int i = 0;

    if (size < 1) {
        return;
    }
    do {
        digits[ndigits++] = (char)('0' + value % 10);
        value /= 10;
    } while (value && ndigits < 7);

    while (*prefix && i < size - 1) {
        out[i++] = *prefix++;
    }
    while (ndigits && i < size - 1) {
        out[i++] = digits[--ndigits];
    }
    out[i] = 0;
}

const char *astro_dso_type_name(int type)
{
    static const char *const names[] = {
        "object",         "galaxy",         "open cluster",    "globular",
        "planetary neb.", "nebula",         "reflection neb.", "dark nebula",
        "SN remnant",     "cluster+nebula", "asterism",        "star",
    };
    return (type >= 0 && type <= ASTRO_DSO_STAR) ? names[type] : names[0];
}

void astro_dso_decode(const uint8_t *p, astro_dso *out)
{
    out->ra = astro_get_u32(p);
    out->dec = (int32_t)astro_get_u32(p + 4);
    out->major_axis = astro_get_u16(p + 8);
    out->minor_axis = astro_get_u16(p + 10);
    out->position_angle = (int16_t)astro_get_u16(p + 12);
    out->mag10 = (int16_t)astro_get_u16(p + 14);
    out->type = p[16];
    out->catalogue = p[17];
    out->number = astro_get_u16(p + 18);
    out->name = astro_get_u16(p + 20);
    out->reserved = 0;
}

int astro_dso_count(const astro_sky_files *files)
{
    uint8_t h[DSO_HEADER_SIZE];

    if (!astro_file_read(&files->dso, 0, h, sizeof(h)) || memcmp(h, "SDSO", 4) ||
        astro_get_u32(h + 4) != FILE_VERSION || astro_get_u32(h + 12) != ASTRO_DSO_RECORD_BYTES ||
        astro_get_u32(h + 16) != ASTRO_TILE_RA_BINS ||
        astro_get_u32(h + 20) != ASTRO_TILE_DEC_BINS || astro_get_u32(h + 8) > MAX_RECORDS) {
        return -1;
    }
    return (int)astro_get_u32(h + 8);
}

static int read_dso_record(const astro_sky_files *files, uint32_t record, astro_dso *out)
{
    uint8_t raw[ASTRO_DSO_RECORD_BYTES];

    if (!astro_file_read(&files->dso, ASTRO_DSO_HEADER_BYTES + record * ASTRO_DSO_RECORD_BYTES, raw,
                         sizeof(raw))) {
        return 0;
    }
    astro_dso_decode(raw, out);
    return out->ra < ASTRO_FULL_CIRCLE && out->dec >= -ASTRO_QUARTER_CIRCLE &&
           out->dec <= ASTRO_QUARTER_CIRCLE;
}

/* DSO.KEY: sorted (catalogue << 16 | number) -> record index. At most ~15 reads. */
int astro_dso_lookup(const astro_sky_files *files, int catalogue, int number, astro_target *out)
{
    uint8_t h[NAME_HEADER];
    uint8_t k[KEY_RECORD];

    memset(out, 0, sizeof(*out));
    int count = astro_dso_count(files);
    if (count < 0 || !astro_file_read(&files->dso_key, 0, h, sizeof(h)) || memcmp(h, "SKEY", 4) ||
        astro_get_u32(h + 4) != FILE_VERSION || astro_get_u32(h + 12) != KEY_RECORD ||
        astro_get_u32(h + 8) > MAX_RECORDS) {
        return -1;
    }

    uint32_t wanted = (uint32_t)catalogue << 16 | (uint16_t)number;
    uint32_t lo = 0;
    uint32_t hi = astro_get_u32(h + 8);
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (!astro_file_read(&files->dso_key, NAME_HEADER + mid * KEY_RECORD, k, sizeof(k))) {
            return -1;
        }
        uint32_t key = (uint32_t)k[0] << 16 | astro_get_u16(k + 2);
        if (key < wanted) {
            lo = mid + 1;
        } else if (key > wanted) {
            hi = mid;
        } else {
            uint32_t record = astro_get_u32(k + 4);
            if (record >= (uint32_t)count || !read_dso_record(files, record, &out->object)) {
                return -1;
            }
            astro_designation(catalogue, number, out->label, sizeof(out->label));
            out->valid = 1;
            return 1;
        }
    }
    return 0;
}

void astro_dso_common_name(const astro_sky_files *files, const astro_dso *object, char *out,
                           int size)
{
    uint8_t h[NAME_HEADER];
    char name[ASTRO_NAME_BYTES];

    if (size < 1) {
        return;
    }
    out[0] = 0;
    if (!object->name || !astro_file_read(&files->dso_nam, 0, h, sizeof(h)) ||
        memcmp(h, "SNAM", 4) || astro_get_u32(h + 12) != ASTRO_NAME_BYTES ||
        object->name >= astro_get_u32(h + 8) ||
        !astro_file_read(&files->dso_nam, NAME_HEADER + (uint32_t)object->name * ASTRO_NAME_BYTES,
                         name, sizeof(name))) {
        return;
    }
    name[ASTRO_NAME_BYTES - 1] = 0;
    strncpy(out, name, (size_t)size - 1);
    out[size - 1] = 0;
}
