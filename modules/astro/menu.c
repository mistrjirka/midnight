/* SPDX-License-Identifier: GPL-2.0-only */
/* Menu tables and their handlers. Handlers only post worker requests or format values. */
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

static MENU_SELECT_FUNC(open_intervalometer)
{
    select_menu_by_name("Intervalometer", "Take a pic every");
    menu_redraw();
}

static MENU_SELECT_FUNC(open_bulb_timer)
{
    select_menu_by_name("Bulb Timer", "Exposure duration");
    menu_redraw();
}

static MENU_SELECT_FUNC(open_full_magic_lantern)
{
    select_menu_by_name("Shoot", "Intervalometer");
    menu_redraw();
}

static MENU_SELECT_FUNC(start_solve_liveview)
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

static MENU_SELECT_FUNC(start_solve_file)
{
    start(ASTRO_ACTION_SOLVE_FILE, 0);
}

static MENU_SELECT_FUNC(start_guide)
{
    start(ASTRO_ACTION_GUIDE, 1);
}

static MENU_SELECT_FUNC(start_polar_first)
{
    start(ASTRO_ACTION_POLAR_FIRST, 1);
}

static MENU_SELECT_FUNC(start_polar_second)
{
    start(ASTRO_ACTION_POLAR_SECOND, 1);
}

static MENU_SELECT_FUNC(start_polar_adjust)
{
    start(ASTRO_ACTION_POLAR_ADJUST, 1);
}

static MENU_SELECT_FUNC(start_autofocus)
{
    start(ASTRO_ACTION_AUTOFOCUS, 1);
}

static MENU_SELECT_FUNC(start_measure)
{
    start(ASTRO_ACTION_MEASURE_FOCUS, 0);
}

static MENU_SELECT_FUNC(start_read_location)
{
    start(ASTRO_ACTION_READ_LOCATION, 0);
}

#define STATUS_MENU_BYTES 25 /* fits beside "Solve Live View (3s)" */

/* Long values cover the row name, so the menu shows a short, fresh status only. The solve log
 * window has the full story. */
static MENU_UPDATE_FUNC(status_update)
{
    char text[STATUS_MENU_BYTES];

    if (astro_busy() || astro_status_recent()) {
        snprintf(text, sizeof(text), "%s", astro_status());
        MENU_SET_VALUE("%s", text);
    }
}

/* Caret editing (as in the intervalometer) needs UNIT_DEC and no .select. The digit count
 * follows the catalogue's range. */
static MENU_UPDATE_FUNC(target_number_update)
{
    int catalogue = astro_cfg_target_catalogue;
    char name[ASTRO_NAME_BYTES];

    if (catalogue < 1 || catalogue > ASTRO_CAT_COUNT) {
        catalogue = ASTRO_CAT_M;
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

static MENU_UPDATE_FUNC(target_object_update)
{
    if (!astro_target_current()) {
        MENU_SET_VALUE("...");
        return;
    }
    MENU_SET_VALUE("%s", astro_target_info());
    if (astro_target_get()->valid) {
        MENU_SET_RINFO("%d'", (astro_target_get()->object.major_axis + 5) / 10);
    }
}

static MENU_UPDATE_FUNC(target_sky_update)
{
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

static MENU_UPDATE_FUNC(polar_update)
{
    static const char *const steps[] = {"Capture A next", "A done: rotate RA, capture B",
                                        "Axis known: Adjust"};
    MENU_SET_VALUE("%s",
                   astro_polar_step() == 2 ? astro_polar_summary() : steps[astro_polar_step()]);
}

static MENU_UPDATE_FUNC(location_source_update)
{
    char source[64];
    astro_location_source(source, sizeof(source));
    MENU_SET_VALUE("%s", source);
    if (astro_location_age_minutes() > ASTRO_LOCATION_STALE_HOURS * 60) {
        MENU_SET_WARNING(MENU_WARN_ADVICE, "Old fix: take a new photo with GPS on and re-read.");
    }
}

static MENU_UPDATE_FUNC(location_position_update)
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

static MENU_UPDATE_FUNC(utc_offset_update)
{
    int offset = astro_cfg_utc_offset;
    int magnitude = offset < 0 ? -offset : offset;
    MENU_SET_VALUE("UTC%s%d:%02d", offset < 0 ? "-" : "+", magnitude / 60, magnitude % 60);
}

static MENU_UPDATE_FUNC(focal_source_update)
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

static MENU_UPDATE_FUNC(manual_focal_update)
{
    int fov = astro_lens_fov_cdeg(astro_sensor_width_x10(), astro_cfg_focal_mm);
    MENU_SET_RINFO("FOV %d.%d", fov / 100, fov / 10 % 10);
    if (astro_cfg_focal_source != ASTRO_FOCAL_MANUAL) {
        MENU_SET_WARNING(MENU_WARN_ADVICE, "Used only when the focal source is Manual.");
    } else if (fov > WIDE_FIELD_CDEG) {
        MENU_SET_WARNING(MENU_WARN_ADVICE, "Over 60 deg: install the optional SKYW index.");
    }
}

static MENU_UPDATE_FUNC(last_status_update)
{
    /* The Last solve screen is historical state, not a transient notification. */
    MENU_SET_VALUE("%s", astro_status());
}

static MENU_UPDATE_FUNC(last_ra_update)
{
    const astro_solve_result *r = astro_last_solution();
    MENU_SET_VALUE("%s", r->valid ? r->ra_text : "--");
}

static MENU_UPDATE_FUNC(last_dec_update)
{
    const astro_solve_result *r = astro_last_solution();
    MENU_SET_VALUE("%s", r->valid ? r->dec_text : "--");
}

static MENU_UPDATE_FUNC(last_lens_update)
{
    const astro_solve_result *r = astro_last_solution();
    MENU_SET_VALUE("%s", r->valid ? r->lens_text : "--");
}

static struct menu_entry astro_menu[] = {
    {.name = "Intervalometer settings", .select = open_intervalometer, .icon_type = IT_ACTION,
     .help = "Original ML intervalometer: start, interval and shot count."},
    {.name = "Bulb Timer settings", .select = open_bulb_timer, .icon_type = IT_ACTION,
     .help = "Original ML Bulb Timer, no duplicated controls."},
    {
        .name = "Solve Live View (3s)",
        .select = start_solve_liveview,
        .update = status_update,
        .icon_type = IT_ACTION,
        .help = "Waits 3 s, solves the 1x photo Live View without hints, shows the result.",
    },
    {
        .name = "Target",
        .select = menu_open_submenu,
        .update = target_object_update,
        .help = "Pick an M/NGC/IC/C/B/Sh2 object to guide to and mark on solved frames.",
        .children =
            (struct menu_entry[]){
                {.name = "Catalogue",
                 .priv = &astro_cfg_target_catalogue,
                 .min = 1,
                 .max = ASTRO_CAT_COUNT,
                 .choices = CHOICES("Messier (M)", "NGC", "IC", "Caldwell (C)", "Barnard (B)",
                                    "Sharpless (Sh2)")},
                {.name = "Number",
                 .priv = &astro_cfg_target_number,
                 .min = 1,
                 .max = 110,
                 .unit = UNIT_DEC,
                 .update = target_number_update,
                 .help = "LEFT/RIGHT: digit, UP/DOWN: value. M 1-110, NGC 1-7840, IC 1-5386."},
                {.name = "Object",
                 .update = target_object_update,
                 .icon_type = IT_ACTION,
                 .help = "Catalogue entry for this number (ML/ASTRO/DSO.BIN)."},
                {.name = "In the sky now",
                 .update = target_sky_update,
                 .icon_type = IT_ACTION,
                 .help = "Altitude/azimuth and rise/set, from Location and the camera clock."},
                {.name = "Mark target",
                 .priv = &astro_cfg_mark_target,
                 .max = 1,
                 .help = "Show the target, or an arrow to it, on every solved frame."},
                MENU_EOL,
            },
    },
    {
        .name = "Guide to target",
        .select = start_guide,
        .icon_type = IT_ACTION,
        .help = "1x photo Live View: re-solves and points to the target. Q stops.",
        .help2 = "Set the lens focal length for much faster updates.",
    },
    {
        .name = "Polar alignment",
        .select = menu_open_submenu,
        .update = polar_update,
        .help = "Two solves around the RA axis find the mount axis; then adjust alt/az.",
        .children =
            (struct menu_entry[]){
                {.name = "1. Capture A",
                 .select = start_polar_first,
                 .icon_type = IT_ACTION,
                 .help = "Point near the pole, tracking on. Solves the Live View frame."},
                {.name = "2. Rotate RA, capture B",
                 .select = start_polar_second,
                 .icon_type = IT_ACTION,
                 .help = "Turn the RA axis 30-90 degrees first."},
                {.name = "3. Adjust",
                 .select = start_polar_adjust,
                 .icon_type = IT_ACTION,
                 .help = "Turn the alt/az knobs until the reticle sits in the circle. Q stops.",
                 .help2 = "Read the location first for altitude/azimuth directions."},
                {.name = "Status", .update = polar_update, .icon_type = IT_ACTION},
                MENU_EOL,
            },
    },
    {
        .name = "Star focus",
        .select = menu_open_submenu,
        .help = "Star-size readout and lens autofocus on a star (no Bahtinov mask).",
        .children =
            (struct menu_entry[]){
                {.name = "Star size readout",
                 .priv = &astro_cfg_focus_readout,
                 .max = 1,
                 .help =
                     "Live score without moving the lens. Same star, 5x/10x; lower is sharper."},
                {.name = "Measure once", .select = start_measure, .icon_type = IT_ACTION},
                {.name = "Autofocus",
                 .select = start_autofocus,
                 .icon_type = IT_ACTION,
                 .help = "Electronic AF lens only. Moves the focus motor. Not hardware validated."},
                {.name = "Motor step size",
                 .priv = &astro_cfg_focus_step,
                 .min = 1,
                 .max = 3,
                 .help = "Canon focus step command. Start at 1."},
                {.name = "Scan radius",
                 .priv = &astro_cfg_focus_radius,
                 .min = 1,
                 .max = 6,
                 .help = "Motor steps tested on each side of the start position."},
                {.name = "Shortcut",
                 .priv = &astro_cfg_focus_shortcut,
                 .max = 3,
                 .choices = CHOICES("Off", "Double half-press", "INFO button", "600D DISP / 6D light"),
                 .help = "550D has no special-button shortcut. Select Off, INFO or half-press.",
                 .help2 = "600D uses DISP, 6D uses LCD-light. Photo Live View only."},
                MENU_EOL,
            },
    },
    {
        .name = "Live View gain",
        .select = astro_gain_select,
        .update = astro_gain_update,
        .icon_type = IT_PERCENT_OFF,
        .depends_on = DEP_PHOTO_MODE,
        .help = "Brighter preview, photo mode only. ISO and saved photos are unchanged.",
        .help2 = "LEFT/RIGHT: Off to +6 EV (64x). Also amplifies noise.",
    },
    {
        .name = "Overlays",
        .select = menu_open_submenu,
        .help = "What is drawn on solved frames.",
        .children =
            (struct menu_entry[]){
                {.name = "Star names",
                 .priv = &astro_cfg_overlay_stars,
                 .max = 1,
                 .help = "Stars to magnitude 4.5, with Greek Bayer letters."},
                {.name = "Deep-sky outlines", .priv = &astro_cfg_overlay_dso, .max = 1},
                {.name = "Constellations", .priv = &astro_cfg_overlay_constellations, .max = 1},
                {.name = "DSO magnitude limit",
                 .priv = &astro_cfg_overlay_dso_mag,
                 .min = 6,
                 .max = 14,
                 .help = "NGC/IC brighter than this are outlined. M and C always are."},
                MENU_EOL,
            },
    },
    {
        .name = "Location",
        .select = menu_open_submenu,
        .update = location_position_update,
        .help = "GPS-tagged latest photo (6D built-in GPS; 550D/600D need geotagged photos).",
        .children =
            (struct menu_entry[]){
                {.name = "Read newest photo",
                 .select = start_read_location,
                 .icon_type = IT_ACTION,
                 .help = "Take any photo with GPS on first. Shows which photo and how old."},
                {.name = "Source", .update = location_source_update, .icon_type = IT_ACTION},
                {.name = "Position", .update = location_position_update, .icon_type = IT_ACTION},
                {.name = "Clock offset",
                 .priv = &astro_cfg_utc_offset,
                 .min = -840,
                 .max = 840,
                 .update = utc_offset_update,
                 .help = "Camera clock minus UTC, learned from GPS time. Adjust if wrong."},
                MENU_EOL,
            },
    },
    {
        .name = "Lens",
        .select = menu_open_submenu,
        .help = "Optional solving hint. Blind solving needs no lens data.",
        .children =
            (struct menu_entry[]){
                {.name = "Focal length source",
                 .priv = &astro_cfg_focal_source,
                 .max = 2,
                 .choices = CHOICES("None (blind)", "From lens", "Manual"),
                 .update = focal_source_update,
                 .help = "A hint restricts the image scale to +-30 %: much faster solves."},
                {.name = "Manual focal length",
                 .priv = &astro_cfg_focal_mm,
                 .min = 4,
                 .max = 1200,
                 .unit = UNIT_DEC,
                 .update = manual_focal_update,
                 .help = "Millimetres. LEFT/RIGHT: digit, UP/DOWN: value."},
                MENU_EOL,
            },
    },
    {
        .name = "Last solve",
        .select = menu_open_submenu,
        .update = last_ra_update,
        .help = "Frame centre of the most recent solution (J2000).",
        .children =
            (struct menu_entry[]){
                {.name = "Status", .update = last_status_update, .icon_type = IT_ACTION},
                {.name = "RA", .update = last_ra_update, .icon_type = IT_ACTION},
                {.name = "Dec", .update = last_dec_update, .icon_type = IT_ACTION},
                {.name = "Measured lens",
                 .update = last_lens_update,
                 .icon_type = IT_ACTION,
                 .help = "Focal length measured from the solved field."},
                MENU_EOL,
            },
    },
    {
        .name = "Developer",
        .select = menu_open_submenu,
        .help = "Test files for QEMU and the desktop. Not needed for shooting.",
        .children =
            (struct menu_entry[]){
                {.name = "Solve image file",
                 .select = start_solve_file,
                 .icon_type = IT_ACTION,
                 .help = "Solves the selected 360x240 8-bit PGM from ML/ASTRO."},
                {.name = "Image file",
                 .priv = &astro_cfg_test_image,
                 .max = 5,
                 .choices = CHOICES("TEST.PGM", "TEST1.PGM", "TEST2.PGM", "TEST3.PGM", "RAW1.PGM",
                                    "RAW2.PGM")},
                MENU_EOL,
            },
    },
    {.name = "Full Magic Lantern", .select = open_full_magic_lantern,
     .icon_type = IT_ACTION, .help = "Return to original Magic Lantern controls."},
    MENU_EOL,
};

/* Keep the familiar Focus -> Midnight entry and quick settings shortcuts. */
static struct menu_entry midnight_home[] = {
    {
        .name = "Midnight",
        .select = menu_open_submenu,
        .help = "Sky solving, target guidance, polar alignment and focus.",
        .children = astro_menu,
        .submenu_width = 710,
        .icon_type = IT_SUBMENU,
    }
};

void astro_menu_add(void)
{
    menu_add(TAB_NAME, midnight_home, COUNT(midnight_home));
}

void astro_menu_remove(void)
{
    menu_remove(TAB_NAME, midnight_home, COUNT(midnight_home));
}
