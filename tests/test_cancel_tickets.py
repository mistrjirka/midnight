"""Real Midnight DryOS worker against deterministic queue + Q-press simulations.

Tests cancel before start, during queue publication, during processing, a late
cancel from the previous request, foreground behind background, post failure,
and 32-bit token rollover. Compiled under ASan/UBSan.
"""
from pathlib import Path
import subprocess
import tempfile

from test_worker_lifecycle import MOCKS as ORIGINAL_MOCKS, SOURCE

PRODUCTION = SOURCE[SOURCE.index("#define QUEUE_LENGTH"):]
MOCKS = ORIGINAL_MOCKS
MOCKS = MOCKS.replace(
    "static struct semaphore sem;",
    """void astro_cancel(void);
int astro_cancel_requested(void);
int astro_request(astro_action action);
static int inject_next, cancel_in_post, post_failure;
static int clean_runs, cancelled_runs, background_cancelled;
static struct semaphore sem;""",
)
MOCKS = MOCKS.replace(
    """    (void)q;if(queue_write>=16)return 1;
    messages[queue_write++]=action;return 0;""",
    """    (void)q;
    if (post_failure) {post_failure=0;return 1;}
    if(queue_write>=16)return 1;
    messages[queue_write++]=action;
    if(cancel_in_post && action==ASTRO_ACTION_SOLVE_FILE) astro_cancel();
    return 0;""",
)
MOCKS = MOCKS.replace(
    """    (void)q;(void)timeout;
    if(queue_read==queue_write)return 1;""",
    """    (void)q;(void)timeout;
    if(queue_read==queue_write && inject_next) {
        inject_next=0; // GUI posts B only after foreground A finishes.
        assert(astro_request(ASTRO_ACTION_SOLVE_FILE));
        assert(astro_request(ASTRO_ACTION_QUIT));
    }
    if(queue_read==queue_write)return 1;""",
)
MOCKS = MOCKS.replace(
    "static void astro_run_solve(int file){(void)file;actions++;}",
    """static void astro_run_solve(int file){
        (void)file;actions++;
        if(astro_cancel_requested()) cancelled_runs++; else clean_runs++;
    }""",
)
MOCKS = MOCKS.replace(
    "static void astro_focus_update_score(void){actions++;}",
    """static void astro_focus_update_score(void){
        actions++;
        if(astro_cancel_requested())background_cancelled++;
    }""",
)
MAIN = r"""
static void reset_test(void) {
    memset(&worker,0,sizeof(worker));
    queue_read=queue_write=0;
    actions=clean_runs=cancelled_runs=background_cancelled=0;
    inject_next=cancel_in_post=post_failure=0;
    fail_task=fail_queue=fail_sem=0;
    task_entry=NULL;
}
static void run_until_quit(void) {
    assert(task_entry);
    task_entry(NULL);
    assert(worker.stopped && !worker.busy);
    assert(astro_worker_stop());
}
int main(void) {
    reset_test();
    astro_cancel(); // Before startup must not touch uninitialized DryOS objects.
    assert(astro_worker_start());
    assert(astro_request(ASTRO_ACTION_FOCUS_SCORE));
    assert(astro_request(ASTRO_ACTION_SOLVE_FILE));
    uint32_t old = __atomic_load_n(&worker.active_ticket,__ATOMIC_RELAXED);
    assert(old != 0 && !astro_cancel_requested());
    astro_cancel(); // Q while the foreground action is queued behind background.
    assert(__atomic_load_n(&worker.cancel_ticket,__ATOMIC_RELAXED)==old);
    assert(astro_request(ASTRO_ACTION_QUIT));
    run_until_quit();
    assert(cancelled_runs==1 && clean_runs==0);
    assert(background_cancelled==0); // Background must not see next action's cancel.

    reset_test();
    assert(astro_worker_start());
    assert(astro_request(ASTRO_ACTION_SOLVE_FILE));
    astro_cancel(); // Cancel A, not a later B.
    uint32_t ticket_a=__atomic_load_n(&worker.active_ticket,__ATOMIC_RELAXED);
    inject_next=1;
    run_until_quit(); // Inject B only after A has completed.
    assert(clean_runs==1 && cancelled_runs==1);
    assert(worker.next_ticket!=ticket_a);

    reset_test();
    assert(astro_worker_start());
    cancel_in_post=1; // Simulates GUI Q while queue_post has not returned.
    assert(astro_request(ASTRO_ACTION_SOLVE_FILE));
    assert(astro_request(ASTRO_ACTION_QUIT));
    run_until_quit();
    assert(cancelled_runs==1 && clean_runs==0);

    reset_test();
    assert(astro_worker_start());
    post_failure=1;
    assert(!astro_request(ASTRO_ACTION_SOLVE_FILE));
    assert(!astro_busy());
    assert(__atomic_load_n(&worker.active_ticket,__ATOMIC_RELAXED)==0);
    assert(astro_request(ASTRO_ACTION_SOLVE_FILE));
    assert(astro_request(ASTRO_ACTION_QUIT));
    run_until_quit();
    assert(clean_runs==1 && cancelled_runs==0);

    reset_test();
    assert(astro_worker_start());
    worker.next_ticket=UINT32_MAX;
    __atomic_store_n(&worker.cancel_ticket,1u,__ATOMIC_RELAXED);
    assert(astro_request(ASTRO_ACTION_SOLVE_FILE));
    assert(__atomic_load_n(&worker.active_ticket,__ATOMIC_RELAXED)==2);
    assert(astro_request(ASTRO_ACTION_QUIT));
    run_until_quit();
    assert(clean_runs==1 && cancelled_runs==0);

    puts("PASS: ticketed Q, queued foreground, stale Q, post race/failure, wrap, QUIT");
    return 0;
}
"""

def test_cancel_tickets():
    with tempfile.TemporaryDirectory(prefix="midnight-ticket-") as d:
        source = Path(d) / "cancel.c"
        source.write_text(MOCKS + PRODUCTION + MAIN)
        executable = Path(d) / "cancel"
        subprocess.run([
            "gcc", "-std=c99", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
            "-Wno-unused-parameter", "-fsanitize=address,undefined",
            "-fno-sanitize-recover=all", str(source), "-o", str(executable)
        ], check=True)
        result = subprocess.run([str(executable)], capture_output=True, text=True)
        assert result.returncode == 0, result.stdout + result.stderr
        assert "PASS:" in result.stdout
