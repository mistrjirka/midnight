/* SPDX-License-Identifier: GPL-2.0-only */
/* Solve pipeline: trail mask -> DoG detection -> SKY index -> optional SKYW wide index. */
#include <dryos.h>
#include <fio-ml.h>
#include <mem.h>
#include <timer.h>
#include <string.h>
#include "core/star_detect.h"
#include "core/streak.h"
#include "core/wcs.h"
#include "progress.h"
#include "sdcard.h"
#include "solve.h"
#include "state.h"
#include "worker.h"

#define MAX_KEYS       40000
#define MAX_PATTERNS   20000000
#define MAX_FIELDS     1000000
#define FORMAT_VERSION 1

/* With a possible trail, the unmasked attempt gets this share of the budget. */
#define FIRST_PASS_PERCENT 40
#define MIN_RETRY_MS       1000

/* Hint window: expected image scale +-30 %. */
#define HINT_LOW_NUM  10
#define HINT_LOW_DEN  13
#define HINT_HIGH_NUM 13
#define HINT_HIGH_DEN 10

typedef struct {
    int state; /* 0 not tried, 1 loaded, -1 absent, -2 corrupt */
    astro_sky_dir_entry *dir;
    astro_card_file patterns;
    astro_card_file fields;
    astro_sky_index index;
} loaded_index;

static struct {
    loaded_index sky;
    loaded_index wide;
    astro_detect_scratch *detect;
    astro_sky_workspace *workspace;
    uint32_t started_ms;
    uint32_t budget_ms;
} cache;

static int check_header(astro_card_file *f, const char *magic, uint32_t header[4])
{
    return astro_read_exact(f->file, header, 16) && !memcmp(header, magic, 4) &&
           header[1] == FORMAT_VERSION;
}

static int open_checked(astro_card_file *f, const char *prefix, const char *ext, const char *magic,
                        uint32_t header[4])
{
    char name[16];
    snprintf(name, sizeof(name), "%s.%s", prefix, ext);
    return astro_card_open(f, name) && check_header(f, magic, header);
}

/* SKY.DIR is kept in RAM (~142 KB); patterns and fields are read on demand. */
static void load_index(loaded_index *ix, const char *prefix)
{
    char name[16];
    uint32_t header[4];
    astro_card_file dir_file;

    snprintf(name, sizeof(name), "%s.DIR", prefix);
    if (!astro_card_open(&dir_file, name)) {
        ix->state = -1;
        return;
    }
    ix->state = -2;
    int ok = check_header(&dir_file, "SDIR", header) && header[2] && header[2] <= MAX_KEYS &&
             header[3] <= MAX_PATTERNS;
    if (ok) {
        uint32_t bytes = (header[2] + 1) * sizeof(*ix->dir);
        ix->index.nkeys = header[2];
        ix->index.npatterns = header[3];
        ix->dir = malloc(bytes);
        ok = ix->dir && astro_read_exact(dir_file.file, ix->dir, bytes);
    }
    astro_card_close(&dir_file);

    ok = ok && open_checked(&ix->patterns, prefix, "PAT", "SPAT", header) &&
         header[2] == ix->index.npatterns && header[3] == ASTRO_SKY_PATTERN_BYTES;
    ok = ok && open_checked(&ix->fields, prefix, "FLD", "SFLD", header) && header[2] &&
         header[2] <= MAX_FIELDS && header[3] == ASTRO_SKY_FIELD_BYTES;
    if (!ok) {
        return; /* state -2; astro_solve_release() frees what was opened */
    }
    ix->index.nfields = header[2];
    ix->index.dir = ix->dir;
    ix->index.read_patterns = astro_card_read;
    ix->index.patterns_context = &ix->patterns;
    ix->index.read_fields = astro_card_read;
    ix->index.fields_context = &ix->fields;
    ix->patterns.position = UINT32_MAX;
    ix->fields.position = UINT32_MAX;
    ix->state = 1;
}

static void unload_index(loaded_index *ix)
{
    astro_card_close(&ix->patterns);
    astro_card_close(&ix->fields);
    free(ix->dir);
    memset(ix, 0, sizeof(*ix));
}

void astro_solve_release(void)
{
    unload_index(&cache.sky);
    unload_index(&cache.wide);
    free(cache.detect);
    free(cache.workspace);
    cache.detect = NULL;
    cache.workspace = NULL;
}

static int keep_going(void *unused, unsigned quads, unsigned checked)
{
    astro_set_status("Sky patterns %d, checks %d", quads, checked);
    astro_log_progress("  %d quads tried, %d checks", quads, checked);
    return !astro_cancel_requested() &&
           (uint32_t)get_ms_clock() - cache.started_ms < cache.budget_ms;
}

/* Expected scale in Q16 px per millidegree: W * f / sensor px per radian * pi / 180000. */
static void hint_window(astro_sky_options *options, int *hinted)
{
    int focal = astro_hint_focal_mm();

    memset(options, 0, sizeof(*options));
    *hinted = focal >= 4;
    if (!*hinted) {
        return;
    }
    int64_t scale = (int64_t)ASTRO_IMAGE_WIDTH * focal * 65536 * 31416 /
                    ((int64_t)astro_sensor_width_x10() * 18000 * 10000);
    options->scale_min = (uint32_t)(scale * HINT_LOW_NUM / HINT_LOW_DEN);
    options->scale_max = (uint32_t)(scale * HINT_HIGH_NUM / HINT_HIGH_DEN);
}

static astro_sky_status solve_with(loaded_index *ix, const char *prefix,
                                   const astro_sky_options *options, astro_solve_result *out)
{
    if (!ix->state) {
        astro_log("Opening sky index %s", prefix);
        load_index(ix, prefix);
    }
    if (ix->state != 1) {
        return ASTRO_SKY_BAD_INDEX;
    }
    astro_log("Searching %s index, %s", prefix, options->scale_max ? "lens hint" : "blind");
    return astro_sky_solve(out->stars, out->star_count, ASTRO_IMAGE_WIDTH, ASTRO_IMAGE_HEIGHT,
                           &ix->index, options, keep_going, NULL, cache.workspace, &out->sky);
}

static void format_result(astro_solve_result *r)
{
    uint32_t seconds_of_time = (r->sky.ra * 24 + 500) / 1000; /* 1e-4 deg -> s of RA */
    if (seconds_of_time >= 86400) {
        seconds_of_time -= 86400;
    }
    snprintf(r->ra_text, sizeof(r->ra_text), "%02dh%02dm%02ds", seconds_of_time / 3600,
             seconds_of_time / 60 % 60, seconds_of_time % 60);

    int32_t dec = r->sky.dec;
    uint32_t arcsec = ((uint32_t)(dec < 0 ? -dec : dec) * 36 + 50) / 100;
    snprintf(r->dec_text, sizeof(r->dec_text), "%s%02dd%02d'%02d\"", dec < 0 ? "-" : "+",
             arcsec / 3600, arcsec / 60 % 60, arcsec % 60);

    int fov = r->sky.fov_cdeg;
    int focal_x10 = astro_lens_focal_x10(astro_sensor_width_x10(), fov);
    snprintf(r->lens_text, sizeof(r->lens_text), "%d mm, FOV %d.%d", (focal_x10 + 5) / 10,
             fov / 100, fov / 10 % 10);
}

static int ensure_scratch(void)
{
    if (!cache.detect) {
        cache.detect = malloc(sizeof(*cache.detect));
    }
    if (!cache.workspace) {
        cache.workspace = malloc(astro_sky_workspace_size());
    }
    return cache.detect && cache.workspace;
}

/* One detect-and-solve attempt on a frame, within budget_ms. */
static astro_solve_status solve_attempt(const uint8_t *image, uint32_t budget_ms, int masked,
                                        astro_solve_result *out)
{
    astro_sky_options options;

    memset(out, 0, sizeof(*out));
    cache.started_ms = (uint32_t)get_ms_clock();
    cache.budget_ms = budget_ms;
    astro_set_status("Detecting stars...");
    astro_log("Detecting stars");
    out->trail_masked = masked;
    out->star_count =
        astro_detect_stars(image, ASTRO_IMAGE_WIDTH, ASTRO_IMAGE_HEIGHT, ASTRO_IMAGE_WIDTH,
                           cache.detect, out->stars, ASTRO_MAX_STARS);
    astro_log("%d stars found", out->star_count);
    if (out->star_count < ASTRO_MIN_STARS) {
        return ASTRO_SOLVE_FEW_STARS;
    }

    hint_window(&options, &out->hinted);
    astro_sky_status status = solve_with(&cache.sky, "SKY", &options, out);
    if (status == ASTRO_SKY_BAD_INDEX) {
        return cache.sky.state == -1 ? ASTRO_SOLVE_NO_INDEX : ASTRO_SOLVE_BAD_INDEX;
    }
    if ((status == ASTRO_SKY_NO_MATCH || status == ASTRO_SKY_BUDGET_EXHAUSTED) &&
        cache.wide.state != -1 && !astro_cancel_requested()) {
        astro_log("No match; trying the wide index");
        astro_sky_status wide = solve_with(&cache.wide, "SKYW", &options, out);
        if (wide == ASTRO_SKY_SOLVED || wide == ASTRO_SKY_CANCELLED) {
            status = wide;
        }
    }
    if (status == ASTRO_SKY_CANCELLED) {
        return ASTRO_SOLVE_CANCELLED;
    }
    if (status != ASTRO_SKY_SOLVED) {
        return ASTRO_SOLVE_NO_MATCH;
    }
    out->valid = 1;
    format_result(out);
    return ASTRO_SOLVE_OK;
}

/* The trail masker can take a dense star field for a trail and blank real stars (it did on a
 * Milky Way frame: 22 matches fell to 14 and the search grew from 2 to 1866 patterns). So the
 * untouched frame goes first; the masked copy is only the fallback. */
astro_solve_status astro_solve_frame(uint8_t *image, uint32_t budget_ms, astro_solve_result *out)
{
    uint32_t started_ms = (uint32_t)get_ms_clock();
    uint8_t *masked = NULL;
    int trail = 0;

    if (!ensure_scratch()) {
        memset(out, 0, sizeof(*out));
        return ASTRO_SOLVE_NO_MEMORY;
    }
    masked = malloc(ASTRO_IMAGE_BYTES);
    if (masked) {
        memcpy(masked, image, ASTRO_IMAGE_BYTES);
        trail = astro_mask_streak(masked, ASTRO_IMAGE_WIDTH, ASTRO_IMAGE_HEIGHT, ASTRO_IMAGE_WIDTH);
    }
    if (trail) {
        astro_log("Possible satellite trail");
    }

    uint32_t first_ms = trail ? budget_ms * FIRST_PASS_PERCENT / 100 : budget_ms;
    astro_solve_status status = solve_attempt(image, first_ms, 0, out);
    uint32_t used_ms = (uint32_t)get_ms_clock() - started_ms;
    if (trail && !astro_cancel_requested() && used_ms + MIN_RETRY_MS < budget_ms &&
        (status == ASTRO_SOLVE_NO_MATCH || status == ASTRO_SOLVE_CANCELLED)) {
        astro_log("Retrying with the trail masked");
        status = solve_attempt(masked, budget_ms - used_ms, 1, out);
    }
    free(masked);

    out->elapsed_ms = (uint32_t)get_ms_clock() - started_ms;
    DryosDebugMsg(0, 15, "ASTRO result=%d stars=%d matched=%d ms=%d trail=%d", status,
                  out->star_count, out->sky.matched, out->elapsed_ms, out->trail_masked);
    return status;
}

const char *astro_solve_status_text(astro_solve_status status)
{
    switch (status) {
        case ASTRO_SOLVE_OK:
            return "Solved";
        case ASTRO_SOLVE_FEW_STARS:
            return "Too few stars (need 8)";
        case ASTRO_SOLVE_NO_MATCH:
            return "No verified sky match";
        case ASTRO_SOLVE_CANCELLED:
            return "Cancelled / time limit";
        case ASTRO_SOLVE_NO_INDEX:
            return "ML/ASTRO/SKY.* missing";
        case ASTRO_SOLVE_BAD_INDEX:
            return "SKY index corrupt";
        default:
            return "Out of RAM";
    }
}
