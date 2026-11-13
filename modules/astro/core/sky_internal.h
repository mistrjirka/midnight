/* SPDX-License-Identifier: GPL-2.0-only */
/* Shared internals of sky_pattern.c, sky_verify.c and sky_solver.c. Not a public API. */
#ifndef ASTRO_SKY_INTERNAL_H
#define ASTRO_SKY_INTERNAL_H

#include "sky_solver.h"

/* Quad descriptor grid: each of the 4 coordinates is quantised to 0..24. */
#define SKY_CODE_STEPS     24
#define SKY_INVALID_CODE   0xffffffffu
#define SKY_MAX_KEYS       81 /* 3^4 neighbouring cells */
#define SKY_PATTERN_STARS  10 /* index patterns use the 10 brightest field stars */
#define SKY_PATTERN_CHUNK  256
#define SKY_VERIFY_BATCH   1024
#define SKY_MAX_FINE_ERROR 9

/* SKY.PAT record (16 bytes, little endian). */
typedef struct {
    uint32_t key;
    uint32_t fine; /* 4 x 8-bit fine descriptor */
    uint32_t field;
    uint8_t a; /* field star indices of the baseline */
    uint8_t b;
    uint8_t flip;
    uint8_t pad;
} sky_pattern;

typedef struct {
    int16_t x; /* tangent-plane position in field units */
    int16_t y;
    uint16_t name;
    uint16_t mag;
} sky_field_star;

/* SKY.FLD record: one shared verification field. */
typedef struct {
    uint32_t ra;
    int32_t dec;
    uint16_t count;
    uint16_t radius;
    uint32_t unit_mdeg; /* millidegrees per coordinate unit (wide index); 0 means 1 */
    sky_field_star stars[ASTRO_SKY_VERIFY_STARS];
} sky_field;

typedef char sky_assert_pattern_size[(sizeof(sky_pattern) == ASTRO_SKY_PATTERN_BYTES) ? 1 : -1];
typedef char sky_assert_field_size[(sizeof(sky_field) == ASTRO_SKY_FIELD_BYTES) ? 1 : -1];

/* Position of a field star that is outside the frame. */
#define SKY_OFF_FRAME (-30000)

/* Quad as seen in the image: canonical code, fine descriptor and the roles of its stars. */
typedef struct {
    uint32_t code;
    uint32_t fine;
    int baseline_a; /* index 0..3 into the quad */
    int baseline_b;
    int flip;
} sky_quad;

struct astro_sky_workspace {
    sky_pattern chunk[SKY_PATTERN_CHUNK];
    sky_pattern batch[SKY_VERIFY_BATCH];
    uint8_t batch_error[SKY_VERIFY_BATCH];
    unsigned batch_count;

    sky_field field;
    uint32_t cached_field;

    /* Current hypothesis: field units -> image pixels, Q16. */
    int32_t matrix_q16[4];
    int32_t translate_q16[2];
    uint32_t scale_min;
    uint32_t scale_max;

    int16_t pixel_x[ASTRO_SKY_VERIFY_STARS];
    int16_t pixel_y[ASTRO_SKY_VERIFY_STARS];
    int8_t association[ASTRO_MAX_STARS]; /* observed star -> field star, -1 none */

    /* Export: field stars re-projected about the image centre. */
    int32_t star_x_q4[ASTRO_SKY_VERIFY_STARS];
    int32_t star_y_q4[ASTRO_SKY_VERIFY_STARS];
    int8_t star_ok[ASTRO_SKY_VERIFY_STARS];
};

/* Everything verification needs to know about the image and the hypothesis source. */
typedef struct {
    const astro_star *stars;
    int count;
    int width;
    int height;
    const astro_sky_index *index;
    astro_sky_progress progress;
    void *progress_context;
    astro_sky_workspace *ws;
    astro_sky_solution *out;
} sky_context;

/* sky_pattern.c */
int astro_sky_quad_code(const int x[4], const int y[4], sky_quad *quad);
int astro_sky_neighbour_keys(const sky_quad *quad, uint32_t keys[SKY_MAX_KEYS]);
int astro_sky_fine_error(uint32_t image_fine, uint32_t pattern_fine);

/* sky_verify.c: returns ASTRO_SKY_SOLVED, ASTRO_SKY_NO_MATCH, BAD_INDEX or CANCELLED. */
astro_sky_status astro_sky_verify_batch(sky_context *ctx, int star_a, int star_b, int flip,
                                        uint32_t quad_mask);

#endif
