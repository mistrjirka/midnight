/* SPDX-License-Identifier: GPL-2.0-only */
/* Scene layers read from the card: named stars, deep-sky outlines, constellation figures. */
#include <string.h>
#include "scene_internal.h"

#define MAX_STAR_LABELS    40
#define STAR_RECORD        12
#define STAR_CHUNK         64
#define MAX_STAR_RECORDS   400000
#define STAR_INDEX_HEADER  20
#define STAR_INDEX_VERSION 2
#define STAR_MAG_OFFSET    50 /* STARS.BIN stores (magnitude + 5) * 10 */
#define BRIGHT_STAR_MAG    70 /* stored value; brighter stars get a better label priority */

#define DSO_CHUNK          32
#define DSO_RANK_MESSIER   100
#define DSO_RANK_CALDWELL  200
#define DSO_RANK_MAGNITUDE 1000
#define DSO_RANK_FAINT_CAT 2000 /* Barnard/Sharpless: shown only when visibly large */
#define DSO_RANK_LARGE     3000
#define MIN_FAINT_REACH_Q4 (4 * 16)

#define CONST_SEGMENT       16
#define CONST_SEGMENT_CHUNK 48
#define CONST_LABEL         32
#define CONST_LABEL_CHUNK   16
#define CONST_NAME_OFFSET   12
#define CONST_NAME_BYTES    20
#define MAX_SEGMENTS        4000
#define MAX_CONST_LABELS    200

enum {
    PRIORITY_TARGET = 0,
    PRIORITY_CATALOGUE_DSO = 10,
    PRIORITY_BRIGHT_STAR = 20,
    PRIORITY_CONSTELLATION = 30,
    PRIORITY_OTHER_DSO = 40,
    PRIORITY_STAR = 50
};

typedef struct {
    int16_t x, y;
    uint16_t mag;
    uint16_t label;
} star_pick;

typedef struct {
    int rank;
    astro_dso object;
    astro_ellipse ellipse;
} dso_pick;

static star_pick star_picks[MAX_STAR_LABELS];
static dso_pick dso_picks[ASTRO_SCENE_ELLIPSES];

static int read_header(const astro_file *file, uint8_t *h, unsigned bytes, const char *magic)
{
    return astro_file_read(file, 0, h, bytes) && !memcmp(h, magic, 4);
}

/* Keeps the brightest MAX_STAR_LABELS named stars that fall inside the frame. */
static int pick_star(scene_ctx *ctx, const uint8_t *record, int limit, int count)
{
    int mag = astro_get_u16(record + 8);
    int label = astro_get_u16(record + 10);
    int32_t x, y;

    if (!label || mag > limit || (count == MAX_STAR_LABELS && mag >= star_picks[count - 1].mag)) {
        return count;
    }
    if (!astro_wcs_project(ctx->wcs, astro_get_u32(record), (int32_t)astro_get_u32(record + 4), &x,
                           &y) ||
        !astro_scene_in_frame(ctx, x, y, 0)) {
        return count;
    }
    int k = count < MAX_STAR_LABELS ? count++ : count - 1;
    for (; k > 0 && star_picks[k - 1].mag > mag; k--) {
        star_picks[k] = star_picks[k - 1];
    }
    star_picks[k].x = (int16_t)x;
    star_picks[k].y = (int16_t)y;
    star_picks[k].mag = (uint16_t)mag;
    star_picks[k].label = (uint16_t)label;
    return count;
}

static int label_stars(scene_ctx *ctx, int count)
{
    uint8_t h[16];

    if (!read_header(&ctx->files->stars_nam, h, sizeof(h), "SNAM") ||
        astro_get_u32(h + 12) != ASTRO_NAME_BYTES) {
        return -1;
    }
    uint32_t nlabels = astro_get_u32(h + 8);
    for (int i = 0; i < count; i++) {
        char text[ASTRO_NAME_BYTES];
        if (star_picks[i].label >= nlabels ||
            !astro_file_read(&ctx->files->stars_nam,
                             16 + (uint32_t)star_picks[i].label * ASTRO_NAME_BYTES, text,
                             sizeof(text))) {
            return -1;
        }
        text[ASTRO_NAME_BYTES - 1] = 0;
        if (!memcmp(text, "HIP ", 4)) {
            continue; /* bare catalogue numbers are clutter */
        }
        int priority = star_picks[i].mag < BRIGHT_STAR_MAG ? PRIORITY_BRIGHT_STAR : PRIORITY_STAR;
        astro_scene_add_label(ctx, star_picks[i].x, star_picks[i].y, ASTRO_LABEL_STAR, priority,
                              text);
    }
    return 0;
}

int astro_scene_add_stars(scene_ctx *ctx)
{
    const astro_sky_files *f = ctx->files;
    uint8_t h[STAR_INDEX_HEADER];
    int count = 0;

    if (!f->stars.read || !f->stars_idx.read || !f->stars_nam.read) {
        return 0;
    }
    if (!read_header(&f->stars_idx, h, STAR_INDEX_HEADER, "SIDX") ||
        astro_get_u32(h + 4) != STAR_INDEX_VERSION || astro_get_u32(h + 12) != ASTRO_TILE_RA_BINS ||
        astro_get_u32(h + 16) != ASTRO_TILE_DEC_BINS) {
        return -1;
    }
    uint32_t records = astro_get_u32(h + 8);
    if (records > MAX_STAR_RECORDS ||
        !astro_scene_load_tile_offsets(ctx, &f->stars_idx, STAR_INDEX_HEADER, records)) {
        return -1;
    }
    if (!read_header(&f->stars, h, 16, "ASTR") || astro_get_u32(h + 8) != records ||
        astro_get_u32(h + 12) != STAR_RECORD) {
        return -1;
    }

    int limit = ctx->opts->star_mag10 + STAR_MAG_OFFSET;
    for (int t = 0; t < ctx->ntiles; t++) {
        uint32_t pos = ctx->tile_offsets[ctx->tiles[t]];
        uint32_t end = ctx->tile_offsets[ctx->tiles[t] + 1];
        while (pos < end) {
            uint32_t take = end - pos > STAR_CHUNK ? STAR_CHUNK : end - pos;
            if (!astro_file_read(&f->stars, 16 + pos * STAR_RECORD, ctx->chunk,
                                 take * STAR_RECORD)) {
                return -1;
            }
            for (uint32_t i = 0; i < take; i++) {
                count = pick_star(ctx, ctx->chunk + i * STAR_RECORD, limit, count);
            }
            pos += take;
        }
    }
    return label_stars(ctx, count);
}

/* Lower rank draws first. -1: do not draw. */
static int dso_rank(const astro_dso *o, const astro_overlay_opts *opts)
{
    if (o->catalogue == ASTRO_CAT_M) {
        return DSO_RANK_MESSIER;
    }
    if (o->catalogue == ASTRO_CAT_C) {
        return DSO_RANK_CALDWELL;
    }
    if (o->mag10 != ASTRO_MAG_UNKNOWN && o->mag10 <= opts->dso_mag10) {
        return DSO_RANK_MAGNITUDE + o->mag10;
    }
    if (o->catalogue == ASTRO_CAT_SH2 || o->catalogue == ASTRO_CAT_B) {
        int capped = o->major_axis > DSO_RANK_FAINT_CAT ? DSO_RANK_FAINT_CAT : o->major_axis;
        return DSO_RANK_LARGE - capped;
    }
    return -1;
}

static int is_target(const scene_ctx *ctx, const astro_dso *o)
{
    const astro_target *t = ctx->target;
    return t && t->valid && o->ra == t->object.ra && o->dec == t->object.dec;
}

static int pick_dso(scene_ctx *ctx, const astro_dso *o, int count)
{
    int rank = dso_rank(o, ctx->opts);
    astro_ellipse e;
    int32_t x, y;

    if (rank < 0 || (count == ASTRO_SCENE_ELLIPSES && rank >= dso_picks[count - 1].rank) ||
        is_target(ctx, o)) {
        return count; /* the target gets its own outline */
    }
    if (!astro_wcs_project(ctx->wcs, o->ra, o->dec, &x, &y) ||
        !astro_scene_ellipse_axes(ctx, o, &e)) {
        return count;
    }
    int32_t reach = (e.ax < 0 ? -e.ax : e.ax) + (e.ay < 0 ? -e.ay : e.ay);
    if (!astro_scene_in_frame(ctx, x, y, reach) ||
        (rank >= DSO_RANK_FAINT_CAT && reach < MIN_FAINT_REACH_Q4)) {
        return count;
    }
    e.x = (int16_t)x;
    e.y = (int16_t)y;
    e.kind = ASTRO_LABEL_DSO;
    e.pad = 0;

    int k = count < ASTRO_SCENE_ELLIPSES ? count++ : count - 1;
    for (; k > 0 && dso_picks[k - 1].rank > rank; k--) {
        dso_picks[k] = dso_picks[k - 1];
    }
    dso_picks[k].rank = rank;
    dso_picks[k].object = *o;
    dso_picks[k].ellipse = e;
    return count;
}

int astro_scene_add_dso(scene_ctx *ctx)
{
    const astro_file *file = &ctx->files->dso;
    astro_scene *s = ctx->scene;
    int count = 0;

    if (!file->read) {
        return 0;
    }
    int records = astro_dso_count(ctx->files);
    if (records < 0 || !astro_scene_load_tile_offsets(ctx, file, 24, (uint32_t)records)) {
        return -1;
    }
    for (int t = 0; t < ctx->ntiles; t++) {
        uint32_t pos = ctx->tile_offsets[ctx->tiles[t]];
        uint32_t end = ctx->tile_offsets[ctx->tiles[t] + 1];
        while (pos < end) {
            uint32_t take = end - pos > DSO_CHUNK ? DSO_CHUNK : end - pos;
            if (!astro_file_read(file, ASTRO_DSO_HEADER_BYTES + pos * ASTRO_DSO_RECORD_BYTES,
                                 ctx->chunk, take * ASTRO_DSO_RECORD_BYTES)) {
                return -1;
            }
            for (uint32_t i = 0; i < take; i++) {
                astro_dso o;
                astro_dso_decode(ctx->chunk + i * ASTRO_DSO_RECORD_BYTES, &o);
                count = pick_dso(ctx, &o, count);
            }
            pos += take;
        }
    }

    for (int i = 0; i < count && s->nellipses < ASTRO_SCENE_ELLIPSES; i++) {
        const dso_pick *p = &dso_picks[i];
        char text[ASTRO_NAME_BYTES];
        s->ellipses[s->nellipses++] = p->ellipse;
        astro_designation(p->object.catalogue, p->object.number, text, sizeof(text));
        if (astro_scene_in_frame(ctx, p->ellipse.x, p->ellipse.y, 0)) {
            int priority =
                p->rank < DSO_RANK_MAGNITUDE ? PRIORITY_CATALOGUE_DSO : PRIORITY_OTHER_DSO;
            astro_scene_add_label(ctx, p->ellipse.x, p->ellipse.y, ASTRO_LABEL_DSO, priority, text);
        }
    }
    return 0;
}

static int add_segments(scene_ctx *ctx, uint32_t nsegments)
{
    const astro_file *file = &ctx->files->constellations;

    for (uint32_t pos = 0; pos < nsegments; pos += CONST_SEGMENT_CHUNK) {
        uint32_t take =
            nsegments - pos > CONST_SEGMENT_CHUNK ? CONST_SEGMENT_CHUNK : nsegments - pos;
        if (!astro_file_read(file, 16 + pos * CONST_SEGMENT, ctx->chunk, take * CONST_SEGMENT)) {
            return -1;
        }
        for (uint32_t i = 0; i < take; i++) {
            const uint8_t *seg = ctx->chunk + i * CONST_SEGMENT;
            int32_t x1, y1, x2, y2;
            if (astro_wcs_project(ctx->wcs, astro_get_u32(seg), (int32_t)astro_get_u32(seg + 4),
                                  &x1, &y1) &&
                astro_wcs_project(ctx->wcs, astro_get_u32(seg + 8),
                                  (int32_t)astro_get_u32(seg + 12), &x2, &y2)) {
                astro_scene_add_line(ctx, x1, y1, x2, y2);
            }
        }
    }
    return 0;
}

static int add_constellation_names(scene_ctx *ctx, uint32_t nsegments, uint32_t nlabels)
{
    const astro_file *file = &ctx->files->constellations;
    uint32_t base = 16 + nsegments * CONST_SEGMENT;

    for (uint32_t pos = 0; pos < nlabels; pos += CONST_LABEL_CHUNK) {
        uint32_t take = nlabels - pos > CONST_LABEL_CHUNK ? CONST_LABEL_CHUNK : nlabels - pos;
        if (!astro_file_read(file, base + pos * CONST_LABEL, ctx->chunk, take * CONST_LABEL)) {
            return -1;
        }
        for (uint32_t i = 0; i < take; i++) {
            const uint8_t *label = ctx->chunk + i * CONST_LABEL;
            char name[CONST_NAME_BYTES + 1];
            int32_t x, y;
            memcpy(name, label + CONST_NAME_OFFSET, CONST_NAME_BYTES);
            name[CONST_NAME_BYTES] = 0;
            if (astro_wcs_project(ctx->wcs, astro_get_u32(label), (int32_t)astro_get_u32(label + 4),
                                  &x, &y) &&
                astro_scene_in_frame(ctx, x, y, 0)) {
                astro_scene_add_label(ctx, x, y, ASTRO_LABEL_CONSTELLATION, PRIORITY_CONSTELLATION,
                                      name);
            }
        }
    }
    return 0;
}

int astro_scene_add_constellations(scene_ctx *ctx)
{
    const astro_file *file = &ctx->files->constellations;
    uint8_t h[16];

    if (!file->read) {
        return 0;
    }
    if (!read_header(file, h, sizeof(h), "SCON") || astro_get_u32(h + 4) != 1 ||
        astro_get_u32(h + 8) > MAX_SEGMENTS || astro_get_u32(h + 12) > MAX_CONST_LABELS) {
        return -1;
    }
    uint32_t nsegments = astro_get_u32(h + 8);
    if (add_segments(ctx, nsegments) < 0) {
        return -1;
    }
    return add_constellation_names(ctx, nsegments, astro_get_u32(h + 12));
}
