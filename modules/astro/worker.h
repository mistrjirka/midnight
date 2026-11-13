/* SPDX-License-Identifier: GPL-2.0-only */
/* The single worker task: all image analysis, card I/O and lens commands run here. */
#ifndef ASTRO_WORKER_H
#define ASTRO_WORKER_H

#include <stdint.h>

typedef enum {
    ASTRO_ACTION_NONE = 0,
    /* Foreground: the user started them, one at a time, Q cancels. */
    ASTRO_ACTION_SOLVE_LIVEVIEW,
    ASTRO_ACTION_SOLVE_FILE,
    ASTRO_ACTION_SOLVE_PLAYBACK,
    ASTRO_ACTION_GUIDE,
    ASTRO_ACTION_POLAR_FIRST,
    ASTRO_ACTION_POLAR_SECOND,
    ASTRO_ACTION_POLAR_ADJUST,
    ASTRO_ACTION_AUTOFOCUS,
    ASTRO_ACTION_MEASURE_FOCUS,
    ASTRO_ACTION_READ_LOCATION,
    /* Background: dropped silently when the worker has anything else to do. */
    ASTRO_ACTION_FOCUS_SCORE,
    ASTRO_ACTION_LOOKUP_TARGET,
    ASTRO_ACTION_QUIT
} astro_action;

int astro_worker_start(void);
/* Returns 0 if the worker is still busy (it is then left running). */
int astro_worker_stop(void);

/* Queues an action. Returns 0 when refused (busy, or a background action not needed). */
int astro_request(astro_action action);

int astro_busy(void); /* a foreground action is queued or running */
astro_action astro_running_action(void);
void astro_cancel(void); /* asks the running foreground action to stop */
int astro_cancel_requested(void);

/* Worker-owned analysis image (ASTRO_IMAGE_BYTES). Only valid inside the worker. */
uint8_t *astro_image(void);

#endif
