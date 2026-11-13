/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Focus score from a star's point spread function: the flux-weighted mean squared radius.
 * Hot pixels and cosmic-ray hits are rejected because real stars cover several pixels.
 */
#include "focus_metric.h"

/* Focus-star detector. */
#define FIND_BORDER         4
#define FIND_MIN_ABOVE_BG   12
#define FIND_MIN_LEVEL      15
#define FIND_NEIGHBOUR_BG   4 /* neighbour counts as lit above bg + 4 */
#define FIND_MIN_NEIGHBOURS 2
#define FIND_APERTURE       3 /* 7x7 centroid box */
#define FIND_FLUX_BIAS      3
#define FIND_MIN_FLUX       28
#define DUPLICATE_DISTANCE  5

/* Locked metric. */
#define LOCK_BORDER        8
#define LOCK_MAX_RADIUS    15
#define PEAK_MIN_ABOVE_BG  18
#define PEAK_MIN_LEVEL     24
#define PEAK_CUT_FRACTION  5 /* lit neighbour: above bg + (peak - bg) / 5 */
#define PEAK_MIN_LIT       3 /* of the 8 neighbours */
#define APERTURE_MIN       4
#define APERTURE_MAX       14
#define APERTURE_MARGIN    17 /* APERTURE_MAX + 1 extra ring + 2 */
#define MOMENT_BIAS        4
#define WING_FADE_PCT      7 /* ring mean below 7 % of peak: star has faded */
#define MIN_STAR_FLUX      65
#define MAX_CENTROID_SHIFT 4

/* Initial star search: central 3/5 of the frame. */
#define CENTRAL_FRACTION 5
#define INITIAL_LOCK     3

static int abs_int(int x)
{
    return x < 0 ? -x : x;
}

/* Strict 5x5 maximum (plateau: first in scan order) with at least two lit neighbours. */
static int is_focus_peak(const uint8_t *image, int stride, int x, int y, int background)
{
    int peak = image[y * stride + x];
    int lit = 0;

    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            if (!dx && !dy) {
                continue;
            }
            int p = image[(y + dy) * stride + x + dx];
            int earlier = dy < 0 || (dy == 0 && dx < 0);
            if (p > peak || (p == peak && earlier)) {
                return 0;
            }
            if (p > background + FIND_NEIGHBOUR_BG) {
                lit++;
            }
        }
    }
    return lit >= FIND_MIN_NEIGHBOURS;
}

/* Background-subtracted centroid over a 7x7 box. Returns the flux. */
static int box_centroid(const uint8_t *image, int stride, int x, int y, int background, int *cx,
                        int *cy)
{
    int flux = 0;
    int wx = 0;
    int wy = 0;

    for (int dy = -FIND_APERTURE; dy <= FIND_APERTURE; dy++) {
        for (int dx = -FIND_APERTURE; dx <= FIND_APERTURE; dx++) {
            int p = (int)image[(y + dy) * stride + x + dx] - background - FIND_FLUX_BIAS;
            if (p > 0) {
                flux += p;
                wx += (x + dx) * p;
                wy += (y + dy) * p;
            }
        }
    }
    if (flux > 0) {
        *cx = (wx + flux / 2) / flux;
        *cy = (wy + flux / 2) / flux;
    }
    return flux;
}

static void bubble_up(astro_star *stars, int i)
{
    for (; i > 0 && stars[i].flux > stars[i - 1].flux; i--) {
        astro_star tmp = stars[i];
        stars[i] = stars[i - 1];
        stars[i - 1] = tmp;
    }
}

/* Adds a star, or strengthens an existing one within 5 px. Keeps brightest-first order. */
static int add_focus_star(astro_star *stars, int count, int max_stars, int x, int y, int flux)
{
    uint16_t flux16 = (uint16_t)(flux < 65535 ? flux : 65535);

    for (int i = 0; i < count; i++) {
        if (abs_int(stars[i].x - x) <= DUPLICATE_DISTANCE &&
            abs_int(stars[i].y - y) <= DUPLICATE_DISTANCE) {
            if (flux16 > stars[i].flux) {
                stars[i].x = (int16_t)x;
                stars[i].y = (int16_t)y;
                stars[i].flux = flux16;
                bubble_up(stars, i);
            }
            return count;
        }
    }
    if (count < max_stars) {
        count++;
    } else if (flux16 <= stars[count - 1].flux) {
        return count;
    }
    stars[count - 1].x = (int16_t)x;
    stars[count - 1].y = (int16_t)y;
    stars[count - 1].flux = flux16;
    bubble_up(stars, count - 1);
    return count;
}

int astro_find_focus_stars(const uint8_t *image, int width, int height, int stride, astro_star *out,
                           int max_stars)
{
    int count = 0;

    if (!image || !out || width < 32 || height < 32 || stride < width || max_stars < 1) {
        return 0;
    }
    if (max_stars > ASTRO_MAX_STARS) {
        max_stars = ASTRO_MAX_STARS;
    }
    int background = astro_sky_background(image, width, height, stride);

    for (int y = FIND_BORDER; y < height - FIND_BORDER; y++) {
        for (int x = FIND_BORDER; x < width - FIND_BORDER; x++) {
            int peak = image[y * stride + x];
            int cx = x;
            int cy = y;
            if (peak < background + FIND_MIN_ABOVE_BG || peak < FIND_MIN_LEVEL ||
                !is_focus_peak(image, stride, x, y, background)) {
                continue;
            }
            int flux = box_centroid(image, stride, x, y, background, &cx, &cy);
            if (flux >= FIND_MIN_FLUX) {
                count = add_focus_star(out, count, max_stars, cx, cy, flux);
            }
        }
    }
    return count;
}

/* Brightest extended peak within radius of (ox, oy); ties go to the closest. */
static int find_locked_peak(const uint8_t *image, int width, int height, int stride, int ox, int oy,
                            int radius, int background, int *px, int *py)
{
    int best_rank = -1;
    int best_distance = 0;

    for (int y = oy - radius; y <= oy + radius; y++) {
        for (int x = ox - radius; x <= ox + radius; x++) {
            if (y < LOCK_BORDER || y >= height - LOCK_BORDER || x < LOCK_BORDER ||
                x >= width - LOCK_BORDER) {
                continue;
            }
            int v = image[y * stride + x];
            if (v < background + PEAK_MIN_ABOVE_BG || v < PEAK_MIN_LEVEL) {
                continue;
            }
            int cut = background + (v - background) / PEAK_CUT_FRACTION;
            int lit = 0;
            int is_max = 1;
            for (int dy = -1; dy <= 1 && is_max; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    int t = image[(y + dy) * stride + x + dx];
                    if ((dx || dy) && t > v) {
                        is_max = 0;
                        break;
                    }
                    lit += (dx || dy) && t >= cut;
                }
            }
            int rank = v - background;
            int distance = abs_int(x - ox) + abs_int(y - oy);
            if (!is_max || lit < PEAK_MIN_LIT) {
                continue;
            }
            if (rank > best_rank || (rank == best_rank && distance < best_distance)) {
                best_rank = rank;
                best_distance = distance;
                *px = x;
                *py = y;
            }
        }
    }
    return best_rank;
}

/* Aperture radius from the radial profile: grow until the ring fades, plus one ring for
 * the wings. Adapts to focal length and defocus instead of a fixed aperture. */
static int aperture_radius(const uint8_t *image, int stride, int px, int py, int background,
                           int peak)
{
    int outer = APERTURE_MIN;

    for (int radius = APERTURE_MIN; radius <= APERTURE_MAX; radius++) {
        int64_t ring_sum = 0;
        int ring_pixels = 0;
        for (int dy = -radius; dy <= radius; dy++) {
            for (int dx = -radius; dx <= radius; dx++) {
                int r2 = dx * dx + dy * dy;
                if (r2 > radius * radius || r2 <= (radius - 1) * (radius - 1)) {
                    continue;
                }
                int v = (int)image[(py + dy) * stride + px + dx] - background - MOMENT_BIAS;
                if (v > 0) {
                    ring_sum += v;
                }
                ring_pixels++;
            }
        }
        outer = radius;
        if (radius >= APERTURE_MIN + 1 &&
            ring_sum * 100 < (int64_t)ring_pixels * peak * WING_FADE_PCT) {
            if (outer < APERTURE_MAX) {
                outer++;
            }
            break;
        }
    }
    return outer;
}

static int64_t round_div(int64_t num, int64_t den)
{
    return (num + (num >= 0 ? den / 2 : -den / 2)) / den;
}

int astro_focus_metric_locked(const uint8_t *image, int width, int height, int stride, int *star_x,
                              int *star_y, int radius)
{
    int px = -1;
    int py = -1;

    if (!image || !star_x || !star_y || width < 32 || height < 32 || stride < width || radius < 1 ||
        radius > LOCK_MAX_RADIUS || *star_x < LOCK_BORDER || *star_x >= width - LOCK_BORDER ||
        *star_y < LOCK_BORDER || *star_y >= height - LOCK_BORDER) {
        return -1;
    }
    int background = astro_sky_background(image, width, height, stride);
    int ox = *star_x;
    int oy = *star_y;
    int peak = find_locked_peak(image, width, height, stride, ox, oy, radius, background, &px, &py);
    if (px < 0) {
        return -1;
    }
    if (px < APERTURE_MARGIN || px >= width - APERTURE_MARGIN || py < APERTURE_MARGIN ||
        py >= height - APERTURE_MARGIN) {
        return -1;
    }

    int outer = aperture_radius(image, stride, px, py, background, peak);
    int64_t flux = 0, r2_sum = 0, wx = 0, wy = 0;
    for (int dy = -outer; dy <= outer; dy++) {
        for (int dx = -outer; dx <= outer; dx++) {
            int r2 = dx * dx + dy * dy;
            int v = (int)image[(py + dy) * stride + px + dx] - background - MOMENT_BIAS;
            if (r2 > outer * outer || v <= 0) {
                continue;
            }
            flux += v;
            wx += (int64_t)dx * v;
            wy += (int64_t)dy * v;
            r2_sum += (int64_t)v * r2;
        }
    }
    if (flux < MIN_STAR_FLUX || peak < PEAK_MIN_ABOVE_BG) {
        return -1;
    }

    int cx = px + (int)round_div(wx, flux);
    int cy = py + (int)round_div(wy, flux);
    if (abs_int(cx - ox) > radius + 2 || abs_int(cy - oy) > radius + 2 ||
        abs_int(cx - px) > MAX_CENTROID_SHIFT || abs_int(cy - py) > MAX_CENTROID_SHIFT) {
        return -1;
    }
    *star_x = cx;
    *star_y = cy;
    return (int)(r2_sum * 16 / flux);
}

int astro_focus_metric(const uint8_t *image, int width, int height, int stride, int *star_x,
                       int *star_y)
{
    astro_star stars[ASTRO_MAX_STARS];

    if (!image || !star_x || !star_y || width < 32 || height < 32 || stride < width) {
        return -1;
    }
    int count = astro_find_focus_stars(image, width, height, stride, stars, ASTRO_MAX_STARS);

    /* Pick the strongest stellar source in the central area. Autofocus then stays locked
     * to it via astro_focus_metric_locked. */
    for (int i = 0; i < count; i++) {
        int x = stars[i].x;
        int y = stars[i].y;
        if (x < width / CENTRAL_FRACTION || x >= width - width / CENTRAL_FRACTION ||
            y < height / CENTRAL_FRACTION || y >= height - height / CENTRAL_FRACTION) {
            continue;
        }
        int metric = astro_focus_metric_locked(image, width, height, stride, &x, &y, INITIAL_LOCK);
        if (metric >= 0) {
            *star_x = x;
            *star_y = y;
            return metric;
        }
    }
    return -1;
}
