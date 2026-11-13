/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Blind solve driver: enumerate image quads from the brightest stars outward, look up
 * their descriptor (and its noisy neighbours) in SKY.DIR, stream the matching SKY.PAT
 * records from the card and verify the closest ones. Stops at the first verified match.
 */
#include <string.h>
#include "sky_internal.h"

#define MIN_STARS           8
#define MAX_QUADS           5000
#define MIN_WIDTH           100
#define MAX_WIDTH           4096
#define MIN_HEIGHT          80
#define MAX_HEIGHT          2160
#define MAX_KEYS            65536
#define MIN_QUAD_SPACING_SQ 64 /* 8 px between any two quad stars */
#define PROGRESS_EVERY      8

unsigned astro_sky_workspace_size(void)
{
    return sizeof(astro_sky_workspace);
}

static int index_is_valid(const astro_sky_index *index)
{
    const astro_sky_dir_entry *dir = index->dir;

    if (!dir || !index->read_patterns || !index->read_fields || !index->nkeys ||
        index->nkeys > MAX_KEYS || !index->nfields ||
        dir[index->nkeys].offset != index->npatterns) {
        return 0;
    }
    for (unsigned i = 0; i < index->nkeys; i++) {
        if (dir[i].offset > dir[i + 1].offset || dir[i].key >= dir[i + 1].key ||
            dir[i + 1].offset > index->npatterns) {
            return 0;
        }
    }
    return 1;
}

static int find_key(const astro_sky_dir_entry *dir, unsigned count, uint32_t key)
{
    unsigned lo = 0;
    unsigned hi = count;

    while (lo < hi) {
        unsigned mid = lo + (hi - lo) / 2;
        if (dir[mid].key < key) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo < count && dir[lo].key == key ? (int)lo : -1;
}

static void set_scale_window(astro_sky_workspace *ws, const astro_sky_options *options)
{
    ws->scale_min = ASTRO_SKY_SCALE_MIN;
    ws->scale_max = ASTRO_SKY_SCALE_MAX;
    if (options && options->scale_min) {
        ws->scale_min =
            options->scale_min < ASTRO_SKY_SCALE_MIN ? ASTRO_SKY_SCALE_MIN : options->scale_min;
        ws->scale_max =
            options->scale_max > ASTRO_SKY_SCALE_MAX ? ASTRO_SKY_SCALE_MAX : options->scale_max;
    }
}

static int report_progress(const sky_context *ctx)
{
    return !ctx->progress ||
           ctx->progress(ctx->progress_context, ctx->out->quads, ctx->out->checked);
}

/* Quads must be wide (longest side at least a quarter of the image) and have no two stars
 * closer than 8 px, or the descriptor is dominated by centroid noise. */
static int quad_is_usable(const int x[4], const int y[4], int width)
{
    int longest = 0;
    int shortest = 0x7fffffff;

    for (int i = 0; i < 4; i++) {
        for (int j = i + 1; j < 4; j++) {
            int dx = x[i] - x[j];
            int dy = y[i] - y[j];
            int d = dx * dx + dy * dy;
            if (d > longest) {
                longest = d;
            }
            if (d < shortest) {
                shortest = d;
            }
        }
    }
    return longest >= (width / 4) * (width / 4) && shortest >= MIN_QUAD_SPACING_SQ;
}

/* Streams all patterns stored under one key and batches those whose fine descriptor is
 * close. Verifies early if the batch fills up. */
static astro_sky_status collect_key(sky_context *ctx, int entry, uint32_t key, const sky_quad *quad,
                                    const int ids[4], uint32_t quad_mask)
{
    astro_sky_workspace *ws = ctx->ws;
    const astro_sky_index *index = ctx->index;
    uint32_t pos = index->dir[entry].offset;
    uint32_t end = index->dir[entry + 1].offset;

    while (pos < end) {
        if (ctx->out->reads % PROGRESS_EVERY == 0 && !report_progress(ctx)) {
            return ASTRO_SKY_CANCELLED;
        }
        uint32_t take = end - pos > SKY_PATTERN_CHUNK ? SKY_PATTERN_CHUNK : end - pos;
        ctx->out->reads++;
        if (!index->read_patterns(index->patterns_context,
                                  ASTRO_SKY_FILE_HEADER + pos * ASTRO_SKY_PATTERN_BYTES, ws->chunk,
                                  take * ASTRO_SKY_PATTERN_BYTES)) {
            return ASTRO_SKY_BAD_INDEX;
        }
        for (unsigned r = 0; r < take; r++) {
            const sky_pattern *p = &ws->chunk[r];
            if (p->key != key) {
                return ASTRO_SKY_BAD_INDEX;
            }
            int error = astro_sky_fine_error(quad->fine, p->fine);
            if (error < 0 || error > SKY_MAX_FINE_ERROR) {
                continue;
            }
            ws->batch[ws->batch_count] = *p;
            ws->batch_error[ws->batch_count++] = (uint8_t)error;
            if (ws->batch_count == SKY_VERIFY_BATCH) {
                astro_sky_status status = astro_sky_verify_batch(
                    ctx, ids[quad->baseline_a], ids[quad->baseline_b], quad->flip, quad_mask);
                if (status != ASTRO_SKY_NO_MATCH) {
                    return status;
                }
            }
        }
        pos += take;
    }
    return ASTRO_SKY_NO_MATCH;
}

/* Looks up one image quad (stars ids[0..3]) and verifies its candidates. */
static astro_sky_status try_quad(sky_context *ctx, const int ids[4])
{
    const astro_star *s = ctx->stars;
    int x[4] = {s[ids[0]].x, s[ids[1]].x, s[ids[2]].x, s[ids[3]].x};
    int y[4] = {s[ids[0]].y, s[ids[1]].y, s[ids[2]].y, s[ids[3]].y};
    uint32_t keys[SKY_MAX_KEYS];
    sky_quad quad;

    if (!quad_is_usable(x, y, ctx->width)) {
        return ASTRO_SKY_NO_MATCH;
    }
    if (ctx->out->quads >= MAX_QUADS) {
        return ASTRO_SKY_BUDGET_EXHAUSTED;
    }
    ctx->out->quads++;
    if (ctx->out->quads % PROGRESS_EVERY == 1 && !report_progress(ctx)) {
        return ASTRO_SKY_CANCELLED;
    }
    if (!astro_sky_quad_code(x, y, &quad)) {
        return ASTRO_SKY_NO_MATCH;
    }

    uint32_t quad_mask = (1u << ids[0]) | (1u << ids[1]) | (1u << ids[2]) | (1u << ids[3]);
    int nkeys = astro_sky_neighbour_keys(&quad, keys);
    ctx->ws->batch_count = 0;
    for (int k = 0; k < nkeys; k++) {
        int entry = find_key(ctx->index->dir, ctx->index->nkeys, keys[k]);
        if (entry < 0) {
            continue;
        }
        astro_sky_status status = collect_key(ctx, entry, keys[k], &quad, ids, quad_mask);
        if (status != ASTRO_SKY_NO_MATCH) {
            return status;
        }
    }
    return astro_sky_verify_batch(ctx, ids[quad.baseline_a], ids[quad.baseline_b], quad.flip,
                                  quad_mask);
}

astro_sky_status astro_sky_solve(const astro_star *stars, int count, int width, int height,
                                 const astro_sky_index *index, const astro_sky_options *options,
                                 astro_sky_progress progress, void *progress_context,
                                 astro_sky_workspace *workspace, astro_sky_solution *out)
{
    sky_context ctx = {stars,     count, width, height, index, progress, progress_context,
                       workspace, out};

    if (!stars || !index || !workspace || !out || count < MIN_STARS || count > ASTRO_MAX_STARS ||
        width < MIN_WIDTH || width > MAX_WIDTH || height < MIN_HEIGHT || height > MAX_HEIGHT) {
        return ASTRO_SKY_BAD_INDEX;
    }
    if (options && options->scale_min && options->scale_min > options->scale_max) {
        return ASTRO_SKY_BAD_INDEX;
    }
    memset(out, 0, sizeof(*out));
    workspace->cached_field = 0xffffffffu;
    workspace->batch_count = 0;
    set_scale_window(workspace, options);
    if (!index_is_valid(index)) {
        return ASTRO_SKY_BAD_INDEX;
    }

    /* Grow the star set one rank at a time, so the budget is not spent on quads that
     * contain a hot pixel or the first false detection. */
    for (int last = 3; last < count; last++) {
        for (int a = 0; a < last - 2; a++) {
            for (int b = a + 1; b < last - 1; b++) {
                for (int c = b + 1; c < last; c++) {
                    int ids[4] = {a, b, c, last};
                    astro_sky_status status = try_quad(&ctx, ids);
                    if (status != ASTRO_SKY_NO_MATCH) {
                        return status;
                    }
                }
            }
        }
    }
    return ASTRO_SKY_NO_MATCH;
}
