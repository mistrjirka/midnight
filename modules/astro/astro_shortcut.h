/* Pure decision logic, usable in desktop unit tests and ARM firmware. */
#ifndef ASTRO_SHORTCUT_H
#define ASTRO_SHORTCUT_H
enum astro_camera_code {
    ASTRO_CAMERA_600D = 600,
    ASTRO_CAMERA_6D = 6
};
enum astro_shortcut_action {
    ASTRO_SHORTCUT_PASS = 0,
    ASTRO_SHORTCUT_AUTOFOCUS = 1,
    ASTRO_SHORTCUT_TOGGLE_READOUT = 2
};
/* Distinct Canon event IDs from platform/600D.102/gui.h and
 * platform/6D.116/gui.h. Key assignment is opt-out in the camera menu.
 */
int astro_shortcut_decide(int camera, int raw_key, int enabled,
                          int liveview_photo, int menu_open, int busy,
                          int electronic_af_available);
#endif
