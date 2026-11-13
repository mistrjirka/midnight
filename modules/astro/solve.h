/* SPDX-License-Identifier: GPL-2.0-only */
/* Detect stars in a frame and plate-solve it against the card's sky index. */
#ifndef ASTRO_SOLVE_H
#define ASTRO_SOLVE_H

#include <stdint.h>
#include "core/sky_solver.h"
#include "core/star.h"

typedef enum {
    ASTRO_SOLVE_OK = 0,
    ASTRO_SOLVE_FEW_STARS,
    ASTRO_SOLVE_NO_MATCH,
    ASTRO_SOLVE_CANCELLED,
    ASTRO_SOLVE_NO_INDEX,
    ASTRO_SOLVE_BAD_INDEX,
    ASTRO_SOLVE_NO_MEMORY
} astro_solve_status;

typedef struct {
    int valid;
    astro_sky_solution sky;
    astro_star stars[ASTRO_MAX_STARS];
    int star_count;
    int trail_masked;
    int hinted;
    uint32_t elapsed_ms;
    char ra_text[16];   /* "05h35m17s" */
    char dec_text[16];  /* "-05d23'12\"" */
    char lens_text[24]; /* "135 mm, FOV 15.1" */
} astro_solve_result;

#define ASTRO_MIN_STARS 8

/* Solves `image` in place (trail masking modifies it). Gives up after budget_ms or when
 * the worker is asked to cancel. The index stays cached for the next call. */
astro_solve_status astro_solve_frame(uint8_t *image, uint32_t budget_ms, astro_solve_result *out);

const char *astro_solve_status_text(astro_solve_status status);

/* Frees the cached index and scratch memory (after a solve or a live loop). */
void astro_solve_release(void);

#endif
