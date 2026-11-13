/* SPDX-License-Identifier: GPL-2.0-only */
/* Blind plate solver: four-star pattern lookup in an SD-card index, then verification. */
#ifndef ASTRO_SKY_SOLVER_H
#define ASTRO_SKY_SOLVER_H

#include <stdint.h>
#include "star.h"
#include "wcs.h"

#define ASTRO_SKY_VERIFY_STARS  96
#define ASTRO_SKY_FIELD_BYTES   (16 + ASTRO_SKY_VERIFY_STARS * 8)
#define ASTRO_SKY_PATTERN_BYTES 16
#define ASTRO_SKY_FILE_HEADER   16

/* Accepted image scale, Q16 pixels per millidegree. For a 360 px wide image:
 * 500 is a ~47 degree field, 30000 is ~0.8 degree. */
#define ASTRO_SKY_SCALE_MIN 140
#define ASTRO_SKY_SCALE_MAX 30000

/* SKY.DIR entry: pattern key and index of its first record in SKY.PAT. */
typedef struct {
    uint32_t key;
    uint32_t offset;
} astro_sky_dir_entry;

typedef struct {
    uint32_t ra; /* image centre, 1e-4 degree */
    int32_t dec;
    uint32_t field;   /* index field that verified */
    uint32_t quads;   /* image quads tried */
    uint32_t reads;   /* SD reads */
    uint32_t checked; /* hypotheses verified (Bonferroni factor) */
    int matched;      /* verified star matches */
    int mse100;       /* mean squared residual, px^2 * 100 */
    int fov_cdeg;
    uint32_t matched_mask;           /* bit i: observed star i matched */
    uint16_t names[ASTRO_MAX_STARS]; /* STARS.NAM label per observed star, 0 = none */
    astro_wcs wcs;                   /* sky -> image transform of the solution */
} astro_sky_solution;

/* scale_min == 0: full supported range. A focal-length hint narrows it. */
typedef struct {
    uint32_t scale_min;
    uint32_t scale_max;
} astro_sky_options;

/* Reads `bytes` at `offset` into dest. Returns nonzero on success. */
typedef int (*astro_sky_read)(void *context, uint32_t offset, void *dest, uint32_t bytes);

/* Called periodically. Return zero to cancel the solve. */
typedef int (*astro_sky_progress)(void *context, unsigned quads, unsigned checked);

typedef struct {
    const astro_sky_dir_entry *dir; /* nkeys + 1 entries; the last is the end sentinel */
    unsigned nkeys;
    unsigned npatterns;
    unsigned nfields;
    astro_sky_read read_patterns;
    void *patterns_context;
    astro_sky_read read_fields;
    void *fields_context;
} astro_sky_index;

typedef enum {
    ASTRO_SKY_CANCELLED = -2,
    ASTRO_SKY_BAD_INDEX = -1,
    ASTRO_SKY_NO_MATCH = 0,
    ASTRO_SKY_SOLVED = 1,
    ASTRO_SKY_BUDGET_EXHAUSTED = 2, /* quad limit reached without a match */
} astro_sky_status;

typedef struct astro_sky_workspace astro_sky_workspace;

/* About 24 KB of scratch; allocate it per solve. */
unsigned astro_sky_workspace_size(void);

astro_sky_status astro_sky_solve(const astro_star *stars, int count, int width, int height,
                                 const astro_sky_index *index, const astro_sky_options *options,
                                 astro_sky_progress progress, void *progress_context,
                                 astro_sky_workspace *workspace, astro_sky_solution *out);

#endif
