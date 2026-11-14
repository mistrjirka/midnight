/* SPDX-License-Identifier: GPL-2.0-only */
/* Single registry of supported camera firmware versions and capabilities. */
#include <module.h>
#include <stddef.h>
#include "camera.h"

static const astro_camera_profile profiles[] = {
#define ASTRO_CAMERA(model, firmware, width, key, symbols) {model, firmware, width, key},
#include "camera_profiles.def"
#undef ASTRO_CAMERA
};

static const astro_camera_profile *current;

const astro_camera_profile *astro_camera_detect(void)
{
    current = NULL;
    for (unsigned i = 0; i < sizeof(profiles) / sizeof(profiles[0]); ++i) {
        if (is_camera(profiles[i].model, profiles[i].firmware)) {
            current = &profiles[i];
            break;
        }
    }
    return current;
}

const astro_camera_profile *astro_camera_current(void)
{
    return current;
}

int astro_sensor_width_x10(void)
{
    return current ? current->sensor_width_x10 : 0;
}
