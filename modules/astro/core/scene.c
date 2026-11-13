/* SPDX-License-Identifier: GPL-2.0-only */
/* Scene geometry: frame extent, sky tiles, clipped lines, ellipses, labels and target. */
#include <string.h>
#include "scene_internal.h"

#define FRAME_MARGIN_1E4DEG   10000 /* tiles one degree beyond the farthest corner */
#define NEAR_POLE_1E4DEG      890000
#define ARCMIN10_PER_SEMI_DEG 1200 /* 0.1 arcmin full axis -> semi-axis in degrees */
#define Q4_LIMIT              32000
#define CLIP_GUARD            8

/* Builder scratch (~17 KB). Static: scenes are built by the single worker task only. */
static struct {
    uint32_t tile_offsets[ASTRO_TILES + 1];
    uint16_t tiles[ASTRO_TILES];
    uint8_t chunk[SCENE_CHUNK_BYTES];
} work;

int astro_scene_in_frame(const scene_ctx *ctx, int32_t x_q4, int32_t y_q4, int32_t margin_q4)
{
    const astro_scene *s = ctx->scene;
    return x_q4 >= -margin_q4 && y_q4 >= -margin_q4 && x_q4 < s->width * 16 + margin_q4 &&
           y_q4 < s->height * 16 + margin_q4;
}

/* Inserts a label by priority. When full, it replaces the least important one. */
void astro_scene_add_label(scene_ctx *ctx, int32_t x_q4, int32_t y_q4, int kind, int priority,
                           const char *text)
{
    astro_scene *s = ctx->scene;
    astro_label *label;

    if (!text[0]) {
        return;
    }
    if (s->nlabels < ASTRO_SCENE_LABELS) {
        label = &s->labels[s->nlabels++];
    } else {
        label = &s->labels[ASTRO_SCENE_LABELS - 1];
        if (label->priority <= priority) {
            return;
        }
    }
    label->x = (int16_t)x_q4;
    label->y = (int16_t)y_q4;
    label->kind = (uint8_t)kind;
    label->priority = (uint8_t)priority;
    strncpy(label->text, text, sizeof(label->text) - 1);
    label->text[sizeof(label->text) - 1] = 0;

    /* Stable insertion: move it ahead of strictly less important labels. */
    for (int i = (int)(label - s->labels);
         i > 0 && s->labels[i - 1].priority > s->labels[i].priority; i--) {
        astro_label tmp = s->labels[i];
        s->labels[i] = s->labels[i - 1];
        s->labels[i - 1] = tmp;
    }
}

/* Cohen-Sutherland region code against the frame, Q4. */
static int outcode(const astro_scene *s, int64_t x, int64_t y)
{
    return (x < 0) | ((x >= s->width * 16) << 1) | ((y < 0) << 2) | ((y >= s->height * 16) << 3);
}

void astro_scene_add_line(scene_ctx *ctx, int64_t x1, int64_t y1, int64_t x2, int64_t y2)
{
    astro_scene *s = ctx->scene;
    int64_t xmax = s->width * 16 - 1;
    int64_t ymax = s->height * 16 - 1;
    int c1 = outcode(s, x1, y1);
    int c2 = outcode(s, x2, y2);

    for (int guard = 0; guard < CLIP_GUARD && (c1 | c2); guard++) {
        if (c1 & c2) {
            return; /* entirely on one outside side */
        }
        int c = c1 ? c1 : c2;
        int64_t x, y;
        if (c & 8) {
            x = x1 + (x2 - x1) * (ymax - y1) / (y2 - y1);
            y = ymax;
        } else if (c & 4) {
            x = x1 + (x2 - x1) * (0 - y1) / (y2 - y1);
            y = 0;
        } else if (c & 2) {
            y = y1 + (y2 - y1) * (xmax - x1) / (x2 - x1);
            x = xmax;
        } else {
            y = y1 + (y2 - y1) * (0 - x1) / (x2 - x1);
            x = 0;
        }
        if (c == c1) {
            x1 = x;
            y1 = y;
            c1 = outcode(s, x1, y1);
        } else {
            x2 = x;
            y2 = y;
            c2 = outcode(s, x2, y2);
        }
    }
    if ((c1 | c2) || s->nlines >= ASTRO_SCENE_LINES) {
        return;
    }
    astro_line *line = &s->lines[s->nlines++];
    line->x1 = (int16_t)x1;
    line->y1 = (int16_t)y1;
    line->x2 = (int16_t)x2;
    line->y2 = (int16_t)y2;
}

static int16_t clamp_q4(int64_t v)
{
    if (v > Q4_LIMIT) {
        return Q4_LIMIT;
    }
    return (int16_t)(v < -Q4_LIMIT ? -Q4_LIMIT : v);
}

/* Semi-axis vectors (Q4) of an object's outline, oriented by its position angle
 * (measured from north through east). */
int astro_scene_ellipse_axes(const scene_ctx *ctx, const astro_dso *object, astro_ellipse *out)
{
    int32_t north[2], east[2];
    int32_t sin_pa, cos_pa;

    if (!astro_wcs_axes(ctx->wcs, object->ra, object->dec, north, east)) {
        return 0;
    }
    int32_t pa = object->position_angle >= 0 ? object->position_angle : 0;
    astro_sincos(pa * 10000, &sin_pa, &cos_pa);

    int64_t ux = ((int64_t)north[0] * cos_pa + (int64_t)east[0] * sin_pa) >> 30;
    int64_t uy = ((int64_t)north[1] * cos_pa + (int64_t)east[1] * sin_pa) >> 30;
    int64_t vx = ((int64_t)east[0] * cos_pa - (int64_t)north[0] * sin_pa) >> 30;
    int64_t vy = ((int64_t)east[1] * cos_pa - (int64_t)north[1] * sin_pa) >> 30;
    int major = object->major_axis;
    int minor = object->minor_axis ? object->minor_axis : major;

    out->ax = clamp_q4(ux * major / ARCMIN10_PER_SEMI_DEG);
    out->ay = clamp_q4(uy * major / ARCMIN10_PER_SEMI_DEG);
    out->bx = clamp_q4(vx * minor / ARCMIN10_PER_SEMI_DEG);
    out->by = clamp_q4(vy * minor / ARCMIN10_PER_SEMI_DEG);
    return 1;
}

static int32_t abs32(int32_t v)
{
    return v < 0 ? -v : v;
}

/* Collects the 5-degree tiles that may overlap a sky circle. */
static int frame_tiles(uint32_t ra, int32_t dec, uint32_t radius, uint16_t *tiles)
{
    int count = 0;
    int32_t lo = dec - (int32_t)radius;
    int32_t hi = dec + (int32_t)radius;

    if (lo < -ASTRO_QUARTER_CIRCLE) {
        lo = -ASTRO_QUARTER_CIRCLE;
    }
    if (hi > ASTRO_QUARTER_CIRCLE) {
        hi = ASTRO_QUARTER_CIRCLE;
    }

    /* RA half-width grows as 1/cos(dec) at the declination farthest from the equator. */
    int32_t worst = abs32(lo) > abs32(hi) ? abs32(lo) : abs32(hi);
    int64_t half_width = ASTRO_FULL_CIRCLE;
    if (worst < NEAR_POLE_1E4DEG) {
        int32_t sin_d, cos_d;
        astro_sincos(worst, &sin_d, &cos_d);
        half_width = ((int64_t)radius << 30) / cos_d + ASTRO_TILE_SIZE;
    }
    int all_ra = half_width >= ASTRO_HALF_CIRCLE;

    int first_band = (lo + ASTRO_QUARTER_CIRCLE) / ASTRO_TILE_SIZE;
    int last_band = (hi + ASTRO_QUARTER_CIRCLE) / ASTRO_TILE_SIZE;
    for (int band = first_band; band <= last_band && band < ASTRO_TILE_DEC_BINS; band++) {
        for (int column = 0; column < ASTRO_TILE_RA_BINS; column++) {
            if (!all_ra) {
                int32_t d = column * ASTRO_TILE_SIZE + ASTRO_TILE_SIZE / 2 - (int32_t)ra;
                d = abs32(d);
                if (d > ASTRO_HALF_CIRCLE) {
                    d = ASTRO_FULL_CIRCLE - d;
                }
                if (d > half_width + ASTRO_TILE_SIZE / 2) {
                    continue;
                }
            }
            tiles[count++] = (uint16_t)(band * ASTRO_TILE_RA_BINS + column);
        }
    }
    return count;
}

/* Tile offset table: TILES + 1 monotonic record indices, first 0, last == count. */
int astro_scene_load_tile_offsets(scene_ctx *ctx, const astro_file *file, uint32_t offset,
                                  uint32_t count)
{
    uint32_t *t = ctx->tile_offsets;

    if (!astro_file_read(file, offset, t, (ASTRO_TILES + 1) * sizeof(uint32_t)) || t[0] ||
        t[ASTRO_TILES] != count) {
        return 0;
    }
    for (int i = 0; i < ASTRO_TILES; i++) {
        if (t[i] > t[i + 1]) {
            return 0;
        }
    }
    return 1;
}

static void add_target(scene_ctx *ctx)
{
    astro_scene *s = ctx->scene;
    const astro_target *t = ctx->target;
    astro_ellipse e;
    int32_t x, y;

    memcpy(s->target_label, t->label, sizeof(s->target_label));
    s->target_label[sizeof(s->target_label) - 1] = 0;
    if (!astro_wcs_project(ctx->wcs, t->object.ra, t->object.dec, &x, &y) ||
        !astro_scene_in_frame(ctx, x, y, 0)) {
        s->target = ASTRO_TARGET_OUTSIDE;
        return;
    }
    s->target = ASTRO_TARGET_IN_FRAME;
    s->target_x = (int16_t)x;
    s->target_y = (int16_t)y;
    if (astro_scene_ellipse_axes(ctx, &t->object, &e) && s->nellipses < ASTRO_SCENE_ELLIPSES) {
        e.x = (int16_t)x;
        e.y = (int16_t)y;
        e.kind = ASTRO_LABEL_TARGET;
        e.pad = 0;
        s->ellipses[s->nellipses++] = e;
    }
    astro_scene_add_label(ctx, x, y, ASTRO_LABEL_TARGET, 0, t->label);
}

/* Angular radius of the frame around its centre: the farthest corner. */
static int frame_radius(const astro_wcs *wcs, int width, int height, uint32_t ra, int32_t dec,
                        uint32_t *radius)
{
    *radius = 0;
    for (int corner = 0; corner < 4; corner++) {
        uint32_t corner_ra;
        int32_t corner_dec;
        if (!astro_wcs_unproject(wcs, (corner & 1) * width * 16, (corner >> 1) * height * 16,
                                 &corner_ra, &corner_dec)) {
            return 0;
        }
        uint32_t separation = astro_separation(ra, dec, corner_ra, corner_dec);
        if (separation > *radius) {
            *radius = separation;
        }
    }
    return 1;
}

int astro_build_scene(const astro_wcs *wcs, int width, int height, const astro_overlay_opts *opts,
                      const astro_target *target, const astro_sky_files *files, astro_scene *scene)
{
    scene_ctx ctx = {scene, wcs, files, opts, target, work.tiles, 0, work.tile_offsets, work.chunk};
    uint32_t ra, radius;
    int32_t dec;
    int status = 0;

    memset(scene, 0, sizeof(*scene));
    scene->width = width;
    scene->height = height;
    if (!astro_wcs_unproject(wcs, width * 8, height * 8, &ra, &dec) ||
        !frame_radius(wcs, width, height, ra, dec, &radius)) {
        return -1;
    }
    radius += FRAME_MARGIN_1E4DEG;

    if (target && target->valid) {
        int32_t dx, dy;
        add_target(&ctx);
        scene->target_separation = astro_separation(ra, dec, target->object.ra, target->object.dec);
        if (astro_wcs_direction(wcs, ra, dec, target->object.ra, target->object.dec, &dx, &dy)) {
            scene->target_dx = (int16_t)dx;
            scene->target_dy = (int16_t)dy;
        }
    }

    ctx.ntiles = frame_tiles(ra, dec, radius, work.tiles);
    if (opts->dso && astro_scene_add_dso(&ctx) < 0) {
        status = -1;
    }
    if (opts->constellations && astro_scene_add_constellations(&ctx) < 0) {
        status = -1;
    }
    if (opts->stars && astro_scene_add_stars(&ctx) < 0) {
        status = -1;
    }
    return status;
}
