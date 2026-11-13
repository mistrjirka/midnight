/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Hypothesis verification. A matching pattern only proposes a transform; it is accepted
 * after independent field stars line up, the fit is refined twice, and the number of
 * coincidences is statistically significant given how many hypotheses were tried.
 */
#include <string.h>
#include "sky_internal.h"

#define Q16_ONE 65536
#define Q24_ONE 16777216

#define FRAME_MARGIN         10 /* px: field stars this far outside still associate */
#define MAX_OFFSET_PX        4  /* per axis, before the squared-distance test */
#define FIRST_TOLERANCE_SQ   10
#define REFINED_TOLERANCE_SQ 6
#define EXPORT_TOLERANCE_SQ  6 /* final re-association, px^2 (strictly below 7) */
#define EXPORT_MAX_OFFSET    3
#define MIN_MATCHES          8
#define MIN_INDEPENDENT      4      /* matches outside the four pattern stars */
#define MIN_BASELINE_SQ      250000 /* field units^2 */
#define MIN_FIELD_STARS      10
#define MAX_UNIT_MDEG        8
#define MIN_FIT_STARS        4
#define MIN_FIT_SPREAD       100
#define PROGRESS_EVERY       16

/* Accept when the expected number of chance matches is below 1e-6 (log10 * 1000). */
#define FALSE_ALARM_LOG10_MILLI (-6000)
/* Integer pixel offsets within the final 6 px^2 association radius. */
#define MATCH_AREA_PX 21

/* 1000 * log10(k!) for k = 0..32. */
static const uint16_t log10_factorial[ASTRO_MAX_STARS + 1] = {
    0,     0,     301,   778,   1380,  2079,  2857,  3702,  4606,  5560,  6560,
    7601,  8680,  9794,  10940, 12116, 13321, 14551, 15806, 17085, 18386, 19708,
    21051, 22412, 23793, 25191, 26606, 28037, 29484, 30947, 32424, 33915, 35420,
};

/* 1000 * log10(1 + i / 16) for i = 0..15. */
static const uint16_t log10_fraction[16] = {0,   26,  51,  75,  97,  118, 138, 158,
                                            176, 194, 211, 227, 243, 258, 273, 287};

static int abs_int(int x)
{
    return x < 0 ? -x : x;
}

static int64_t abs64(int64_t x)
{
    return x < 0 ? -x : x;
}

static int popcount32(uint32_t x)
{
    int n = 0;
    while (x) {
        x &= x - 1;
        n++;
    }
    return n;
}

/* 1000 * log10(x), to about 0.01. */
static int log10_milli(uint32_t x)
{
    int exponent = 0;

    if (!x) {
        return -100000;
    }
    while (x >= 32) {
        x >>= 1;
        exponent++;
    }
    while (x < 16) {
        x <<= 1;
        exponent--;
    }
    /* x is now 16..31: log10(16) = 1.204, each doubling adds 0.301. */
    return 1204 + exponent * 301 + log10_fraction[x - 16];
}

static int bit_is_set(const uint32_t *bits, unsigned i)
{
    return (bits[i >> 5] >> (i & 31)) & 1;
}

static void set_bit(uint32_t *bits, unsigned i)
{
    bits[i >> 5] |= 1u << (i & 31);
}

/*
 * The four pattern stars match by construction, so only the others are evidence. The
 * chance of k coincidences among n candidates is at most C(n,k) * p^k, with
 * p = (field stars in frame) * (match area) / (frame area). Multiplying by the number of
 * hypotheses tested so far is the Bonferroni correction.
 */
static int is_significant(const sky_context *ctx, int candidates, int independent)
{
    const astro_sky_workspace *ws = ctx->ws;
    int inside = 0;

    for (unsigned i = 0; i < ws->field.count; i++) {
        inside += ws->pixel_x[i] != SKY_OFF_FRAME;
    }
    if (independent < MIN_INDEPENDENT || candidates < independent || candidates > ASTRO_MAX_STARS ||
        !inside) {
        return 0;
    }
    uint32_t frame_area =
        (uint32_t)(ctx->width + 2 * FRAME_MARGIN) * (uint32_t)(ctx->height + 2 * FRAME_MARGIN);
    int log_p = log10_milli((uint32_t)inside * MATCH_AREA_PX) - log10_milli(frame_area);
    uint32_t hypotheses = ctx->out->checked ? ctx->out->checked : 1;
    int log_false = log10_factorial[candidates] - log10_factorial[independent] -
                    log10_factorial[candidates - independent] + independent * log_p +
                    log10_milli(hypotheses);
    return log_false <= FALSE_ALARM_LOG10_MILLI;
}

/* Projects all field stars with the current hypothesis into pixel_x/pixel_y. */
static void project_field(sky_context *ctx)
{
    astro_sky_workspace *ws = ctx->ws;

    for (unsigned k = 0; k < ws->field.count; k++) {
        const sky_field_star *s = &ws->field.stars[k];
        int64_t x = (int64_t)ws->matrix_q16[0] * s->x + (int64_t)ws->matrix_q16[1] * s->y +
                    ws->translate_q16[0];
        int64_t y = (int64_t)ws->matrix_q16[2] * s->x + (int64_t)ws->matrix_q16[3] * s->y +
                    ws->translate_q16[1];
        x = (x + Q16_ONE / 2) >> 16;
        y = (y + Q16_ONE / 2) >> 16;
        int outside = x < -FRAME_MARGIN || x > ctx->width + FRAME_MARGIN || y < -FRAME_MARGIN ||
                      y > ctx->height + FRAME_MARGIN;
        ws->pixel_x[k] = (int16_t)(outside ? SKY_OFF_FRAME : x);
        ws->pixel_y[k] = (int16_t)(outside ? SKY_OFF_FRAME : y);
    }
}

/* Greedy nearest-neighbour association, each field star used once. Returns matches. */
static int associate(sky_context *ctx, int tolerance_sq, int *error, uint32_t *mask)
{
    astro_sky_workspace *ws = ctx->ws;
    uint32_t used[3] = {0, 0, 0};
    int matches = 0;

    project_field(ctx);
    *error = 0;
    *mask = 0;
    for (int o = 0; o < ctx->count; o++) {
        int best = -1;
        int best_distance = tolerance_sq + 1;
        for (unsigned k = 0; k < ws->field.count; k++) {
            if (bit_is_set(used, k) || ws->pixel_x[k] == SKY_OFF_FRAME) {
                continue;
            }
            int dx = ws->pixel_x[k] - ctx->stars[o].x;
            int dy = ws->pixel_y[k] - ctx->stars[o].y;
            if (abs_int(dx) > MAX_OFFSET_PX || abs_int(dy) > MAX_OFFSET_PX) {
                continue;
            }
            int distance = dx * dx + dy * dy;
            if (distance < best_distance) {
                best = (int)k;
                best_distance = distance;
            }
        }
        ws->association[o] = (int8_t)best;
        if (best >= 0) {
            set_bit(used, (unsigned)best);
            *error += best_distance;
            matches++;
            *mask |= 1u << o;
        }
    }
    return matches;
}

/* Least-squares similarity (with fixed mirror parity) from associated pairs. */
static int refine(sky_context *ctx, int parity)
{
    astro_sky_workspace *ws = ctx->ws;
    int64_t sx = 0, sy = 0, su = 0, sv = 0, ss = 0, sa = 0, sb = 0;
    int used = 0;

    for (int i = 0; i < ctx->count; i++) {
        if (ws->association[i] < 0) {
            continue;
        }
        const sky_field_star *c = &ws->field.stars[(unsigned)ws->association[i]];
        int64_t x = c->x;
        int64_t y = c->y * parity;
        int64_t u = ctx->stars[i].x;
        int64_t v = ctx->stars[i].y;
        sx += x;
        sy += y;
        su += u;
        sv += v;
        ss += x * x + y * y;
        sa += x * u + y * v;
        sb += x * v - y * u;
        used++;
    }
    int64_t den = used * ss - sx * sx - sy * sy;
    if (used < MIN_FIT_STARS || den < MIN_FIT_SPREAD) {
        return 0;
    }
    int64_t a = (used * sa - sx * su - sy * sv) * Q16_ONE / den;
    int64_t b = (used * sb - sx * sv + sy * su) * Q16_ONE / den;
    if (abs64(a) > Q16_ONE || abs64(b) > Q16_ONE) {
        return 0;
    }
    ws->matrix_q16[0] = (int32_t)a;
    ws->matrix_q16[1] = (int32_t)(-b * parity);
    ws->matrix_q16[2] = (int32_t)b;
    ws->matrix_q16[3] = (int32_t)(a * parity);
    ws->translate_q16[0] = (int32_t)((su * Q16_ONE - a * sx + b * sy) / used);
    ws->translate_q16[1] = (int32_t)((sv * Q16_ONE - b * sx - a * sy) / used);
    return 1;
}

/* Same normal equations as refine(), but from tangent-plane millidegrees about the image
 * centre, with Q24 output for wide-lens precision. */
static int fit_wcs(sky_context *ctx, int parity, astro_wcs *wcs)
{
    astro_sky_workspace *ws = ctx->ws;
    int64_t sx = 0, sy = 0, su = 0, sv = 0, ss = 0, sa = 0, sb = 0;
    int used = 0;

    for (int i = 0; i < ctx->count; i++) {
        int k = ws->association[i];
        if (k < 0 || !ws->star_ok[k]) {
            continue;
        }
        int64_t x = ws->star_x_q4[k] / 16;
        int64_t y = parity * (ws->star_y_q4[k] / 16);
        int64_t u = ctx->stars[i].x;
        int64_t v = ctx->stars[i].y;
        sx += x;
        sy += y;
        su += u;
        sv += v;
        ss += x * x + y * y;
        sa += x * u + y * v;
        sb += x * v - y * u;
        used++;
    }
    int64_t den = used * ss - sx * sx - sy * sy;
    if (used < MIN_FIT_STARS || den < MIN_FIT_SPREAD) {
        return 0;
    }
    int64_t a = (used * sa - sx * su - sy * sv) * Q24_ONE / den;
    int64_t b = (used * sb - sx * sv + sy * su) * Q24_ONE / den;
    if (!a && !b) {
        return 0;
    }
    wcs->m_q24[0] = (int32_t)a;
    wcs->m_q24[1] = (int32_t)(-b * parity);
    wcs->m_q24[2] = (int32_t)b;
    wcs->m_q24[3] = (int32_t)(a * parity);
    wcs->t_q16[0] = (int32_t)((su * Q16_ONE - (a * sx - b * sy) / 256) / used);
    wcs->t_q16[1] = (int32_t)((sv * Q16_ONE - (b * sx + a * sy) / 256) / used);
    return 1;
}

/* Re-expresses every field star in tangent coordinates about the solved image centre. */
static void reproject_about_centre(sky_context *ctx, int32_t unit)
{
    astro_sky_workspace *ws = ctx->ws;
    astro_wcs identity = {ws->field.ra, ws->field.dec, {Q24_ONE, 0, 0, Q24_ONE}, {0, 0}};
    astro_wcs centre = identity;

    /* With an identity matrix, "pixels" are tangent millidegrees. */
    centre.ra0 = ctx->out->ra;
    centre.dec0 = ctx->out->dec;
    for (unsigned k = 0; k < ws->field.count; k++) {
        uint32_t ra;
        int32_t dec;
        const sky_field_star *s = &ws->field.stars[k];
        ws->star_ok[k] =
            (int8_t)(astro_wcs_unproject(&identity, s->x * unit * 16, s->y * unit * 16, &ra,
                                         &dec) &&
                     astro_wcs_project(&centre, ra, dec, &ws->star_x_q4[k], &ws->star_y_q4[k]));
    }
}

/* One association pass across the whole frame with the refitted transform. */
static int reassociate(sky_context *ctx, const astro_wcs *wcs, int *error, uint32_t *mask)
{
    astro_sky_workspace *ws = ctx->ws;
    uint32_t used[3] = {0, 0, 0};
    int matches = 0;

    *error = 0;
    *mask = 0;
    for (int o = 0; o < ctx->count; o++) {
        int best = -1;
        int best_distance = EXPORT_TOLERANCE_SQ + 1;
        for (unsigned k = 0; k < ws->field.count; k++) {
            if (!ws->star_ok[k] || bit_is_set(used, k)) {
                continue;
            }
            int64_t px = (int64_t)wcs->m_q24[0] * ws->star_x_q4[k] +
                         (int64_t)wcs->m_q24[1] * ws->star_y_q4[k] + (int64_t)wcs->t_q16[0] * 4096;
            int64_t py = (int64_t)wcs->m_q24[2] * ws->star_x_q4[k] +
                         (int64_t)wcs->m_q24[3] * ws->star_y_q4[k] + (int64_t)wcs->t_q16[1] * 4096;
            int32_t x_q4 = (int32_t)((px + (1 << 23)) >> 24);
            int32_t y_q4 = (int32_t)((py + (1 << 23)) >> 24);
            int dx = (x_q4 - ctx->stars[o].x * 16 + 8) >> 4;
            int dy = (y_q4 - ctx->stars[o].y * 16 + 8) >> 4;
            if (abs_int(dx) > EXPORT_MAX_OFFSET || abs_int(dy) > EXPORT_MAX_OFFSET) {
                continue;
            }
            int distance = dx * dx + dy * dy;
            if (distance < best_distance) {
                best = (int)k;
                best_distance = distance;
            }
        }
        ws->association[o] = (int8_t)best;
        if (best >= 0) {
            set_bit(used, (unsigned)best);
            *error += best_distance;
            matches++;
            *mask |= 1u << o;
        }
    }
    return matches;
}

/*
 * Runs once, after acceptance. The search transform is anchored at the index field's
 * tangent point, which can be far from the image centre on wide lenses. Refitting about
 * the centre only improves geometry; acceptance is already decided.
 */
static void export_solution(sky_context *ctx, int parity)
{
    astro_sky_workspace *ws = ctx->ws;
    astro_sky_solution *out = ctx->out;
    int32_t unit = ws->field.unit_mdeg ? (int32_t)ws->field.unit_mdeg : 1;
    astro_wcs field_wcs = {ws->field.ra, ws->field.dec, {0, 0, 0, 0}, {0, 0}};

    for (int i = 0; i < 4; i++) {
        field_wcs.m_q24[i] = ws->matrix_q16[i] * 256 / unit;
    }
    field_wcs.t_q16[0] = ws->translate_q16[0];
    field_wcs.t_q16[1] = ws->translate_q16[1];
    out->wcs = field_wcs;
    if (!astro_wcs_unproject(&field_wcs, ctx->width * 8, ctx->height * 8, &out->ra, &out->dec)) {
        out->ra = ws->field.ra;
        out->dec = ws->field.dec;
    }

    reproject_about_centre(ctx, unit);
    astro_wcs refit = {out->ra, out->dec, {0, 0, 0, 0}, {0, 0}};
    if (fit_wcs(ctx, parity, &refit)) {
        int8_t previous[ASTRO_MAX_STARS];
        int error;
        uint32_t mask;
        astro_wcs again = refit;

        out->wcs = refit;
        memcpy(previous, ws->association, sizeof(previous));
        int matches = reassociate(ctx, &refit, &error, &mask);
        if (matches >= out->matched && fit_wcs(ctx, parity, &again)) {
            out->wcs = again;
            out->matched = matches;
            out->matched_mask = mask;
            out->mse100 = matches ? 100 * error / matches : 0;
        } else {
            memcpy(ws->association, previous, sizeof(previous));
        }
        astro_wcs_unproject(&out->wcs, ctx->width * 8, ctx->height * 8, &out->ra, &out->dec);
    }
    out->fov_cdeg = astro_wcs_fov_cdeg(&out->wcs, ctx->width, ctx->height);
}

/* Loads and sanity-checks a verification field (cached: patterns often share fields).
 * Returns 0 on a read error or a corrupt record. */
static int load_field(sky_context *ctx, uint32_t field_index)
{
    astro_sky_workspace *ws = ctx->ws;
    const sky_field *f = &ws->field;

    if (ws->cached_field != field_index) {
        ctx->out->reads++;
        uint32_t offset = ASTRO_SKY_FILE_HEADER + field_index * ASTRO_SKY_FIELD_BYTES;
        if (!ctx->index->read_fields(ctx->index->fields_context, offset, &ws->field,
                                     ASTRO_SKY_FIELD_BYTES)) {
            return 0;
        }
        ws->cached_field = field_index;
    }
    if (f->count < MIN_FIELD_STARS || f->count > ASTRO_SKY_VERIFY_STARS ||
        f->ra >= ASTRO_FULL_CIRCLE || f->dec < -ASTRO_QUARTER_CIRCLE ||
        f->dec > ASTRO_QUARTER_CIRCLE || f->unit_mdeg > MAX_UNIT_MDEG) {
        return 0;
    }
    return 1;
}

/* Similarity mapping the pattern's baseline onto the image baseline. Returns 0 when the
 * implied scale is outside the accepted range. */
static int hypothesis_from_baseline(sky_context *ctx, const sky_pattern *p, int star_a, int star_b,
                                    int parity)
{
    astro_sky_workspace *ws = ctx->ws;
    const sky_field_star *a = &ws->field.stars[p->a];
    const sky_field_star *b = &ws->field.stars[p->b];
    int64_t dxs = b->x - a->x;
    int64_t dys = b->y - a->y;
    int64_t dxo = ctx->stars[star_b].x - ctx->stars[star_a].x;
    int64_t dyo = ctx->stars[star_b].y - ctx->stars[star_a].y;
    int64_t den = dxs * dxs + dys * dys;

    if (den < MIN_BASELINE_SQ) {
        return 0;
    }
    ws->matrix_q16[0] = (int32_t)((dxo * dxs + parity * dyo * dys) * Q16_ONE / den);
    ws->matrix_q16[1] = (int32_t)((dxo * dys - parity * dyo * dxs) * Q16_ONE / den);
    ws->matrix_q16[2] = (int32_t)((dyo * dxs - parity * dxo * dys) * Q16_ONE / den);
    ws->matrix_q16[3] = (int32_t)((dyo * dys + parity * dxo * dxs) * Q16_ONE / den);

    uint32_t scale = astro_isqrt64((uint64_t)((int64_t)ws->matrix_q16[0] * ws->matrix_q16[0] +
                                              (int64_t)ws->matrix_q16[2] * ws->matrix_q16[2]));
    uint32_t unit = ws->field.unit_mdeg ? ws->field.unit_mdeg : 1;
    if (scale < ws->scale_min * unit || scale > ws->scale_max * unit) {
        return 0;
    }
    ctx->out->checked++;

    /* 64-bit: wide-index coordinates times the matrix can exceed 32 bits. */
    ws->translate_q16[0] =
        (int32_t)((int64_t)ctx->stars[star_a].x * Q16_ONE - (int64_t)ws->matrix_q16[0] * a->x -
                  (int64_t)ws->matrix_q16[1] * a->y);
    ws->translate_q16[1] =
        (int32_t)((int64_t)ctx->stars[star_a].y * Q16_ONE - (int64_t)ws->matrix_q16[2] * a->x -
                  (int64_t)ws->matrix_q16[3] * a->y);
    return 1;
}

static int enough_matches(int matches, uint32_t mask, uint32_t quad_mask)
{
    return matches >= MIN_MATCHES && popcount32(mask & ~quad_mask) >= MIN_INDEPENDENT;
}

static astro_sky_status verify_pattern(sky_context *ctx, const sky_pattern *p, int star_a,
                                       int star_b, int flip, uint32_t quad_mask)
{
    astro_sky_workspace *ws = ctx->ws;
    int error;
    uint32_t mask;

    if (p->field >= ctx->index->nfields || p->a >= SKY_PATTERN_STARS || p->b >= SKY_PATTERN_STARS ||
        p->a == p->b || p->flip > 1) {
        return ASTRO_SKY_NO_MATCH;
    }
    if (!load_field(ctx, p->field)) {
        return ASTRO_SKY_BAD_INDEX;
    }
    int parity = flip == p->flip ? 1 : -1;
    if (!hypothesis_from_baseline(ctx, p, star_a, star_b, parity)) {
        return ASTRO_SKY_NO_MATCH;
    }

    int matches = associate(ctx, FIRST_TOLERANCE_SQ, &error, &mask);
    if (!enough_matches(matches, mask, quad_mask)) {
        return ASTRO_SKY_NO_MATCH;
    }
    for (int pass = 0; pass < 2; pass++) {
        if (!refine(ctx, parity)) {
            return ASTRO_SKY_NO_MATCH;
        }
        matches = associate(ctx, REFINED_TOLERANCE_SQ, &error, &mask);
        if (!enough_matches(matches, mask, quad_mask) || error > 2 * matches) {
            return ASTRO_SKY_NO_MATCH;
        }
    }
    int candidates = ctx->count - popcount32(quad_mask);
    if (!is_significant(ctx, candidates, popcount32(mask & ~quad_mask))) {
        return ASTRO_SKY_NO_MATCH;
    }

    astro_sky_solution *out = ctx->out;
    out->matched = matches;
    out->mse100 = 100 * error / matches;
    out->matched_mask = mask;
    out->field = p->field;
    export_solution(ctx, parity);
    memset(out->names, 0, sizeof(out->names));
    for (int i = 0; i < ctx->count; i++) {
        if (ws->association[i] >= 0) {
            out->names[i] = ws->field.stars[(unsigned)ws->association[i]].name;
        }
    }
    return ASTRO_SKY_SOLVED;
}

/* Verifies the batched candidates of one image quad, best fine-descriptor match first. */
astro_sky_status astro_sky_verify_batch(sky_context *ctx, int star_a, int star_b, int flip,
                                        uint32_t quad_mask)
{
    astro_sky_workspace *ws = ctx->ws;

    for (int error = 0; error <= SKY_MAX_FINE_ERROR; error++) {
        for (unsigned i = 0; i < ws->batch_count; i++) {
            if (ws->batch_error[i] != error) {
                continue;
            }
            if (ctx->progress && ctx->out->checked % PROGRESS_EVERY == 0 &&
                !ctx->progress(ctx->progress_context, ctx->out->quads, ctx->out->checked)) {
                return ASTRO_SKY_CANCELLED;
            }
            astro_sky_status status =
                verify_pattern(ctx, &ws->batch[i], star_a, star_b, flip, quad_mask);
            if (status != ASTRO_SKY_NO_MATCH) {
                return status;
            }
        }
    }
    ws->batch_count = 0;
    return ASTRO_SKY_NO_MATCH;
}
