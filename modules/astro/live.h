/* SPDX-License-Identifier: GPL-2.0-only */
/* Repeated Live View solving until Q, a menu, playback or leaving photo Live View. */
#ifndef ASTRO_LIVE_H
#define ASTRO_LIVE_H

#include "solve.h"

/* Called on the worker after every frame, solved or not. */
typedef void (*astro_live_frame_fn)(void *context, astro_solve_status status,
                                    const astro_solve_result *result);

typedef struct {
    int frames;
    int solved;
} astro_live_stats;

void astro_live_loop(astro_live_frame_fn on_frame, void *context, astro_live_stats *stats);

#endif
