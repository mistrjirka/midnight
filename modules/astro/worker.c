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
    volatile int closing; /* QUIT was queued: no new work may enter */
    volatile int started;
    volatile astro_action running;
    uint8_t *image;
} worker;

static int is_foreground(astro_action action)
{
    return action > ASTRO_ACTION_NONE && action < ASTRO_ACTION_FOCUS_SCORE;
}

int astro_busy(void)
{
    if (!worker.started || !worker.lock) {
        return 0;
    }
    /* The callback may run while a producer holds the lock. Treat that
     * short interval as busy instead of reading unsynchronized flags. */
    if (take_semaphore_now(worker.lock) != 0) {
        return 1;
    }
    int busy = worker.busy || worker.closing;
    give_semaphore(worker.lock);
    return busy;
}

astro_action astro_running_action(void)
{
    return worker.running;
}

void astro_cancel(void)
{
    if (worker.started) {
        worker.cancel = 1;
    }
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

    /* Never call DryOS with an invalid queue/semaphore ID. Unknown enum
     * values must not silently reach the worker as background actions. */
    if (!worker.started || !worker.lock || !worker.queue ||
        action <= ASTRO_ACTION_NONE || action > ASTRO_ACTION_QUIT) {
        return 0;
    }
    if (take_semaphore(worker.lock, 0) != 0) {
        return 0;
    }
    if (!worker.closing &&
        (action == ASTRO_ACTION_QUIT ||
         (is_foreground(action) && !worker.busy) ||
         (!is_foreground(action) && !worker.pending))) {
        accepted = msg_queue_post(worker.queue, (uint32_t)action) == 0;
    }
    if (accepted) {
        worker.pending++;
        if (action == ASTRO_ACTION_QUIT) {
            /* Prevent new requests from being posted *behind* QUIT. Those
             * would be abandoned and could reference an unloaded module. */
            worker.closing = 1;
        } else if (is_foreground(action)) {
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
    if (worker.started) {
        return 0;
    }
    /* DryOS treats low-bit-set IDs as errors. Test before any wait/post:
     * invalid queue IDs can otherwise cause assertions on the camera. */
    worker.lock = create_named_semaphore("astro_lock", 1);
    if (!worker.lock || ((uintptr_t)worker.lock & 1u)) {
        worker.lock = NULL;
        return 0;
    }
    worker.queue = (struct msg_queue *)msg_queue_create("astro_queue", QUEUE_LENGTH);
    if (!worker.queue || ((uintptr_t)worker.queue & 1u)) {
        worker.queue = NULL;
        return 0;
    }
    worker.stopped = 0;
    worker.closing = 0;
    worker.cancel = 0;
    worker.busy = 0;
    worker.pending = 0;
    worker.running = ASTRO_ACTION_NONE;
    worker.started = 1;
    uint32_t task_id = task_create("astro_worker", TASK_PRIORITY, TASK_STACK, worker_task, 0);
    if (!task_id || (task_id & 1u)) {
        worker.started = 0;
        return 0;
    }
    return 1;
}

int astro_worker_stop(void)
{
    if (!worker.started || worker.busy) {
        return 0;
    }
    if (!worker.closing && !astro_request(ASTRO_ACTION_QUIT)) {
        return 0;
    }
    for (int waited = 0; !worker.stopped && waited < STOP_TIMEOUT_MS; waited += 50) {
        msleep(50);
    }
    if (!worker.stopped) {
        return 0; /* fail closed: never unload code while its task runs */
    }
    free(worker.image);
    worker.image = NULL;
    worker.started = 0;
    return 1;
}
