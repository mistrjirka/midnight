/* SPDX-License-Identifier: GPL-2.0-only */
/* Live guiding: every solved frame redraws the scene with the target arrow or outline. */
#include <dryos.h>
#include <bmp.h>
#include "core/altaz.h"
#include "draw.h"
#include "guide.h"
#include "live.h"
#include "location.h"
#include "overlay.h"
#include "solve_actions.h"
#include "state.h"

#define HUD_TEXT 96

static struct {
    astro_scene shown; /* what is on screen, so it can be erased exactly */
    astro_scene next;
    int drawn;
} guide;

/* " alt 34 deg" when the location is known, or a horizon warning. */
static void append_altitude(char *text, int size, const astro_target *target)
{
    astro_site site;
    astro_horizontal h;
    int used = (int)strlen(text);

    if (!target->valid || !astro_location_site(&site)) {
        return;
    }
    astro_altaz(&site, astro_now_utc(), target->object.ra, target->object.dec, &h);
    if (h.altitude < 0) {
        snprintf(text + used, size - used, "  BELOW HORIZON");
    } else {
        snprintf(text + used, size - used, "  alt %d", (int)(h.altitude / 10000));
    }
}

/* Erases exactly what was drawn before, then draws the new scene. Call under BMP_LOCK. */
static void replace_scene_on_screen(void)
{
    if (guide.drawn) {
        astro_draw_scene(&guide.shown, &astro_liveview_map, 1);
    }
    astro_draw_scene(&guide.next, &astro_liveview_map, 0);
    guide.shown = guide.next;
    guide.drawn = 1;
}

static void on_frame(void *unused, astro_solve_status status, const astro_solve_result *result)
{
    char text[HUD_TEXT];

    if (status != ASTRO_SOLVE_OK) {
        snprintf(text, sizeof(text), "%s: hold still / raise LV gain   Q: stop",
                 astro_solve_status_text(status));
        astro_hud(text, COLOR_WHITE);
        return;
    }
    astro_remember_solution(result);
    astro_build_overlay(result, &guide.next);
    BMP_LOCK(replace_scene_on_screen());

    astro_target_status(&guide.shown, text, sizeof(text));
    append_altitude(text, sizeof(text), astro_target_get());
    astro_hud(text, guide.shown.target == ASTRO_TARGET_IN_FRAME ? COLOR_GREEN1 : COLOR_YELLOW);
}

void astro_guide_run(void)
{
    astro_live_stats stats;

    astro_target_lookup();
    if (!astro_target_get()->valid) {
        astro_set_status("Pick a target first");
        return;
    }
    guide.drawn = 0;
    astro_live_loop(on_frame, NULL, &stats);
    if (guide.drawn) {
        BMP_LOCK(astro_draw_scene(&guide.shown, &astro_liveview_map, 1));
    }
    astro_set_status("Guide stopped: %d/%d frames solved", stats.solved, stats.frames);
}
