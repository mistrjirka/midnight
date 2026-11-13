/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * The camera rides on the RA axis, so the axis stays at one fixed pixel while the alt/az
 * knobs move the whole mount. Find that pixel once from two frames, then keep re-solving
 * and show where the true pole is relative to it.
 */
#include <dryos.h>
#include <bmp.h>
#include <notify_box.h>
#include "core/polar_math.h"
#include "capture.h"
#include "draw.h"
#include "live.h"
#include "location.h"
#include "polar.h"
#include "solve_actions.h"
#include "state.h"
#include "worker.h"

#define SETTLE_MS       2000
#define SOLVE_BUDGET_MS 30000
#define NOTIFY_MS       5000
#define HUD_TEXT        96
#define GOOD_ENOUGH     50 /* 1e-4 degree: 18 arcsec */

static struct {
    int step;
    astro_wcs first;
    astro_polar_axis axis;
    char summary[64];
    int arrow_drawn;
    int32_t arrow[4]; /* axis and pole pixels last drawn, Q4 */
} polar;

int astro_polar_step(void)
{
    return polar.step;
}

const char *astro_polar_summary(void)
{
    return polar.summary;
}

static int capture_and_solve(astro_solve_result *result)
{
    uint8_t *image = astro_image();

    if (!astro_liveview_ready()) {
        astro_set_status("Use 1x photo Live View");
        return 0;
    }
    for (int waited = 0; waited < SETTLE_MS; waited += 100) {
        if (astro_cancel_requested()) {
            return 0;
        }
        msleep(100);
    }
    if (!astro_capture_display(image)) {
        astro_set_status("No Live View image");
        return 0;
    }
    astro_solve_status status = astro_solve_frame(image, SOLVE_BUDGET_MS, result);
    astro_solve_release();
    if (status != ASTRO_SOLVE_OK) {
        astro_set_status("%s", astro_solve_status_text(status));
        return 0;
    }
    astro_remember_solution(result);
    return 1;
}

static const char *axis_problem(astro_polar_status status)
{
    switch (status) {
        case ASTRO_POLAR_SMALL_ROTATION:
            return "Rotate the RA axis more (30-90 deg)";
        case ASTRO_POLAR_AXIS_OFF_FRAME_FAR:
            return "Axis far from the frame: point near the pole";
        default:
            return "Bad solution, capture again";
    }
}

void astro_polar_capture(int second)
{
    astro_solve_result result;

    if (second && polar.step < 1) {
        astro_set_status("Capture frame A first");
    } else if (capture_and_solve(&result)) {
        if (!second) {
            polar.first = result.sky.wcs;
            polar.step = 1;
            astro_set_status("A solved. Rotate RA 30-90 deg, then capture B");
        } else {
            astro_polar_status status = astro_polar_find_axis(
                &polar.first, &result.sky.wcs, ASTRO_IMAGE_WIDTH, ASTRO_IMAGE_HEIGHT, &polar.axis);
            if (status == ASTRO_POLAR_OK) {
                polar.step = 2;
                snprintf(
                    polar.summary, sizeof(polar.summary), "Axis found (rotated %d deg)",
                    (int)((polar.axis.rotation < 0 ? -polar.axis.rotation : polar.axis.rotation) /
                          10000));
                astro_set_status("%s. Now: Adjust", polar.summary);
            } else {
                astro_set_status("%s", axis_problem(status));
            }
        }
    }
    NotifyBox(NOTIFY_MS, "Polar: %s", astro_status());
}

/* "Alt: lower 0.25  Az: move west 0.10  (0.27 deg off)". */
static void correction_text(const astro_polar_error *e, int have_site, char *out, int size)
{
    int alt = e->altitude_error < 0 ? -e->altitude_error : e->altitude_error;
    int az = e->azimuth_error < 0 ? -e->azimuth_error : e->azimuth_error;

    if (e->separation < GOOD_ENOUGH) {
        snprintf(out, size, "ALIGNED (%d arcsec)   Q: stop", e->separation * 36 / 100);
    } else if (!have_site) {
        snprintf(out, size, "%d.%02d deg off: move to the circle  (set location)",
                 e->separation / 10000, e->separation / 100 % 100);
    } else {
        snprintf(out, size, "Alt: %s %d.%02d  Az: %s %d.%02d  (%d.%02d off)",
                 e->altitude_error > 0 ? "lower" : "raise", alt / 10000, alt / 100 % 100,
                 e->azimuth_error > 0 ? "west" : "east", az / 10000, az / 100 % 100,
                 e->separation / 10000, e->separation / 100 % 100);
    }
}

static void paint_arrow(int color)
{
    astro_draw_arrow(&astro_liveview_map, polar.arrow[0], polar.arrow[1], polar.arrow[2],
                     polar.arrow[3], color);
}

/* Erases the previous axis-to-pole arrow, then draws the new one. Call under BMP_LOCK. */
static void replace_arrow(const astro_polar_error *e)
{
    if (polar.arrow_drawn) {
        paint_arrow(COLOR_EMPTY);
    }
    polar.arrow_drawn = e->pole_projectable;
    if (polar.arrow_drawn) {
        polar.arrow[0] = polar.axis.axis_x_q4;
        polar.arrow[1] = polar.axis.axis_y_q4;
        polar.arrow[2] = e->pole_x_q4;
        polar.arrow[3] = e->pole_y_q4;
        paint_arrow(COLOR_CYAN);
    }
}

static void on_frame(void *unused, astro_solve_status status, const astro_solve_result *result)
{
    char text[HUD_TEXT];
    astro_polar_error error;
    astro_site site = {0, 0};
    int have_site = astro_location_site(&site);

    if (status != ASTRO_SOLVE_OK) {
        snprintf(text, sizeof(text), "%s   Q: stop", astro_solve_status_text(status));
        astro_hud(text, COLOR_WHITE);
        return;
    }
    if (!have_site) {
        site.latitude = 1; /* sign only: assume the northern hemisphere */
    }
    if (astro_polar_measure(&result->sky.wcs, polar.axis.axis_x_q4, polar.axis.axis_y_q4, &site,
                            astro_now_utc(), &error) != ASTRO_POLAR_OK) {
        astro_hud("Axis outside the solved field   Q: stop", COLOR_WHITE);
        return;
    }
    BMP_LOCK(replace_arrow(&error));
    correction_text(&error, have_site, text, sizeof(text));
    astro_hud(text, error.separation < GOOD_ENOUGH ? COLOR_GREEN1 : COLOR_YELLOW);
}

void astro_polar_adjust(void)
{
    astro_live_stats stats;

    if (polar.step < 2) {
        astro_set_status("Capture A and B first");
        return;
    }
    polar.arrow_drawn = 0;
    astro_live_loop(on_frame, NULL, &stats);
    if (polar.arrow_drawn) {
        BMP_LOCK(paint_arrow(COLOR_EMPTY));
    }
    astro_set_status("Adjust stopped: %d/%d frames solved", stats.solved, stats.frames);
}
