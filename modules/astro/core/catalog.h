/* SPDX-License-Identifier: GPL-2.0-only */
/* Deep-sky catalogue lookups (DSO.BIN/.KEY/.NAM) and on-card data file set. */
#ifndef ASTRO_CATALOG_H
#define ASTRO_CATALOG_H

#include <stdint.h>
#include "reader.h"

/* Catalogue IDs, shared with tools/data/dso.py. Also the picker order. */
enum {
    ASTRO_CAT_M = 1,
    ASTRO_CAT_NGC,
    ASTRO_CAT_IC,
    ASTRO_CAT_C,
    ASTRO_CAT_B,
    ASTRO_CAT_SH2,
    ASTRO_CAT_COUNT = ASTRO_CAT_SH2
};

/* Highest designation number per catalogue (index 0 unused). */
extern const uint16_t astro_catalogue_max[ASTRO_CAT_COUNT + 1];

/* Object types, shared with tools/data/dso.py. */
enum {
    ASTRO_DSO_GALAXY = 1,
    ASTRO_DSO_OPEN,
    ASTRO_DSO_GLOBULAR,
    ASTRO_DSO_PLANETARY,
    ASTRO_DSO_NEBULA,
    ASTRO_DSO_REFLECTION,
    ASTRO_DSO_DARK,
    ASTRO_DSO_SNR,
    ASTRO_DSO_CLUSTER_NEBULA,
    ASTRO_DSO_ASTERISM,
    ASTRO_DSO_STAR
};

#define ASTRO_MAG_UNKNOWN 32767

/* Sky index tiles shared by STARS.IDX and DSO.BIN: 5 x 5 degree cells. */
#define ASTRO_TILE_RA_BINS  72
#define ASTRO_TILE_DEC_BINS 36
#define ASTRO_TILES         (ASTRO_TILE_RA_BINS * ASTRO_TILE_DEC_BINS)
#define ASTRO_TILE_SIZE     50000 /* 1e-4 degree */

#define ASTRO_DSO_RECORD_BYTES 24
#define ASTRO_DSO_HEADER_BYTES (24 + (ASTRO_TILES + 1) * 4)
#define ASTRO_NAME_BYTES       24

typedef struct {
    astro_file stars, stars_idx, stars_nam; /* STARS.BIN / .IDX / .NAM */
    astro_file dso, dso_key, dso_nam;       /* DSO.BIN / .KEY / .NAM */
    astro_file constellations;              /* CONST.BIN */
} astro_sky_files;

/* One DSO.BIN record. */
typedef struct {
    uint32_t ra; /* 1e-4 degree */
    int32_t dec;
    uint16_t major_axis; /* 0.1 arcmin */
    uint16_t minor_axis;
    int16_t position_angle; /* degrees, -1 unknown */
    int16_t mag10;
    uint8_t type;
    uint8_t catalogue; /* best designation */
    uint16_t number;
    uint16_t name; /* DSO.NAM index, 0 = none */
    uint16_t reserved;
} astro_dso;

typedef struct {
    int valid;
    astro_dso object;
    char label[ASTRO_NAME_BYTES]; /* "M42", "NGC 7000", "Sh2-155" */
} astro_target;

/* "M42", "NGC 7000", ... into out (always terminated). */
void astro_designation(int catalogue, int number, char *out, int size);

const char *astro_dso_type_name(int type);

/* Returns 1 found, 0 not in the catalogue, -1 missing or corrupt files. */
int astro_dso_lookup(const astro_sky_files *files, int catalogue, int number, astro_target *out);

/* Common name ("Orion Nebula"), or an empty string. */
void astro_dso_common_name(const astro_sky_files *files, const astro_dso *object, char *out,
                           int size);

/* Shared with scene.c. */
void astro_dso_decode(const uint8_t *record, astro_dso *out);
int astro_dso_count(const astro_sky_files *files); /* -1 on a bad header */

#endif
