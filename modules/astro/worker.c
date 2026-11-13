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
    volatile int stopped;
    volatile int closing; /* QUIT was queued: no new work may enter */
    volatile int started;
    volatile astro_action running;
    /* The producer owns next_ticket under lock. The GUI only reads active_ticket
     * and writes cancel_ticket using native 32-bit relaxed atomic load/store.
     * Running ticket is read and written by this worker task only. */
    uint32_t next_ticket;
    uint32_t active_ticket;
    uint32_t cancel_ticket;
    uint32_t running_ticket;
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
    /* GUI key callbacks must not wait for the worker's lock or queue.
     * ARMv5TE compiles relaxed 32-bit loads/stores to plain LDR/STR,
     * avoiding unavailable __sync_synchronize/libatomic helpers.
     *
     * A delayed cancellation write for an old ticket cannot affect new work. */
    uint32_t ticket = __atomic_load_n(&worker.active_ticket, __ATOMIC_RELAXED);
    if (ticket) {
        __atomic_store_n(&worker.cancel_ticket, ticket, __ATOMIC_RELAXED);
    }
}

int astro_cancel_requested(void)
{
    /* Called by the worker only: queued foreground work also carries its
     * cancellation when it starts behind an older background action. */
    uint32_t ticket = worker.running_ticket;
    return ticket && __atomic_load_n(&worker.cancel_ticket, __ATOMIC_RELAXED) == ticket;
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
        if (is_foreground(action)) {
            /* Reserve the ticket before posting. The worker may receive the
             * message immediately, and Q may arrive while we hold this lock.
             * Do not clear cancel_ticket on new actions: doing so could erase
             * a concurrent cancel request. Skip zero and any stale token. */
            uint32_t ticket = worker.next_ticket + 1;
            if (!ticket) ticket = 1;
            if (ticket == __atomic_load_n(&worker.cancel_ticket, __ATOMIC_RELAXED)) {
                ++ticket;
                if (!ticket) ticket = 1;
            }
            worker.next_ticket = ticket;
            worker.busy = 1;
            __atomic_store_n(&worker.active_ticket, ticket, __ATOMIC_RELAXED);
        }
        accepted = msg_queue_post(worker.queue, (uint32_t)action) == 0;
        if (!accepted && is_foreground(action)) {
            worker.busy = 0;
            __atomic_store_n(&worker.active_ticket, 0u, __ATOMIC_RELAXED);
        }
    }
    if (accepted) {
        worker.pending++;
        if (action == ASTRO_ACTION_QUIT) {
            /* Never allow messages to remain queued after the worker quits. */
            worker.closing = 1;
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
        /* astro_request() owns the foreground ticket until this action
         * completes. Read it only after taking the same lock, so even if the
         * worker woke immediately after msg_queue_post(), it sees the ID. */
        take_semaphore(worker.lock, 0);
        worker.running = action;
        worker.running_ticket = is_foreground(action)
            ? __atomic_load_n(&worker.active_ticket, __ATOMIC_RELAXED) : 0;
        give_semaphore(worker.lock);
        if (worker.image) {
            run(action);
        } else if (is_foreground(action)) {
            astro_set_status("Out of RAM (%d KB needed)", ASTRO_IMAGE_BYTES / 1024);
        }

        take_semaphore(worker.lock, 0);
        worker.running = ASTRO_ACTION_NONE;
        worker.running_ticket = 0;
        worker.pending--;
        if (is_foreground(action)) {
            __atomic_store_n(&worker.active_ticket, 0u, __ATOMIC_RELAXED);
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
    worker.busy = 0;
    worker.pending = 0;
    worker.running = ASTRO_ACTION_NONE;
    worker.next_ticket = 0;
    worker.running_ticket = 0;
    __atomic_store_n(&worker.active_ticket, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&worker.cancel_ticket, 0u, __ATOMIC_RELAXED);
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
