/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Trail detection: Hough voting over bright pixels, then an integer line fit. A line is
 * masked only when it is both long and continuous, so ordinary star fields pass untouched.
 */
#include <string.h>
#include "streak.h"
#include "star.h"

#define THETA_BINS 48 /* 3.75 degree steps over 180 degrees */
#define RHO_BINS   256
#define RHO_STEP   4   /* pixels per rho bin */
#define RHO_OFFSET 512 /* shifts negative rho into the bin range */
#define Q10        1024

#define MAX_ATTEMPTS      6
#define MIN_VOTES         24
#define FIT_BAND          10 /* pixels around the Hough line used for the fit */
#define MIN_FIT_PIXELS    30
#define MAX_SLOPE_Q10     (10 * Q10)
#define MIN_LENGTH        100
#define SUPPORT_STEP      4
#define SUPPORT_HALFWIDTH 2
#define MIN_SUPPORT_PCT   65
#define EDGE_MARGIN       3
#define MASK_EXTEND       5
#define MASK_HALFWIDTH    6
#define MAX_THRESHOLD     225

/* A trail is a thin line: with its stars it lights about 1.4 % of a frame. More bright pixels
 * than this means the threshold sits inside sky glow, vignetting or a dense Milky Way, where
 * any line through the bright area looks "continuous". */
#define MAX_BRIGHT_PERCENT 3

/* Bright-pixel thresholds above background, tried from strict to loose. */
static const int contrast_levels[] = {80, 40, 20};

static const int16_t hough_cos_q10[THETA_BINS] = {
    1024, 1022, 1015, 1004, 989,  970,  946,  918,  887,  852,   812,   769,
    724,  675,  623,  568,  512,  453,  392,  329,  265,  200,   134,   67,
    0,    -67,  -134, -200, -265, -329, -392, -453, -512, -568,  -623,  -675,
    -724, -769, -812, -852, -887, -918, -946, -970, -989, -1004, -1015, -1022,
};
static const int16_t hough_sin_q10[THETA_BINS] = {
    0,   67,  134, 200, 265, 329,  392,  453,  512,  568,  623,  675,  724, 769, 812, 852,
    887, 918, 946, 970, 989, 1004, 1015, 1022, 1024, 1022, 1015, 1004, 989, 970, 946, 918,
    887, 852, 812, 769, 724, 675,  623,  568,  512,  453,  392,  329,  265, 200, 134, 67,
};

/* 24 KB vote table: static because solving runs in a single worker task. */
static uint16_t votes[THETA_BINS * RHO_BINS];

/* Line in the form minor = (slope * major + intercept) / 1024, where major is y for
 * steep lines and x otherwise. */
typedef struct {
    int valid;
    int vertical;
    int slope_q10;
    int intercept_q10;
    int min_major;
    int max_major;
} streak_line;

typedef struct {
    const uint8_t *image;
    int width;
    int height;
    int stride;
    int threshold;
} bright_image;

static int abs_int(int x)
{
    return x < 0 ? -x : x;
}

static int64_t abs64(int64_t x)
{
    return x < 0 ? -x : x;
}

static int rho_of(int x, int y, const bright_image *im, int theta)
{
    return ((x - im->width / 2) * hough_cos_q10[theta] +
            (y - im->height / 2) * hough_sin_q10[theta]) /
           Q10;
}

/* Fills the Hough table from every 2nd pixel. Returns the share of those pixels that are bright,
 * in percent. */
static int vote(const bright_image *im)
{
    int sampled = 0;
    int bright = 0;

    memset(votes, 0, sizeof(votes));
    for (int y = 1; y < im->height - 1; y += 2) {
        for (int x = 1; x < im->width - 1; x += 2) {
            sampled++;
            if (im->image[y * im->stride + x] < im->threshold) {
                continue;
            }
            bright++;
            for (int t = 0; t < THETA_BINS; t++) {
                int bin = (rho_of(x, y, im, t) + RHO_OFFSET) / RHO_STEP;
                if ((unsigned)bin < RHO_BINS && votes[t * RHO_BINS + bin] < 65535) {
                    votes[t * RHO_BINS + bin]++;
                }
            }
        }
    }
    return bright * 100 / sampled;
}

/* Takes the strongest remaining Hough bin. Returns its vote count. */
static int take_peak(int *theta, int *rho)
{
    int best = 0;
    int best_index = 0;

    for (int i = 0; i < THETA_BINS * RHO_BINS; i++) {
        if (votes[i] > best) {
            best = votes[i];
            best_index = i;
        }
    }
    votes[best_index] = 0;
    *theta = best_index / RHO_BINS;
    *rho = (best_index % RHO_BINS) * RHO_STEP - RHO_OFFSET;
    return best;
}

/* Least-squares line through bright pixels near the Hough line. */
static int fit_line(const bright_image *im, int theta, int rho, streak_line *line)
{
    int64_t n = 0, sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0;

    for (int y = 1; y < im->height - 1; y += 2) {
        for (int x = 1; x < im->width - 1; x += 2) {
            if (im->image[y * im->stride + x] < im->threshold ||
                abs_int(rho_of(x, y, im, theta) - rho) > FIT_BAND) {
                continue;
            }
            n++;
            sx += x;
            sy += y;
            sxx += (int64_t)x * x;
            syy += (int64_t)y * y;
            sxy += (int64_t)x * y;
        }
    }
    if (n < MIN_FIT_PIXELS) {
        return 0;
    }

    int64_t spread_x = n * sxx - sx * sx;
    int64_t spread_y = n * syy - sy * sy;
    int vertical = spread_y > spread_x;
    int64_t spread = vertical ? spread_y : spread_x;
    if (spread < 10 * n) {
        return 0;
    }
    int64_t slope = Q10 * (n * sxy - sx * sy) / spread;
    if (abs64(slope) > MAX_SLOPE_Q10) {
        return 0;
    }
    line->vertical = vertical;
    line->slope_q10 = (int)slope;
    line->intercept_q10 =
        (int)(vertical ? (sx * Q10 - slope * sy) / n : (sy * Q10 - slope * sx) / n);
    return 1;
}

/* Is there a bright pixel within +-2 px of the line at this position along it? */
static int supported_at(const bright_image *im, const streak_line *line, int major)
{
    int minor = (int)(((int64_t)line->slope_q10 * major + line->intercept_q10) / Q10);
    int minor_limit = line->vertical ? im->width - EDGE_MARGIN : im->height - EDGE_MARGIN;

    if (minor < EDGE_MARGIN || minor >= minor_limit) {
        return -1;
    }
    for (int delta = -SUPPORT_HALFWIDTH; delta <= SUPPORT_HALFWIDTH; delta++) {
        int x = line->vertical ? minor + delta : major;
        int y = line->vertical ? major : minor + delta;
        if (im->image[y * im->stride + x] >= im->threshold) {
            return 1;
        }
    }
    return 0;
}

/* A trail must span at least 100 px and be lit along 65 % of that span. */
static int is_long_and_continuous(const bright_image *im, streak_line *line)
{
    int length = line->vertical ? im->height : im->width;
    int segments = 0;

    line->min_major = length;
    line->max_major = 0;
    for (int u = 2; u < length - 2; u += SUPPORT_STEP) {
        if (supported_at(im, line, u) == 1) {
            if (u < line->min_major) {
                line->min_major = u;
            }
            if (u > line->max_major) {
                line->max_major = u;
            }
        }
    }
    if (line->max_major - line->min_major < MIN_LENGTH) {
        return 0;
    }
    for (int u = line->min_major; u <= line->max_major; u += SUPPORT_STEP) {
        segments += supported_at(im, line, u) == 1;
    }
    return segments * 100 >= MIN_SUPPORT_PCT * ((line->max_major - line->min_major) / 4 + 1);
}

static streak_line detect_streak(const uint8_t *image, int width, int height, int stride,
                                 int contrast)
{
    streak_line none = {0};
    bright_image im = {image, width, height, stride, 0};

    if (!image || width < 100 || height < 80 || width > ASTRO_STREAK_MAX_WIDTH ||
        height > ASTRO_STREAK_MAX_HEIGHT || stride < width) {
        return none;
    }
    im.threshold = astro_sky_background(image, width, height, stride) + contrast;
    if (im.threshold > MAX_THRESHOLD) {
        return none;
    }

    if (vote(&im) > MAX_BRIGHT_PERCENT) {
        return none;
    }
    for (int attempt = 0; attempt < MAX_ATTEMPTS; attempt++) {
        int theta, rho;
        streak_line line = {0};
        if (take_peak(&theta, &rho) < MIN_VOTES) {
            return none;
        }
        if (fit_line(&im, theta, rho, &line) && is_long_and_continuous(&im, &line)) {
            line.valid = 1;
            return line;
        }
    }
    return none;
}

static void paint_line(uint8_t *image, int width, int height, int stride, const streak_line *line)
{
    uint8_t background = (uint8_t)astro_sky_background(image, width, height, stride);

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int major = line->vertical ? y : x;
            int minor = line->vertical ? x : y;
            if (major < line->min_major - MASK_EXTEND || major > line->max_major + MASK_EXTEND) {
                continue;
            }
            int64_t offset_q10 =
                (int64_t)minor * Q10 - ((int64_t)line->slope_q10 * major + line->intercept_q10);
            if (abs64(offset_q10) < MASK_HALFWIDTH * Q10) {
                image[y * stride + x] = background;
            }
        }
    }
}

int astro_mask_streak(uint8_t *image, int width, int height, int stride)
{
    for (unsigned i = 0; i < sizeof(contrast_levels) / sizeof(contrast_levels[0]); i++) {
        streak_line line = detect_streak(image, width, height, stride, contrast_levels[i]);
        if (line.valid) {
            paint_line(image, width, height, stride, &line);
            return 1;
        }
    }
    return 0;
}
