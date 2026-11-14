/* SPDX-License-Identifier: GPL-2.0-only */
/* Persistent settings and shared status of the module. */
#ifndef ASTRO_STATE_H
#define ASTRO_STATE_H

#include <stdint.h>

/* Analysis image: Live View luma downscaled to 360x240 (3:2 sensor geometry). */
#define ASTRO_IMAGE_WIDTH  360
#define ASTRO_IMAGE_HEIGHT 240
#define ASTRO_IMAGE_BYTES  (ASTRO_IMAGE_WIDTH * ASTRO_IMAGE_HEIGHT)

/* Sensor width in 0.1 mm. Select at runtime because ML modules are portable ARM objects. */
#define ASTRO_600D_SENSOR_WIDTH_X10 223
#define ASTRO_700D_SENSOR_WIDTH_X10 223
#define ASTRO_6D_SENSOR_WIDTH_X10   358
int astro_sensor_width_x10(void);

#define ASTRO_DATA_DIR "ML/ASTRO/"

enum { ASTRO_FOCAL_BLIND = 0, ASTRO_FOCAL_FROM_LENS, ASTRO_FOCAL_MANUAL };
enum {
    ASTRO_SHORTCUT_OFF = 0,
    ASTRO_SHORTCUT_HALFSHUTTER,
    ASTRO_SHORTCUT_INFO,
    ASTRO_SHORTCUT_LIGHT
};

/* Saved in ML/SETTINGS/ASTRO.CFG (see state.c for names and defaults). */
extern int astro_cfg_focal_source;
extern int astro_cfg_focal_mm;
extern int astro_cfg_target_catalogue;
extern int astro_cfg_target_number;
extern int astro_cfg_mark_target;
extern int astro_cfg_overlay_stars;
extern int astro_cfg_overlay_dso;
extern int astro_cfg_overlay_constellations;
extern int astro_cfg_overlay_dso_mag;
extern int astro_cfg_focus_step;
extern int astro_cfg_focus_radius;
extern int astro_cfg_focus_shortcut;
extern int astro_cfg_focus_readout;
extern int astro_cfg_test_image;
extern int astro_cfg_location_valid;
extern int astro_cfg_latitude;        /* 1e-4 degree */
extern int astro_cfg_longitude;       /* 1e-4 degree */
extern int astro_cfg_utc_offset;      /* camera clock minus UTC, minutes */
extern int astro_cfg_location_time;   /* fix time, seconds since J2000 UTC */
extern int astro_cfg_location_folder; /* DCIM folder number of the source photo */
extern int astro_cfg_location_file;   /* file number of the source photo */

/* One-line status of the last action, shown in menus and notifications. */
void astro_set_status(const char *format, ...);
const char *astro_status(void);
int astro_status_recent(void); /* set within the last few seconds */

/* Focal length used as a solving hint in mm, 0 for a blind solve. */
int astro_hint_focal_mm(void);

#endif
