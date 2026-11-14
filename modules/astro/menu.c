/* SPDX-License-Identifier: GPL-2.0-only */
/* Declarative Midnight menu layout. Actions and value updates are in menu_handlers.c. */
#include <dryos.h>
#include <menu.h>
#include "core/catalog.h"
#include "gain.h"
#include "menu.h"
#include "menu_handlers.h"
#include "state.h"

#define TAB_NAME "Focus"

static struct menu_entry astro_menu[] = {
    {.name = "Intervalometer settings", .select = astro_menu_open_intervalometer, .icon_type = IT_ACTION,
     .help = "Original ML intervalometer: start, interval and shot count."},
    {.name = "Bulb Timer settings", .select = astro_menu_open_bulb_timer, .icon_type = IT_ACTION,
     .help = "Original ML Bulb Timer, no duplicated controls."},
    {
        .name = "Solve Live View (3s)",
        .select = astro_menu_start_solve_liveview,
        .update = astro_menu_status_update,
        .icon_type = IT_ACTION,
        .help = "Waits 3 s, solves the 1x photo Live View without hints, shows the result.",
    },
    {
        .name = "Target",
        .select = menu_open_submenu,
        .update = astro_menu_target_object_update,
        .help = "Choose an object to show its name and guidance on solved frames.",
        .children =
            (struct menu_entry[]){
                {.name = "Catalogue",
                 .priv = &astro_cfg_target_catalogue,
                 .min = ASTRO_CAT_NONE,
                 .max = ASTRO_CAT_COUNT,
                 .choices = CHOICES("None", "Messier (M)", "NGC", "IC", "Caldwell (C)",
                                    "Barnard (B)", "Sharpless (Sh2)"),
                 .help = "None hides guidance. Choose a catalogue to select a target."},
                {.name = "Number",
                 .priv = &astro_cfg_target_number,
                 .min = 1,
                 .max = 110,
                 .unit = UNIT_DEC,
                 .update = astro_menu_target_number_update,
                 .help = "LEFT/RIGHT: digit, UP/DOWN: value. M 1-110, NGC 1-7840, IC 1-5386."},
                {.name = "Object",
                 .update = astro_menu_target_object_update,
                 .icon_type = IT_ACTION,
                 .help = "Catalogue entry for this number (ML/ASTRO/DSO.BIN)."},
                {.name = "In the sky now",
                 .update = astro_menu_target_sky_update,
                 .icon_type = IT_ACTION,
                 .help = "Altitude/azimuth and rise/set, from Location and the camera clock."},
                {.name = "Mark target",
                 .priv = &astro_cfg_mark_target,
                 .max = 1,
                 .help = "After selecting a target, show its marker or guiding arrow."},
                MENU_EOL,
            },
    },
    {
        .name = "Guide to target",
        .select = astro_menu_start_guide,
        .icon_type = IT_ACTION,
        .help = "1x photo Live View: re-solves and points to the target. Q stops.",
        .help2 = "Set the lens focal length for much faster updates.",
    },
    {
        .name = "Polar alignment",
        .select = menu_open_submenu,
        .update = astro_menu_polar_update,
        .help = "Two solves around the RA axis find the mount axis; then adjust alt/az.",
        .children =
            (struct menu_entry[]){
                {.name = "1. Capture A",
                 .select = astro_menu_start_polar_first,
                 .icon_type = IT_ACTION,
                 .help = "Point near the pole, tracking on. Solves the Live View frame."},
                {.name = "2. Rotate RA, capture B",
                 .select = astro_menu_start_polar_second,
                 .icon_type = IT_ACTION,
                 .help = "Turn the RA axis 30-90 degrees first."},
                {.name = "3. Adjust",
                 .select = astro_menu_start_polar_adjust,
                 .icon_type = IT_ACTION,
                 .help = "Turn the alt/az knobs until the reticle sits in the circle. Q stops.",
                 .help2 = "Read the location first for altitude/azimuth directions."},
                {.name = "Status", .update = astro_menu_polar_update, .icon_type = IT_ACTION},
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
                {.name = "Measure once", .select = astro_menu_start_measure, .icon_type = IT_ACTION},
                {.name = "Autofocus",
                 .select = astro_menu_start_autofocus,
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
                 .help = "Special key: only 600D DISP or 6D LCD light.",
                 .help2 = "Other cameras: Off, INFO or double half-press."},
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
        .update = astro_menu_location_position_update,
        .help = "GPS-tagged latest photo (6D built-in GPS; other bodies need geotagged photos).",
        .children =
            (struct menu_entry[]){
                {.name = "Read newest photo",
                 .select = astro_menu_start_read_location,
                 .icon_type = IT_ACTION,
                 .help = "Take any photo with GPS on first. Shows which photo and how old."},
                {.name = "Source", .update = astro_menu_location_source_update, .icon_type = IT_ACTION},
                {.name = "Position", .update = astro_menu_location_position_update, .icon_type = IT_ACTION},
                {.name = "Clock offset",
                 .priv = &astro_cfg_utc_offset,
                 .min = -840,
                 .max = 840,
                 .update = astro_menu_utc_offset_update,
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
                 .update = astro_menu_focal_source_update,
                 .help = "A hint restricts the image scale to +-30 %: much faster solves."},
                {.name = "Manual focal length",
                 .priv = &astro_cfg_focal_mm,
                 .min = 4,
                 .max = 1200,
                 .unit = UNIT_DEC,
                 .update = astro_menu_manual_focal_update,
                 .help = "Millimetres. LEFT/RIGHT: digit, UP/DOWN: value."},
                MENU_EOL,
            },
    },
    {
        .name = "Last solve",
        .select = menu_open_submenu,
        .update = astro_menu_last_ra_update,
        .help = "Frame centre of the most recent solution (J2000).",
        .children =
            (struct menu_entry[]){
                {.name = "Status", .update = astro_menu_last_status_update, .icon_type = IT_ACTION},
                {.name = "RA", .update = astro_menu_last_ra_update, .icon_type = IT_ACTION},
                {.name = "Dec", .update = astro_menu_last_dec_update, .icon_type = IT_ACTION},
                {.name = "Measured lens",
                 .update = astro_menu_last_lens_update,
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
                 .select = astro_menu_start_solve_file,
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
    {.name = "Full Magic Lantern", .select = astro_menu_open_full_magic_lantern,
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
