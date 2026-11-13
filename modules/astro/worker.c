/* SPDX-License-Identifier: GPL-2.0-only */
/* Message-queue worker. Menus and callbacks only post requests; the worker owns the image
 * buffer, so no two tasks ever touch it at once. */
#include <dryos.h>
#include <mem.h>
#include "star_focus.h"
#include "guide.h"
#include "location.h"
#include "overlay.h"
#include "playback.h"
#include "polar.h"
#include "solve_actions.h"
#include "state.h"
#include "worker.h"

#define QUEUE_LENGTH    8
#define TASK_PRIORITY   0x1d
#define TASK_STACK      0x4000
#define STOP_TIMEOUT_MS 2000

static struct {
    struct msg_queue *queue;
    struct semaphore *lock;
    volatile int pending; /* posted but not finished */
    volatile int busy;    /* a foreground action is posted or running */
    volatile int cancel;
    volatile int stopped;
    volatile astro_action running;
    uint8_t *image;
} worker;

static int is_foreground(astro_action action)
{
    return action > ASTRO_ACTION_NONE && action < ASTRO_ACTION_FOCUS_SCORE;
}

int astro_busy(void)
{
    return worker.busy;
}

astro_action astro_running_action(void)
{
    return worker.running;
}

void astro_cancel(void)
{
    worker.cancel = 1;
}

int astro_cancel_requested(void)
{
    return worker.cancel;
}

uint8_t *astro_image(void)
{
    return worker.image;
}

int astro_request(astro_action action)
{
    int accepted = 0;

    take_semaphore(worker.lock, 0);
    if (action == ASTRO_ACTION_QUIT || (is_foreground(action) && !worker.busy) ||
        (!is_foreground(action) && !worker.pending)) {
        accepted = msg_queue_post(worker.queue, (uint32_t)action) == 0;
    }
    if (accepted) {
        worker.pending++;
        if (is_foreground(action)) {
            worker.busy = 1;
            worker.cancel = 0;
        }
    }
    give_semaphore(worker.lock);
    return accepted;
}

static void run(astro_action action)
{
    switch (action) {
        case ASTRO_ACTION_SOLVE_LIVEVIEW:
        case ASTRO_ACTION_SOLVE_FILE:
            astro_run_solve(action == ASTRO_ACTION_SOLVE_FILE);
            break;
        case ASTRO_ACTION_SOLVE_PLAYBACK:
            astro_playback_run_solve();
            break;
        case ASTRO_ACTION_GUIDE:
            astro_guide_run();
            break;
        case ASTRO_ACTION_POLAR_FIRST:
        case ASTRO_ACTION_POLAR_SECOND:
            astro_polar_capture(action == ASTRO_ACTION_POLAR_SECOND);
            break;
        case ASTRO_ACTION_POLAR_ADJUST:
            astro_polar_adjust();
            break;
        case ASTRO_ACTION_AUTOFOCUS:
            astro_focus_run_autofocus();
            break;
        case ASTRO_ACTION_MEASURE_FOCUS:
            astro_focus_measure();
            break;
        case ASTRO_ACTION_FOCUS_SCORE:
            astro_focus_update_score();
            break;
        case ASTRO_ACTION_LOOKUP_TARGET:
            astro_target_lookup();
            break;
        case ASTRO_ACTION_READ_LOCATION:
            astro_location_read_last_photo();
            break;
        default:
            break;
    }
}

static void worker_task(void *unused)
{
    for (;;) {
        uint32_t message = 0;
        if (msg_queue_receive(worker.queue, &message, 0)) {
            continue;
        }
        astro_action action = (astro_action)message;
        if (action == ASTRO_ACTION_QUIT) {
            break;
        }
        if (!worker.image) {
            worker.image = malloc(ASTRO_IMAGE_BYTES);
        }
        worker.running = action;
        if (worker.image) {
            run(action);
        } else if (is_foreground(action)) {
            astro_set_status("Out of RAM (%d KB needed)", ASTRO_IMAGE_BYTES / 1024);
        }

        take_semaphore(worker.lock, 0);
        worker.running = ASTRO_ACTION_NONE;
        worker.pending--;
        if (is_foreground(action)) {
            worker.busy = 0;
        }
        give_semaphore(worker.lock);
    }
    worker.stopped = 1;
}

int astro_worker_start(void)
{
    worker.lock = create_named_semaphore("astro_lock", 1);
    worker.queue = (struct msg_queue *)msg_queue_create("astro_queue", QUEUE_LENGTH);
    worker.stopped = 0;
    return worker.lock && worker.queue &&
           task_create("astro_worker", TASK_PRIORITY, TASK_STACK, worker_task, 0);
}

int astro_worker_stop(void)
{
    if (worker.busy || !astro_request(ASTRO_ACTION_QUIT)) {
        return 0;
    }
    for (int waited = 0; !worker.stopped && waited < STOP_TIMEOUT_MS; waited += 50) {
        msleep(50);
    }
    if (!worker.stopped) {
        return 0;
    }
    free(worker.image);
    worker.image = NULL;
    return 1;
}
