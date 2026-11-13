/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Four-star pattern descriptors. The longest pair of a quad is its baseline; the other two
 * stars are expressed in the baseline's frame, which makes the code invariant to position,
 * rotation and scale. Both baseline directions and both mirror parities are tried and the
 * smallest code wins, so the image and the index agree on one canonical form.
 */
#include "sky_internal.h"

#define FINE_MAX        255
#define MIN_BASELINE_SQ 400 /* baseline of at least 20 px */
#define NEIGHBOUR_SLACK 212 /* max |coarse * 255 - fine * 24| for a neighbouring cell */
#define MAX_FINE_DELTA  3

/* The two non-baseline stars in the baseline frame, coarse (0..24) and fine (0..255). */
typedef struct {
    int along[2];  /* coarse projection on the baseline */
    int across[2]; /* coarse signed distance from the baseline */
    int fine_along[2];
    int fine_across[2];
} baseline_frame;

static int clamp(int v, int lo, int hi)
{
    if (v < lo) {
        return lo;
    }
    return v > hi ? hi : v;
}

static void swap_int(int *a, int *b)
{
    int t = *a;
    *a = *b;
    *b = t;
}

/* Rounded num * scale / den, symmetric around zero. */
static int round_scaled(int64_t num, int scale, int64_t den)
{
    return (int)((num * scale + (num >= 0 ? den / 2 : -den / 2)) / den);
}

/* Longest pair (first one found on ties). Returns its squared length. */
static int longest_pair(const int x[4], const int y[4], int *a, int *b)
{
    int longest = -1;

    for (int i = 0; i < 4; i++) {
        for (int j = i + 1; j < 4; j++) {
            int dx = x[j] - x[i];
            int dy = y[j] - y[i];
            int d = dx * dx + dy * dy;
            if (d > longest) {
                longest = d;
                *a = i;
                *b = j;
            }
        }
    }
    return longest;
}

static void project_onto_baseline(const int x[4], const int y[4], int a, int b, const int others[2],
                                  baseline_frame *f)
{
    int bx = x[b] - x[a];
    int by = y[b] - y[a];
    int64_t d = bx * bx + by * by;

    for (int i = 0; i < 2; i++) {
        int dx = x[others[i]] - x[a];
        int dy = y[others[i]] - y[a];
        int64_t dot = (int64_t)dx * bx + (int64_t)dy * by;
        int64_t cross = (int64_t)dx * by - (int64_t)dy * bx;

        f->along[i] = round_scaled(dot, SKY_CODE_STEPS, d);
        f->across[i] = round_scaled(cross, SKY_CODE_STEPS, d);
        f->fine_along[i] = clamp(round_scaled(dot, FINE_MAX, d), 0, FINE_MAX);
        /* Map cross/d from [-1, 1] to [0, 255]. */
        f->fine_across[i] = clamp((int)(((cross + d) * FINE_MAX + d) / (2 * d)), 0, FINE_MAX);
    }
}

/* Code and fine descriptor of one baseline orientation and mirror parity. */
static void encode(const baseline_frame *f, int mirrored, uint32_t *code, uint32_t *fine)
{
    int x0 = clamp(f->along[0], 0, SKY_CODE_STEPS);
    int x1 = clamp(f->along[1], 0, SKY_CODE_STEPS);
    int y0 = mirrored ? -f->across[0] : f->across[0];
    int y1 = mirrored ? -f->across[1] : f->across[1];
    int fx0 = f->fine_along[0];
    int fx1 = f->fine_along[1];
    int fy0 = mirrored ? FINE_MAX - f->fine_across[0] : f->fine_across[0];
    int fy1 = mirrored ? FINE_MAX - f->fine_across[1] : f->fine_across[1];

    /* Signed distance -24..24 to the 0..24 grid. */
    y0 = clamp((y0 + SKY_CODE_STEPS + 1) / 2, 0, SKY_CODE_STEPS);
    y1 = clamp((y1 + SKY_CODE_STEPS + 1) / 2, 0, SKY_CODE_STEPS);

    /* Order the two stars so the code does not depend on their input order. */
    if (x0 > x1 || (x0 == x1 && y0 > y1)) {
        swap_int(&x0, &x1);
        swap_int(&y0, &y1);
        swap_int(&fx0, &fx1);
        swap_int(&fy0, &fy1);
    }
    *code = ((uint32_t)x0 << 15) | ((uint32_t)y0 << 10) | ((uint32_t)x1 << 5) | (uint32_t)y1;
    *fine = ((uint32_t)fx0 << 24) | ((uint32_t)fy0 << 16) | ((uint32_t)fx1 << 8) | (uint32_t)fy1;
}

int astro_sky_quad_code(const int x[4], const int y[4], sky_quad *quad)
{
    int a = 0;
    int b = 1;

    quad->code = SKY_INVALID_CODE;
    if (longest_pair(x, y, &a, &b) < MIN_BASELINE_SQ) {
        return 0;
    }

    int others[2];
    int n = 0;
    for (int i = 0; i < 4; i++) {
        if (i != a && i != b) {
            others[n++] = i;
        }
    }

    for (int swap = 0; swap < 2; swap++) {
        int from = swap ? b : a;
        int to = swap ? a : b;
        baseline_frame frame;
        project_onto_baseline(x, y, from, to, others, &frame);

        for (int mirrored = 0; mirrored < 2; mirrored++) {
            uint32_t code, fine;
            encode(&frame, mirrored, &code, &fine);
            if (code < quad->code) {
                quad->code = code;
                quad->fine = fine;
                quad->baseline_a = from;
                quad->baseline_b = to;
                quad->flip = mirrored;
            }
        }
    }
    return 1;
}

static int code_field(uint32_t code, int index)
{
    return (int)((code >> (15 - 5 * index)) & 31);
}

static int fine_field(uint32_t fine, int index)
{
    return (int)((fine >> (24 - 8 * index)) & FINE_MAX);
}

/* Measurement noise can push a coordinate into a neighbouring grid cell. Every combination
 * of +-1 cells that is still consistent with the fine descriptor becomes a lookup key. */
int astro_sky_neighbour_keys(const sky_quad *quad, uint32_t keys[SKY_MAX_KEYS])
{
    int count = 0;

    for (int combo = 0; combo < SKY_MAX_KEYS; combo++) {
        int value[4];
        int ok = 1;
        int digits = combo;

        /* combo enumerates offsets -1..1 for the 4 coordinates, last coordinate fastest. */
        for (int d = 3; d >= 0; d--) {
            value[d] = code_field(quad->code, d) + digits % 3 - 1;
            digits /= 3;
        }
        for (int d = 0; d < 4 && ok; d++) {
            int error = value[d] * FINE_MAX - fine_field(quad->fine, d) * SKY_CODE_STEPS;
            ok = value[d] >= 0 && value[d] <= SKY_CODE_STEPS && error <= NEIGHBOUR_SLACK &&
                 error >= -NEIGHBOUR_SLACK;
        }
        if (ok) {
            keys[count++] = ((uint32_t)value[0] << 15) | ((uint32_t)value[1] << 10) |
                            ((uint32_t)value[2] << 5) | (uint32_t)value[3];
        }
    }
    return count;
}

/* Sum of the fine-descriptor differences, or -1 if any single one exceeds 3. */
int astro_sky_fine_error(uint32_t image_fine, uint32_t pattern_fine)
{
    int error = 0;

    for (int shift = 0; shift < 32; shift += 8) {
        int delta =
            (int)((image_fine >> shift) & FINE_MAX) - (int)((pattern_fine >> shift) & FINE_MAX);
        if (delta < 0) {
            delta = -delta;
        }
        if (delta > MAX_FINE_DELTA) {
            return -1;
        }
        error += delta;
    }
    return error;
}
