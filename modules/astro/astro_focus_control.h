/*
 * Portable fail-closed lens focus controller. GPL-2.0-or-later.
 * No Magic Lantern APIs, so hardware errors can be simulated on Linux.
 */
#ifndef ASTRO_FOCUS_CONTROL_H
#define ASTRO_FOCUS_CONTROL_H
#include <stdint.h>
typedef struct {
    void *context;
    int (*ready)(void *context);                  /* lens/AF/LV available? */
    int (*position)(void *context, int *position); /* reliable feedback? */
    int (*step)(void *context, int direction, int speed); /* one motor step */
    int (*frame)(void *context, uint8_t *pixels, int w, int h);
    void (*sleep_ms)(void *context, int milliseconds);
} astro_focus_io;
enum astro_focus_status {
    ASTRO_FOCUS_OK = 0,
    ASTRO_FOCUS_NO_LENS,
    ASTRO_FOCUS_NO_FRAME,
    ASTRO_FOCUS_NO_STAR,
    ASTRO_FOCUS_STAR_LOST,
    ASTRO_FOCUS_NO_FEEDBACK,
    ASTRO_FOCUS_MOTOR_FAILED,
    ASTRO_FOCUS_NOT_MOVING,
    ASTRO_FOCUS_EDGE,
    ASTRO_FOCUS_WEAK_CHANGE,
    ASTRO_FOCUS_ABORTED
};
typedef struct {
    enum astro_focus_status status;
    int initial_position, final_position, best_offset, best_metric;
    int commands, samples;
} astro_focus_result;
int astro_run_autofocus(const astro_focus_io *io, uint8_t *pixels, int w, int h,
                       int max_radius, int speed, astro_focus_result *out);
const char *astro_focus_status_text(enum astro_focus_status status);
#endif
