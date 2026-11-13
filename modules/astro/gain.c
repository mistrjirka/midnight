/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Mirrors ML's existing photo-only display gain instead of writing registers ourselves.
 * set_display_gain_equiv() is avoided on purpose: in movie mode it changes recorded ISO.
 * Both functions below are non-static in src/lv-img-engio.c and exported by the 6D build.
 */
#include <dryos.h>
#include <propvalues.h>
#include <gui-common.h>
#include <notify_box.h>
#include <shoot.h>
#include "gain.h"
#include "worker.h"

extern int get_digic_iso_gain_photo(void);
extern void display_gain_toggle(void *priv, int delta);

#define UNITY_GAIN 1024 /* DIGIC gain value for 0 EV */

int astro_gain_ev(void)
{
    int gain = get_digic_iso_gain_photo();
    int ev = 0;

    while (ev < ASTRO_GAIN_MAX_EV && gain > (UNITY_GAIN << ev)) {
        ev++;
    }
    return ev;
}

MENU_SELECT_FUNC(astro_gain_select)
{
    if (astro_busy()) {
        NotifyBox(2000, "Gain locked while focusing / solving");
        return;
    }
    if (is_movie_mode()) {
        NotifyBox(2000, "Live View gain: photo mode only");
        return;
    }
    /* One stop per press, stopping at Off and +6 EV instead of wrapping around. */
    int ev = astro_gain_ev();
    int step = delta > 0 ? 1 : delta < 0 ? -1 : 0;
    if (!step || (step < 0 && ev == 0) || (step > 0 && ev == ASTRO_GAIN_MAX_EV)) {
        return;
    }
    display_gain_toggle(NULL, step); /* always the photo gain, never the movie gain */
    redraw();
}

MENU_UPDATE_FUNC(astro_gain_update)
{
    int ev = astro_gain_ev();

    if (ev) {
        MENU_SET_VALUE("+%d EV (%dx)", ev, 1 << ev);
    } else {
        MENU_SET_VALUE("OFF");
    }
    MENU_SET_ENABLED(ev);
    if (astro_busy()) {
        MENU_SET_WARNING(MENU_WARN_NOT_WORKING, "Wait for focusing / solving to finish.");
    } else if (is_movie_mode()) {
        MENU_SET_WARNING(MENU_WARN_NOT_WORKING, "Photo preview only; movie gain is untouched.");
    } else if (!lv) {
        MENU_SET_WARNING(MENU_WARN_ADVICE, "Open photo Live View to see it. Photos are unchanged.");
    } else if (ev) {
        MENU_SET_WARNING(MENU_WARN_ADVICE, "Boosted preview: not an exposure guide.");
    }
}
