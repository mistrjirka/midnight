/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Difference of Gaussians (DoG) star detection. A narrow Gaussian minus a wide one responds
 * to compact sources and ignores broad glow, gradients and wide trails.
 *
 * Both separable 1D kernels sum to 1024. The horizontal pass is stored at Q8 in a ring of
 * rows, so no full-image blurred copies are needed; the vertical pass yields Q4 responses.
 */
#include "star_detect.h"

#define WIDE_RADIUS  15 /* half-width of the wide kernel */
#define SMALL_RADIUS 4  /* non-zero taps of the narrow kernel */
#define WIDE_TAPS    12 /* non-zero taps of the wide kernel */

/* Ignore partial sources near the edge. */
#define BORDER 8

/* 9x9 non-maximum suppression. */
#define NMS_RADIUS 4

/* Response must exceed one grey level (Q4) above the halo. */
#define MIN_RESPONSE_Q4 16

/* Peaks this close to an accepted one are the same star. */
#define DUPLICATE_DISTANCE 5

static const uint16_t small_kernel[13] = {0, 0, 5, 29, 105, 226, 294, 226, 105, 29, 5, 0, 0};
static const uint16_t wide_kernel[31] = {
    0,   0,  0,  1,  2,  3,  7,  12, 20, 31, 45, 62, 79, 94, 104, 104,
    104, 94, 79, 62, 45, 31, 20, 12, 7,  3,  2,  1,  0,  0,  0,
};
#define SMALL_CENTER 6
#define WIDE_CENTER  15

static int clamp_index(int i, int limit)
{
    if (i < 0) {
        return 0;
    }
    return i >= limit ? limit - 1 : i;
}

static int abs_int(int x)
{
    return x < 0 ? -x : x;
}

/* Horizontal Q8 blur of one image row with both kernels. */
static void blur_row(const uint8_t *row, int width, uint16_t *small_out, uint16_t *wide_out)
{
    for (int x = 0; x < width; x++) {
        uint32_t small = 0;
        uint32_t wide = 0;

        if (x >= WIDE_TAPS && x < width - WIDE_TAPS) {
            /* Interior: no clamping needed. Zero taps are skipped; the sums are unchanged. */
            const uint8_t *p = row + x;
            for (int j = -SMALL_RADIUS; j <= SMALL_RADIUS; j++) {
                small += (uint32_t)small_kernel[j + SMALL_CENTER] * p[j];
            }
            for (int j = -WIDE_TAPS; j <= WIDE_TAPS; j++) {
                wide += (uint32_t)wide_kernel[j + WIDE_CENTER] * p[j];
            }
        } else {
            for (int j = -SMALL_RADIUS; j <= SMALL_RADIUS; j++) {
                small += (uint32_t)small_kernel[j + SMALL_CENTER] * row[clamp_index(x + j, width)];
            }
            for (int j = -WIDE_TAPS; j <= WIDE_TAPS; j++) {
                wide += (uint32_t)wide_kernel[j + WIDE_CENTER] * row[clamp_index(x + j, width)];
            }
        }
        small_out[x] = (uint16_t)((small + 2) / 4);
        wide_out[x] = (uint16_t)((wide + 2) / 4);
    }
}

/* Vertical pass for output row y, from the ring of horizontally blurred rows. */
static void vertical_row(astro_detect_scratch *s, int y, int width, int height)
{
    /* Ring slots are invariant along the row; computing them per pixel costs millions of
     * software divisions on the ARMv5. */
    const uint16_t *small_rows[2 * SMALL_RADIUS + 1];
    const uint16_t *wide_rows[2 * WIDE_TAPS + 1];

    for (int k = -SMALL_RADIUS; k <= SMALL_RADIUS; k++) {
        small_rows[k + SMALL_RADIUS] =
            s->small_rows[clamp_index(y + k, height) % ASTRO_DETECT_RING_ROWS];
    }
    for (int k = -WIDE_TAPS; k <= WIDE_TAPS; k++) {
        wide_rows[k + WIDE_TAPS] =
            s->large_rows[clamp_index(y + k, height) % ASTRO_DETECT_RING_ROWS];
    }

    for (int x = 0; x < width; x++) {
        uint32_t small = 0;
        uint32_t wide = 0;
        for (int k = 0; k < 2 * SMALL_RADIUS + 1; k++) {
            small += (uint32_t)small_kernel[k + SMALL_CENTER - SMALL_RADIUS] * small_rows[k][x];
        }
        for (int k = 0; k < 2 * WIDE_TAPS + 1; k++) {
            wide += (uint32_t)wide_kernel[k + WIDE_CENTER - WIDE_TAPS] * wide_rows[k][x];
        }
        s->response[y * width + x] = (int16_t)((int32_t)(small >> 14) - (int32_t)(wide >> 14));
    }
}

static void compute_response(const uint8_t *image, int width, int height, int stride,
                             astro_detect_scratch *s)
{
    /* Each ring slot is reused once the vertical pass no longer needs it. */
    for (int step = 0; step < height + WIDE_RADIUS; step++) {
        int source_y = clamp_index(step, height);
        int slot = step % ASTRO_DETECT_RING_ROWS;
        blur_row(image + source_y * stride, width, s->small_rows[slot], s->large_rows[slot]);
        if (step >= WIDE_RADIUS) {
            vertical_row(s, step - WIDE_RADIUS, width, height);
        }
    }
}

/* Strict local maximum; on a plateau only the first pixel in scan order qualifies. */
static int is_local_max(const int16_t *response, int width, int x, int y)
{
    int value = response[y * width + x];

    for (int dy = -NMS_RADIUS; dy <= NMS_RADIUS; dy++) {
        for (int dx = -NMS_RADIUS; dx <= NMS_RADIUS; dx++) {
            if (!dx && !dy) {
                continue;
            }
            int neighbour = response[(y + dy) * width + x + dx];
            int earlier = dy < 0 || (dy == 0 && dx < 0);
            if (neighbour > value || (neighbour == value && earlier)) {
                return 0;
            }
        }
    }
    return 1;
}

static int is_duplicate(const astro_star *stars, int count, int x, int y)
{
    for (int i = 0; i < count; i++) {
        if (abs_int(stars[i].x - x) <= DUPLICATE_DISTANCE &&
            abs_int(stars[i].y - y) <= DUPLICATE_DISTANCE) {
            return 1;
        }
    }
    return 0;
}

/* Keeps the list sorted by flux, brightest first, dropping the faintest when full.
 * Returns the new count. */
static int insert_sorted(astro_star *stars, int count, int max_stars, int x, int y, int flux)
{
    if (count < max_stars) {
        count++;
    } else if (flux <= stars[count - 1].flux) {
        return count;
    }
    stars[count - 1].x = (int16_t)x;
    stars[count - 1].y = (int16_t)y;
    stars[count - 1].flux = (uint16_t)flux;
    for (int i = count - 1; i > 0 && stars[i].flux > stars[i - 1].flux; i--) {
        astro_star tmp = stars[i];
        stars[i] = stars[i - 1];
        stars[i - 1] = tmp;
    }
    return count;
}

int astro_detect_stars(const uint8_t *image, int width, int height, int stride,
                       astro_detect_scratch *scratch, astro_star *out, int max_stars)
{
    int count = 0;

    if (!image || !scratch || !out || width < 32 || height < 32 || width > ASTRO_DETECT_MAX_WIDTH ||
        height > ASTRO_DETECT_MAX_HEIGHT || stride < width || max_stars < 1) {
        return 0;
    }
    if (max_stars > ASTRO_MAX_STARS) {
        max_stars = ASTRO_MAX_STARS;
    }

    compute_response(image, width, height, stride, scratch);

    for (int y = BORDER; y < height - BORDER; y++) {
        for (int x = BORDER; x < width - BORDER; x++) {
            int value = scratch->response[y * width + x];
            if (value <= MIN_RESPONSE_Q4 || !is_local_max(scratch->response, width, x, y) ||
                is_duplicate(out, count, x, y)) {
                continue;
            }
            count = insert_sorted(out, count, max_stars, x, y, value);
        }
    }
    return count;
}
