/* SPDX-License-Identifier: GPL-2.0-only */
/* Menu-started solves: settle, capture, solve, then open the annotated result. */
#include <dryos.h>
#include <propvalues.h>
#include <gui-common.h>
#include <shoot.h>
#include "capture.h"
#include "overlay.h"
#include "progress.h"
#include "result_view.h"
#include "solve_actions.h"
#include "state.h"
#include "worker.h"

#define SETTLE_MS       3000 /* let tripod vibration die down; not an exposure */
#define SETTLE_STEP_MS  100
#define SOLVE_BUDGET_MS 45000
#define FAILURE_HOLD_MS 8000 /* long enough to read why it failed */

static astro_solve_result last_solution;
static astro_scene result_scene;

const astro_solve_result *astro_last_solution(void)
{
    return &last_solution;
}

void astro_remember_solution(const astro_solve_result *result)
{
    last_solution = *result;
}

/* Waits out the settling delay; returns 0 if Live View went away or Q was pressed. */
static int settle(void)
{
    astro_log("Hold still, tripod settling");
    for (int elapsed = 0; elapsed < SETTLE_MS; elapsed += SETTLE_STEP_MS) {
        if (astro_cancel_requested()) {
            astro_set_status("Live View solve cancelled by Q");
            return 0;
        }
        if (!lv) {
            astro_set_status("Live View switched off during settling");
            return 0;
        }
        if (is_movie_mode()) {
            astro_set_status("Movie mode started during settling");
            return 0;
        }
        if (lv_dispsize != 1) {
            astro_set_status("Live View zoom changed; use 1x");
            return 0;
        }
        if (is_play_or_qr_mode()) {
            astro_set_status("Playback opened during Live View solve");
            return 0;
        }
        astro_set_status("Hold still: %d s", (SETTLE_MS - elapsed + 999) / 1000);
        astro_log_progress("  %d s left", (SETTLE_MS - elapsed + 999) / 1000);
        msleep(SETTLE_STEP_MS);
    }
    return 1;
}

static int capture(uint8_t *image, int from_test_file)
{
    if (from_test_file) {
        astro_log("Reading %s", astro_test_file_name(astro_cfg_test_image));
        return astro_capture_test_file(image, astro_cfg_test_image);
    }
    if (!astro_liveview_ready()) {
        astro_set_status("Use 1x photo Live View");
        return 0;
    }
    if (!settle()) {
        return 0; /* settle() already stored the specific reason */
    }
    if (!astro_capture_display(image)) {
        astro_set_status("No Live View image");
        return 0;
    }
    astro_log("Live View frame captured");
    return 1;
}

static void fail_with_log(void)
{
    astro_log("%s", astro_status());
    astro_log_end(FAILURE_HOLD_MS);
}

void astro_run_solve(int from_test_file)
{
    uint8_t *image = astro_image();
    astro_solve_result result;

    astro_log_begin("Astro: plate solve");
    if (!capture(image, from_test_file)) {
        fail_with_log();
        return;
    }
    if (!astro_result_keep_frame(image)) {
        astro_set_status("Out of RAM for the result view");
        fail_with_log();
        return;
    }

    astro_solve_status status = astro_solve_frame(image, SOLVE_BUDGET_MS, &result);
    astro_solve_release();
    if (status != ASTRO_SOLVE_OK) {
        astro_set_status("%s (%d stars)", astro_solve_status_text(status), result.star_count);
        fail_with_log();
        return;
    }
    astro_build_overlay(&result, &result_scene);
    astro_remember_solution(&result);
    astro_set_status("Solved %d stars in %d.%02d s", result.sky.matched, result.elapsed_ms / 1000,
                     result.elapsed_ms % 1000 / 10);
    astro_log("%s", astro_status());
    astro_log_handoff();
    astro_result_show(&result, &result_scene);
}
