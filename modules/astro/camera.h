/* SPDX-License-Identifier: GPL-2.0-only */
/* Runtime-selected camera capabilities. The registry lives in camera_profiles.def. */
#ifndef ASTRO_CAMERA_H
#define ASTRO_CAMERA_H

#define ASTRO_FOCUS_KEY_NONE (-1)

typedef struct {
    const char *model;
    const char *firmware;
    int sensor_width_x10; /* sensor width in 0.1 mm */
    int raw_focus_key; /* verified special button, or ASTRO_FOCUS_KEY_NONE */
} astro_camera_profile;

/* Detect exactly one supported camera and firmware when the module starts.
 * The returned pointer is stable until the next detection call. */
const astro_camera_profile *astro_camera_detect(void);
const astro_camera_profile *astro_camera_current(void);
int astro_sensor_width_x10(void);

#endif
