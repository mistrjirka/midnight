/* SPDX-License-Identifier: GPL-2.0-only */
/* Settings live here because ML only persists config variables that a module lists in
 * MODULE_CONFIGS, and that list must be in the same file as their definitions. */
#include <module.h>
#include <dryos.h>
#include <config.h>
#include <lens.h>
#include <stdarg.h>
#include <timer.h>
#include "core/catalog.h"
#include "state.h"

CONFIG_INT("astro.focal_src", astro_cfg_focal_source, ASTRO_FOCAL_BLIND);
CONFIG_INT("astro.focal_mm", astro_cfg_focal_mm, 50);
CONFIG_INT("astro.tgt_cat", astro_cfg_target_catalogue, ASTRO_CAT_M);
CONFIG_INT("astro.tgt_num", astro_cfg_target_number, 42);
CONFIG_INT("astro.tgt_on", astro_cfg_mark_target, 1);
CONFIG_INT("astro.ov_stars", astro_cfg_overlay_stars, 1);
CONFIG_INT("astro.ov_dso", astro_cfg_overlay_dso, 1);
CONFIG_INT("astro.ov_const", astro_cfg_overlay_constellations, 1);
CONFIG_INT("astro.ov_dsomag", astro_cfg_overlay_dso_mag, 9);
CONFIG_INT("astro.step", astro_cfg_focus_step, 1);
CONFIG_INT("astro.scanradius", astro_cfg_focus_radius, 3);
CONFIG_INT("astro.shortcut", astro_cfg_focus_shortcut, ASTRO_SHORTCUT_LIGHT);
CONFIG_INT("astro.overlay", astro_cfg_focus_readout, 0);
CONFIG_INT("astro.test_image", astro_cfg_test_image, 0);
CONFIG_INT("astro.loc_valid", astro_cfg_location_valid, 0);
CONFIG_INT("astro.loc_lat", astro_cfg_latitude, 0);
CONFIG_INT("astro.loc_lon", astro_cfg_longitude, 0);
CONFIG_INT("astro.loc_utcoff", astro_cfg_utc_offset, 0);
CONFIG_INT("astro.loc_time", astro_cfg_location_time, 0);
CONFIG_INT("astro.loc_folder", astro_cfg_location_folder, 0);
CONFIG_INT("astro.loc_file", astro_cfg_location_file, 0);

MODULE_CONFIGS_START()
MODULE_CONFIG(astro_cfg_focal_source)
MODULE_CONFIG(astro_cfg_focal_mm)
MODULE_CONFIG(astro_cfg_target_catalogue)
MODULE_CONFIG(astro_cfg_target_number)
MODULE_CONFIG(astro_cfg_mark_target)
MODULE_CONFIG(astro_cfg_overlay_stars)
MODULE_CONFIG(astro_cfg_overlay_dso)
MODULE_CONFIG(astro_cfg_overlay_constellations)
MODULE_CONFIG(astro_cfg_overlay_dso_mag)
MODULE_CONFIG(astro_cfg_focus_step)
MODULE_CONFIG(astro_cfg_focus_radius)
MODULE_CONFIG(astro_cfg_focus_shortcut)
MODULE_CONFIG(astro_cfg_focus_readout)
MODULE_CONFIG(astro_cfg_test_image)
MODULE_CONFIG(astro_cfg_location_valid)
MODULE_CONFIG(astro_cfg_latitude)
MODULE_CONFIG(astro_cfg_longitude)
MODULE_CONFIG(astro_cfg_utc_offset)
MODULE_CONFIG(astro_cfg_location_time)
MODULE_CONFIG(astro_cfg_location_folder)
MODULE_CONFIG(astro_cfg_location_file)
MODULE_CONFIGS_END()

#define STATUS_RECENT_MS 10000 /* how long a finished action's status stays in the menu */

static char status_text[64] = "Ready";
static uint32_t status_set_ms;

void astro_set_status(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(status_text, sizeof(status_text), format, args);
    va_end(args);
    status_set_ms = (uint32_t)get_ms_clock();
}

int astro_status_recent(void)
{
    return (uint32_t)get_ms_clock() - status_set_ms < STATUS_RECENT_MS;
}

const char *astro_status(void)
{
    return status_text;
}

int astro_sensor_width_x10(void)
{
    return is_camera("550D", "1.0.9") ? ASTRO_550D_SENSOR_WIDTH_X10
         : is_camera("600D", "1.0.2") ? ASTRO_600D_SENSOR_WIDTH_X10
         : is_camera("700D", "1.1.5") ? ASTRO_700D_SENSOR_WIDTH_X10
         : is_camera("6D", "1.1.6") ? ASTRO_6D_SENSOR_WIDTH_X10 : 0;
}

int astro_hint_focal_mm(void)
{
    if (astro_cfg_focal_source == ASTRO_FOCAL_FROM_LENS) {
        return lens_info.lens_exists ? (int)lens_info.focal_len : 0;
    }
    if (astro_cfg_focal_source == ASTRO_FOCAL_MANUAL) {
        return astro_cfg_focal_mm;
    }
    return 0;
}
