/* SPDX-License-Identifier: GPL-2.0-only */
/* Menu actions and read-only value formatters. UI layout is in menu.c. */
#include <dryos.h>
#include <menu.h>
#include <lens.h>
#include <notify_box.h>
#include <gui-common.h>
#include "capture.h"
#include "core/catalog.h"
#include "core/wcs.h"
#include "gain.h"
#include "location.h"
#include "menu.h"
#include "menu_handlers.h"
#include "overlay.h"
#include "polar.h"
#include "solve_actions.h"
#include "state.h"
#include "worker.h"

#define TAB_NAME        "Focus"
#define WIDE_FIELD_CDEG 6000 /* beyond ~60 degrees the optional SKYW index helps */

static void start(astro_action action, int close_menu)
{
    if (!astro_request(action)) {
        astro_set_status("Busy: wait or press Q");
        return;
    }
    if (close_menu) {
        gui_stop_menu();
    }
}

MENU_SELECT_FUNC(astro_menu_open_intervalometer)
{
    select_menu_by_name("Intervalometer", "Take a pic every");
    menu_redraw();
}

MENU_SELECT_FUNC(astro_menu_open_bulb_timer)
{
    select_menu_by_name("Bulb Timer", "Exposure duration");
    menu_redraw();
}

MENU_SELECT_FUNC(astro_menu_open_full_magic_lantern)
{
    select_menu_by_name("Shoot", "Intervalometer");
    menu_redraw();
}

MENU_SELECT_FUNC(astro_menu_start_solve_liveview)
{
    /* Do not queue an action when Canon is not showing 1x photo Live View.
     * This used to surface as a misleading "LiveView changed" after the
     * 3-second settling interval, even though no frame was ever captured. */
    if (!astro_liveview_ready() || is_play_or_qr_mode()) {
        if (!lv) astro_set_status("Turn on 1x photo Live View first");
        else if (is_movie_mode()) astro_set_status("Use photo mode, not movie mode");
        else if (lv_dispsize != 1) astro_set_status("Use 1x Live View, not 5x/10x");
        else astro_set_status("Close playback before Live View solve");
        NotifyBox(5500, "Midnight: %s", astro_status());
        return;
    }
    start(ASTRO_ACTION_SOLVE_LIVEVIEW, 0);
}

MENU_SELECT_FUNC(astro_menu_start_solve_file)
{
    start(ASTRO_ACTION_SOLVE_FILE, 0);
}

MENU_SELECT_FUNC(astro_menu_start_guide)
{
    if (astro_cfg_target_catalogue == ASTRO_CAT_NONE) {
        astro_set_status("Choose a target first");
        NotifyBox(4000, "Midnight: choose a target first");
        return;
    }
    start(ASTRO_ACTION_GUIDE, 1);
}

MENU_SELECT_FUNC(astro_menu_start_polar_first)
{
    start(ASTRO_ACTION_POLAR_FIRST, 1);
}

MENU_SELECT_FUNC(astro_menu_start_polar_second)
{
    start(ASTRO_ACTION_POLAR_SECOND, 1);
}

MENU_SELECT_FUNC(astro_menu_start_polar_adjust)
{
    start(ASTRO_ACTION_POLAR_ADJUST, 1);
}

MENU_SELECT_FUNC(astro_menu_start_autofocus)
{
    start(ASTRO_ACTION_AUTOFOCUS, 1);
}

MENU_SELECT_FUNC(astro_menu_start_measure)
{
    start(ASTRO_ACTION_MEASURE_FOCUS, 0);
}

MENU_SELECT_FUNC(astro_menu_start_read_location)
{
    start(ASTRO_ACTION_READ_LOCATION, 0);
}

#define STATUS_MENU_BYTES 25 /* fits beside "Solve Live View (3s)" */

/* Long values cover the row name, so the menu shows a short, fresh status only. The solve log
 * window has the full story. */
MENU_UPDATE_FUNC(astro_menu_status_update)
{
    char text[STATUS_MENU_BYTES];

    if (astro_busy() || astro_status_recent()) {
        snprintf(text, sizeof(text), "%s", astro_status());
        MENU_SET_VALUE("%s", text);
    }
}

/* Caret editing (as in the intervalometer) needs UNIT_DEC and no .select. The digit count
 * follows the catalogue's range. */
MENU_UPDATE_FUNC(astro_menu_target_number_update)
{
    int catalogue = astro_cfg_target_catalogue;
    char name[ASTRO_NAME_BYTES];

    if (catalogue == ASTRO_CAT_NONE) {
        MENU_SET_VALUE("None");
        MENU_SET_RINFO("Select a catalogue");
        return; /* no target: do not auto-select M1 or another object */
    }
    if (catalogue < ASTRO_CAT_M || catalogue > ASTRO_CAT_COUNT) {
        MENU_SET_VALUE("Invalid catalogue");
        return;
    }
    entry->max = astro_catalogue_max[catalogue];
    if (astro_cfg_target_number > entry->max) {
        astro_cfg_target_number = entry->max;
    }
    if (astro_cfg_target_number < 1) {
        astro_cfg_target_number = 1;
    }
    astro_designation(catalogue, astro_cfg_target_number, name, sizeof(name));
    MENU_SET_RINFO("%s", name);
}

MENU_UPDATE_FUNC(astro_menu_target_object_update)
{
    if (astro_cfg_target_catalogue == ASTRO_CAT_NONE) {
        MENU_SET_VALUE("None");
        return;
    }
    if (!astro_target_current()) {
        MENU_SET_VALUE("...");
        return;
    }
    MENU_SET_VALUE("%s", astro_target_info());
    if (astro_target_get()->valid) {
        MENU_SET_RINFO("%d'", (astro_target_get()->object.major_axis + 5) / 10);
    }
}

MENU_UPDATE_FUNC(astro_menu_target_sky_update)
{
    if (astro_cfg_target_catalogue == ASTRO_CAT_NONE) {
        MENU_SET_VALUE("Select target");
        return;
    }
    astro_site site;
    astro_horizontal h;
    static const char *const states[] = {"sets in", "rises in", "never sets", "never rises"};

    if (!astro_target_current() || !astro_target_get()->valid) {
        MENU_SET_VALUE("--");
        return;
    }
    if (!astro_location_site(&site)) {
        MENU_SET_VALUE("set Location first");
        return;
    }
    const astro_dso *o = &astro_target_get()->object;
    astro_altaz(&site, astro_now_utc(), o->ra, o->dec, &h);
    MENU_SET_VALUE("alt %d az %d", (int)(h.altitude / 10000), h.azimuth / 10000);
    if (h.visibility <= ASTRO_SKY_DOWN) {
        MENU_SET_RINFO("%s %dh%02d", states[h.visibility], h.minutes_to_change / 60,
                       h.minutes_to_change % 60);
    } else {
        MENU_SET_RINFO("%s", states[h.visibility]);
    }
    if (h.altitude < 0) {
        MENU_SET_WARNING(MENU_WARN_ADVICE, "The target is below the horizon now.");
    }
}

MENU_UPDATE_FUNC(astro_menu_polar_update)
{
    static const char *const steps[] = {"Capture A next", "A done: rotate RA, capture B",
                                        "Axis known: Adjust"};
    MENU_SET_VALUE("%s",
                   astro_polar_step() == 2 ? astro_polar_summary() : steps[astro_polar_step()]);
}

MENU_UPDATE_FUNC(astro_menu_location_source_update)
{
    char source[64];
    astro_location_source(source, sizeof(source));
    MENU_SET_VALUE("%s", source);
    if (astro_location_age_minutes() > ASTRO_LOCATION_STALE_HOURS * 60) {
        MENU_SET_WARNING(MENU_WARN_ADVICE, "Old fix: take a new photo with GPS on and re-read.");
    }
}

MENU_UPDATE_FUNC(astro_menu_location_position_update)
{
    int lat = astro_cfg_latitude < 0 ? -astro_cfg_latitude : astro_cfg_latitude;
    int lon = astro_cfg_longitude < 0 ? -astro_cfg_longitude : astro_cfg_longitude;

    if (!astro_cfg_location_valid) {
        MENU_SET_VALUE("--");
        return;
    }
    MENU_SET_VALUE("%d.%04d%s %d.%04d%s", lat / 10000, lat % 10000,
                   astro_cfg_latitude < 0 ? "S" : "N", lon / 10000, lon % 10000,
                   astro_cfg_longitude < 0 ? "W" : "E");
}

MENU_UPDATE_FUNC(astro_menu_utc_offset_update)
{
    int offset = astro_cfg_utc_offset;
    int magnitude = offset < 0 ? -offset : offset;
    MENU_SET_VALUE("UTC%s%d:%02d", offset < 0 ? "-" : "+", magnitude / 60, magnitude % 60);
}

MENU_UPDATE_FUNC(astro_menu_focal_source_update)
{
    if (astro_cfg_focal_source == ASTRO_FOCAL_FROM_LENS) {
        if (lens_info.lens_exists && lens_info.focal_len) {
            MENU_SET_RINFO("%dmm", lens_info.focal_len);
        } else {
            MENU_SET_WARNING(MENU_WARN_NOT_WORKING, "Lens reports no focal length: use Manual.");
        }
    } else if (astro_cfg_focal_source == ASTRO_FOCAL_MANUAL) {
        MENU_SET_RINFO("%dmm", astro_cfg_focal_mm);
    }
}

MENU_UPDATE_FUNC(astro_menu_manual_focal_update)
{
    int fov = astro_lens_fov_cdeg(astro_sensor_width_x10(), astro_cfg_focal_mm);
    MENU_SET_RINFO("FOV %d.%d", fov / 100, fov / 10 % 10);
    if (astro_cfg_focal_source != ASTRO_FOCAL_MANUAL) {
        MENU_SET_WARNING(MENU_WARN_ADVICE, "Used only when the focal source is Manual.");
    } else if (fov > WIDE_FIELD_CDEG) {
        MENU_SET_WARNING(MENU_WARN_ADVICE, "Over 60 deg: install the optional SKYW index.");
    }
}

MENU_UPDATE_FUNC(astro_menu_last_status_update)
{
    /* The Last solve screen is historical state, not a transient notification. */
    MENU_SET_VALUE("%s", astro_status());
}

MENU_UPDATE_FUNC(astro_menu_last_ra_update)
{
    const astro_solve_result *r = astro_last_solution();
    MENU_SET_VALUE("%s", r->valid ? r->ra_text : "--");
}

MENU_UPDATE_FUNC(astro_menu_last_dec_update)
{
    const astro_solve_result *r = astro_last_solution();
    MENU_SET_VALUE("%s", r->valid ? r->dec_text : "--");
}

MENU_UPDATE_FUNC(astro_menu_last_lens_update)
{
    const astro_solve_result *r = astro_last_solution();
    MENU_SET_VALUE("%s", r->valid ? r->lens_text : "--");
}
