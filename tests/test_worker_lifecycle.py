"""Compile Midnight's real worker.c logic against deterministic DryOS mocks.

Rejects invalid task IDs and verifies that QUIT blocks all later callbacks,
even when a foreground request was already queued. Runs with sanitizers.
"""
from pathlib import Path
import subprocess
import tempfile

SOURCE=(Path(__file__).resolve().parents[1]/"modules/astro/worker.c").read_text()
PRODUCTION=SOURCE[SOURCE.index("#define QUEUE_LENGTH"):]
MOCKS=r"""
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
typedef enum {
    ASTRO_ACTION_NONE=0, ASTRO_ACTION_SOLVE_LIVEVIEW, ASTRO_ACTION_SOLVE_FILE,
    ASTRO_ACTION_SOLVE_PLAYBACK, ASTRO_ACTION_GUIDE, ASTRO_ACTION_POLAR_FIRST,
    ASTRO_ACTION_POLAR_SECOND, ASTRO_ACTION_POLAR_ADJUST, ASTRO_ACTION_AUTOFOCUS,
    ASTRO_ACTION_MEASURE_FOCUS, ASTRO_ACTION_READ_LOCATION,
    ASTRO_ACTION_FOCUS_SCORE, ASTRO_ACTION_LOOKUP_TARGET, ASTRO_ACTION_QUIT
} astro_action;
#define ASTRO_IMAGE_BYTES (360*240)
struct semaphore { int value; };
struct msg_queue {int value; };
static struct semaphore sem;
static struct msg_queue queue_id;
static int fail_sem, fail_queue, fail_task;
static int queue_read, queue_write, actions;
static uint32_t messages[16];
static void (*task_entry)(void*);
static struct semaphore* create_named_semaphore(const char *name, int x) {
    (void)name;(void)x;return fail_sem ? (struct semaphore*)(uintptr_t)1 : &sem;
}
static struct msg_queue* msg_queue_create(char *name,uint32_t n) {
    (void)name;(void)n;return fail_queue ? (struct msg_queue*)(uintptr_t)5 : &queue_id;
}
static int take_semaphore(struct semaphore *s,int timeout){(void)s;(void)timeout;return 0;}
static int take_semaphore_now(struct semaphore *s){(void)s;return 0;}
static int give_semaphore(struct semaphore *s){(void)s;return 0;}
static int msg_queue_post(struct msg_queue *q,uint32_t action){
    (void)q;if(queue_write>=16)return 1;
    messages[queue_write++]=action;return 0;
}
static int msg_queue_receive(struct msg_queue *q,void *buffer,uint32_t timeout){
    (void)q;(void)timeout;
    if(queue_read==queue_write)return 1;
    *(uint32_t*)buffer=messages[queue_read++];return 0;
}
static uint32_t task_create(const char *name,uint32_t prio,uint32_t stack,
                            void (*entry)(void*),void *arg){
    (void)name;(void)prio;(void)stack;(void)arg;
    task_entry=entry;return fail_task ? 0 : 2;
}
static void msleep(int millis){(void)millis;}
static void astro_set_status(const char *format, ...) {(void)format;}
static void astro_run_solve(int file){(void)file;actions++;}
static void astro_playback_run_solve(void){actions++;}
static void astro_guide_run(void){actions++;}
static void astro_polar_capture(int second){(void)second;actions++;}
static void astro_polar_adjust(void){actions++;}
static void astro_focus_run_autofocus(void){actions++;}
static void astro_focus_measure(void){actions++;}
static void astro_focus_update_score(void){actions++;}
static void astro_target_lookup(void){actions++;}
static void astro_location_read_last_photo(void){actions++;}
"""
MAIN=r"""
int main(void){
    assert(!astro_request(ASTRO_ACTION_SOLVE_FILE));
    assert(!astro_request(ASTRO_ACTION_QUIT));
    fail_queue=1;
    assert(!astro_worker_start());
    assert(!astro_request(ASTRO_ACTION_SOLVE_FILE));
    fail_queue=0;
    fail_task=1;
    assert(!astro_worker_start());
    fail_task=0;
    queue_read=queue_write=0;
    assert(astro_worker_start());
    assert(!astro_worker_start());
    assert(!astro_request((astro_action)-1));
    assert(!astro_request((astro_action)99));
    assert(astro_request(ASTRO_ACTION_SOLVE_FILE));
    assert(astro_busy());
    assert(!astro_request(ASTRO_ACTION_SOLVE_PLAYBACK));
    assert(!astro_request(ASTRO_ACTION_LOOKUP_TARGET));
    assert(!astro_worker_stop()); // foreground action is active
    assert(astro_request(ASTRO_ACTION_QUIT));
    assert(!astro_request(ASTRO_ACTION_SOLVE_FILE)); // prevents work after QUIT
    assert(!astro_request(ASTRO_ACTION_FOCUS_SCORE));
    assert(!astro_request(ASTRO_ACTION_QUIT)); // no duplicate
    assert(task_entry);
    task_entry(NULL); // processes queued foreground request, then QUIT
    assert(actions==1);
    assert(!worker.busy && worker.stopped);
    assert(astro_worker_stop());
    assert(worker.image==NULL && !worker.started);
    assert(!astro_request(ASTRO_ACTION_SOLVE_FILE));
    puts("PASS: startup failures, valid IDs, queue exclusion, QUIT race, deinit");
    return 0;
}
"""
def test_worker_lifecycle():
    with tempfile.TemporaryDirectory(prefix="midnight-worker-") as temp:
        inp=Path(temp)/"worker.c";inp.write_text(MOCKS+PRODUCTION+MAIN)
        exe=Path(temp)/"worker"
        subprocess.run(["gcc","-std=c99","-O1","-g","-Wall","-Wextra","-Werror",
                        "-Wno-unused-parameter","-fsanitize=address,undefined",
                        "-fno-sanitize-recover=all",str(inp),"-o",str(exe)],check=True)
        result=subprocess.run([str(exe)],capture_output=True,text=True)
        assert result.returncode==0,result.stderr+result.stdout
        assert "PASS:" in result.stdout
