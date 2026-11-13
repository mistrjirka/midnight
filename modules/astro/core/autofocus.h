/* SPDX-License-Identifier: GPL-2.0-only */
/* Fail-closed lens autofocus on a star. Hardware access is injected for host tests. */
#ifndef ASTRO_AUTOFOCUS_H
#define ASTRO_AUTOFOCUS_H

#include <stdint.h>

typedef struct {
    void *context;
    int (*ready)(void *context);                          /* lens, AF and LV usable */
    int (*position)(void *context, int *position);        /* reliable focus feedback */
    int (*step)(void *context, int direction, int speed); /* one motor step */
    int (*frame)(void *context, uint8_t *pixels, int width, int height);
    void (*sleep_ms)(void *context, int milliseconds);
} astro_focus_io;

typedef enum {
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
} astro_focus_status;

typedef struct {
    astro_focus_status status;
    int initial_position;
    int final_position;
    int best_offset; /* motor steps from the start position */
    int best_metric;
    int commands;
    int samples;
} astro_focus_result;

#define ASTRO_FOCUS_MAX_RADIUS 6
#define ASTRO_FOCUS_MAX_SPEED  3

/* Scans +-radius motor steps around the current position and moves to the sharpest one.
 * On any doubt (no feedback, lost star, edge optimum) it returns to the start position or
 * stops. */
astro_focus_status astro_run_autofocus(const astro_focus_io *io, uint8_t *pixels, int width,
                                       int height, int radius, int speed, astro_focus_result *out);

const char *astro_focus_status_text(astro_focus_status status);

#endif
