/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Star autofocus: verify the star on two frames, scan the focus motor one confirmed step at
 * a time, and only report success for a clear interior optimum. Every step is followed by
 * a position check, so a lens without feedback stops instead of being driven blindly.
 */
#include <string.h>
#include "autofocus.h"
#include "focus_metric.h"

#define MOTOR_SETTLE_MS  160
#define CONFIRM_DELAY_MS 100
#define SAMPLE_DELAY_MS  200
#define LOCK_RADIUS      7
#define MIN_IMPROVEMENT  2 /* metric units (1/16 px^2) needed to call it a change */
#define MAX_LOST_FRAMES  1
#define MAX_DIMENSION    1024

/* Lens backlash: the final position is approached from the same side as the scan. */
#define BACKLASH_OVERSHOOT 1

typedef struct {
    const astro_focus_io *io;
    astro_focus_result *result;
    uint8_t *pixels;
    int width;
    int height;
    int speed;
    int position; /* motor steps relative to the start */
    int star_x;
    int star_y;
} focus_run;

const char *astro_focus_status_text(astro_focus_status status)
{
    switch (status) {
        case ASTRO_FOCUS_OK:
            return "Focus complete";
        case ASTRO_FOCUS_NO_LENS:
            return "AF lens/LV unavailable";
        case ASTRO_FOCUS_NO_FRAME:
            return "LiveView frame unavailable";
        case ASTRO_FOCUS_NO_STAR:
            return "No confirmed star, motor not used";
        case ASTRO_FOCUS_STAR_LOST:
            return "Star lost, motor stopped";
        case ASTRO_FOCUS_NO_FEEDBACK:
            return "Lens position unavailable";
        case ASTRO_FOCUS_MOTOR_FAILED:
            return "Lens command failed; stopped";
        case ASTRO_FOCUS_NOT_MOVING:
            return "Motor unresponsive; stopped";
        case ASTRO_FOCUS_EDGE:
            return "Focus optimum at scan edge";
        case ASTRO_FOCUS_WEAK_CHANGE:
            return "No measurable focus change";
        default:
            return "Focus aborted";
    }
}

/* One motor step, confirmed by a change in reported position. */
static astro_focus_status step_checked(focus_run *run, int direction)
{
    const astro_focus_io *io = run->io;
    int before, after;

    if (!io->ready(io->context)) {
        return ASTRO_FOCUS_NO_LENS;
    }
    if (!io->position(io->context, &before)) {
        return ASTRO_FOCUS_NO_FEEDBACK;
    }
    if (!io->step(io->context, direction, run->speed)) {
        return ASTRO_FOCUS_MOTOR_FAILED;
    }
    run->result->commands++;

    for (int attempt = 0; attempt < 2; attempt++) {
        if (attempt) {
            io->sleep_ms(io->context, MOTOR_SETTLE_MS);
        }
        if (!io->ready(io->context)) {
            return ASTRO_FOCUS_NO_LENS;
        }
        if (!io->position(io->context, &after)) {
            return ASTRO_FOCUS_NO_FEEDBACK;
        }
        if (after != before) {
            run->position += direction;
            run->result->final_position = after;
            return ASTRO_FOCUS_OK;
        }
    }
    return ASTRO_FOCUS_NOT_MOVING;
}

static astro_focus_status move_to(focus_run *run, int target)
{
    while (run->position != target) {
        astro_focus_status status = step_checked(run, run->position < target ? 1 : -1);
        if (status != ASTRO_FOCUS_OK) {
            return status;
        }
    }
    return ASTRO_FOCUS_OK;
}

static astro_focus_status measure_locked(focus_run *run, int *metric)
{
    const astro_focus_io *io = run->io;
    int x = run->star_x;
    int y = run->star_y;

    if (!io->ready(io->context)) {
        return ASTRO_FOCUS_NO_LENS;
    }
    if (!io->frame(io->context, run->pixels, run->width, run->height)) {
        return ASTRO_FOCUS_NO_FRAME;
    }
    *metric = astro_focus_metric_locked(run->pixels, run->width, run->height, run->width, &x, &y,
                                        LOCK_RADIUS);
    return *metric < 0 ? ASTRO_FOCUS_STAR_LOST : ASTRO_FOCUS_OK;
}

/* Finds the star and confirms it on a second, independent frame. */
static astro_focus_status acquire_star(focus_run *run, int *baseline)
{
    const astro_focus_io *io = run->io;

    if (!io->frame(io->context, run->pixels, run->width, run->height)) {
        return ASTRO_FOCUS_NO_FRAME;
    }
    if (astro_focus_metric(run->pixels, run->width, run->height, run->width, &run->star_x,
                           &run->star_y) < 0) {
        return ASTRO_FOCUS_NO_STAR;
    }
    io->sleep_ms(io->context, CONFIRM_DELAY_MS);
    astro_focus_status status = measure_locked(run, baseline);
    return status == ASTRO_FOCUS_STAR_LOST ? ASTRO_FOCUS_NO_STAR : status;
}

/* Samples -radius..+radius. Tolerates one lost frame in a row. */
static astro_focus_status scan(focus_run *run, int radius, int *best, int *best_position)
{
    int lost = 0;
    astro_focus_status status = move_to(run, -radius);

    if (status != ASTRO_FOCUS_OK) {
        return status;
    }
    for (int sample = -radius; sample <= radius; sample++) {
        int metric;
        status = move_to(run, sample);
        if (status != ASTRO_FOCUS_OK) {
            return status;
        }
        run->io->sleep_ms(run->io->context, SAMPLE_DELAY_MS);
        status = measure_locked(run, &metric);
        if (status == ASTRO_FOCUS_STAR_LOST && ++lost <= MAX_LOST_FRAMES) {
            continue;
        }
        if (status != ASTRO_FOCUS_OK) {
            return status;
        }
        lost = 0;
        run->result->samples++;
        if (metric < *best) {
            *best = metric;
            *best_position = run->position;
        }
    }
    return ASTRO_FOCUS_OK;
}

static int valid_request(const astro_focus_io *io, const uint8_t *pixels, int width, int height,
                         int radius, int speed)
{
    return io && pixels && width >= 32 && height >= 32 && width <= MAX_DIMENSION &&
           height <= MAX_DIMENSION && radius >= 1 && radius <= ASTRO_FOCUS_MAX_RADIUS &&
           speed >= 1 && speed <= ASTRO_FOCUS_MAX_SPEED && io->ready && io->position && io->step &&
           io->frame && io->sleep_ms;
}

astro_focus_status astro_run_autofocus(const astro_focus_io *io, uint8_t *pixels, int width,
                                       int height, int radius, int speed, astro_focus_result *out)
{
    focus_run run = {io, out, pixels, width, height, speed, 0, width / 2, height / 2};
    astro_focus_status status = ASTRO_FOCUS_ABORTED;
    int baseline, best, best_position = 0;

    if (!out) {
        return ASTRO_FOCUS_ABORTED;
    }
    memset(out, 0, sizeof(*out));
    if (!valid_request(io, pixels, width, height, radius, speed)) {
        goto finished;
    }
    if (!io->ready(io->context)) {
        status = ASTRO_FOCUS_NO_LENS;
        goto finished;
    }
    if (!io->position(io->context, &out->initial_position)) {
        status = ASTRO_FOCUS_NO_FEEDBACK;
        goto finished;
    }
    out->final_position = out->initial_position;

    status = acquire_star(&run, &baseline);
    if (status != ASTRO_FOCUS_OK) {
        goto finished;
    }
    best = baseline;
    status = scan(&run, radius, &best, &best_position);
    if (status != ASTRO_FOCUS_OK) {
        goto finished;
    }
    out->best_offset = best_position;
    out->best_metric = best;

    /* Never call an edge optimum or a negligible change a success: go back to the start. */
    if (best_position == -radius || best_position == radius) {
        status = ASTRO_FOCUS_EDGE;
    } else if (baseline - best < MIN_IMPROVEMENT) {
        status = ASTRO_FOCUS_WEAK_CHANGE;
    } else {
        status = ASTRO_FOCUS_OK;
    }
    int target = status == ASTRO_FOCUS_OK ? best_position : 0;

    /* The scan ended at +radius moving upwards; approach the target from below too. */
    astro_focus_status moved = move_to(&run, target - BACKLASH_OVERSHOOT);
    if (moved == ASTRO_FOCUS_OK) {
        moved = move_to(&run, target);
    }
    if (moved != ASTRO_FOCUS_OK) {
        status = moved;
    }

finished:
    out->status = status;
    return status;
}
