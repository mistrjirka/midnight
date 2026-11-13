/* SPDX-License-Identifier: GPL-2.0-only */
/* Live solve loop shared by guiding and polar-alignment adjustment. */
#include <dryos.h>
#include <bmp.h>
#include <propvalues.h>
#include <gui-common.h>
#include <menu.h>
#include <shoot.h>
#include "capture.h"
#include "draw.h"
#include "live.h"
#include "state.h"
#include "worker.h"

/* A focal-length hint narrows the search, so each update is much faster. */
#define HINTED_BUDGET_MS 8000
#define BLIND_BUDGET_MS  20000
#define MENU_CLOSE_STEPS 30
#define PAUSE_MS         200

static int keep_running(void)
{
    return !astro_cancel_requested() && astro_liveview_ready() && !gui_menu_shown() &&
           !is_play_or_qr_mode();
}

void astro_live_loop(astro_live_frame_fn on_frame, void *context, astro_live_stats *stats)
{
    uint8_t *image = astro_image();
    uint32_t budget = astro_hint_focal_mm() ? HINTED_BUDGET_MS : BLIND_BUDGET_MS;
    astro_solve_result result;

    stats->frames = 0;
    stats->solved = 0;
    for (int i = 0; i < MENU_CLOSE_STEPS && gui_menu_shown(); i++) {
        msleep(50); /* the menu is still closing */
    }
    astro_hud("Hold still...   Q: stop", COLOR_WHITE);
    while (keep_running()) {
        astro_solve_status status = ASTRO_SOLVE_FEW_STARS;
        if (astro_capture_display(image)) {
            status = astro_solve_frame(image, budget, &result);
        }
        if (!keep_running()) {
            break;
        }
        stats->frames++;
        stats->solved += status == ASTRO_SOLVE_OK;
        on_frame(context, status, &result);
        msleep(PAUSE_MS);
    }
    astro_solve_release();
    astro_hud_clear();
}
