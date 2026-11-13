"""Host C regression: pressing Q stops autofocus between Canon lens commands.

The production autofocus.c is compiled with ASan/UBSan. Hardware operations
are deterministic mocks, including a simulated blocking Canon lens command.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / "modules/astro/core"
HARNESS = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "autofocus.h"
#include "focus_metric.h"

static struct {
    int position;
    int steps, frames, time_ms, max_sleep_ms;
    int cancel;
    int on_step, on_frame, on_position;
    int at_ms;
    int position_reads;
} camera;

static int ready(void *arg) { (void)arg; return 1; }
static int position(void *arg, int *out) {
    (void)arg;
    camera.position_reads++;
    *out = camera.position;
    if (camera.on_position && camera.position_reads == camera.on_position)
        camera.cancel = 1;
    return 1;
}
static int step(void *arg, int direction, int speed) {
    (void)arg;assert(speed > 0);
    camera.steps++;
    camera.position += direction;
    if (camera.steps == camera.on_step) {
        camera.cancel = 1; // Q arrives while Canon is processing this command.
        camera.time_ms += 2000; // Simulate an uninterruptible firmware wait.
    }
    return 1;
}
static int frame(void *arg, uint8_t *pixels, int width, int height) {
    (void)arg;
    assert(width == 64 && height == 64);
    memset(pixels, 0, (size_t)width*height);
    camera.frames++;
    if (camera.on_frame && camera.frames == camera.on_frame)
        camera.cancel = 1;
    return 1;
}
static void sleep_ms(void *arg, int millis) {
    (void)arg;
    assert(millis > 0 && millis <= 25); // Cancel is checked at most 25 ms apart.
    if (millis > camera.max_sleep_ms)camera.max_sleep_ms = millis;
    camera.time_ms += millis;
    if (camera.at_ms >= 0 && camera.time_ms >= camera.at_ms)
        camera.cancel = 1;
}
static int cancelled(void *arg) { (void)arg; return camera.cancel; }

/* Mock only the star-size metric. The focus controller logic is production C. */
int astro_focus_metric(const uint8_t *image, int width, int height, int stride,
                       int *x, int *y) {
    (void)image;(void)width;(void)height;(void)stride;
    *x=32;*y=32;
    return 10 + (camera.position - 1)*(camera.position - 1)*12;
}
int astro_focus_metric_locked(const uint8_t *image, int width, int height, int stride,
                              int *x, int *y, int radius) {
    (void)image;(void)width;(void)height;(void)stride;(void)x;(void)y;(void)radius;
    return 10 + (camera.position - 1)*(camera.position - 1)*12;
}
static astro_focus_result run(int radius, int enable_callback) {
    uint8_t image[64*64];
    astro_focus_result result;
    astro_focus_io io = {
        NULL, ready, position, step, frame, sleep_ms,
        enable_callback ? cancelled : NULL
    };
    astro_focus_status status =
        astro_run_autofocus(&io, image, 64, 64, radius, 1, &result);
    assert(status == result.status);
    return result;
}
static void reset(void) {
    memset(&camera, 0, sizeof(camera));
    camera.at_ms = -1;
}
int main(void) {
    astro_focus_result r;
    reset();
    camera.cancel = 1; // Q pressed before the job began.
    r=run(2,1);
    assert(r.status==ASTRO_FOCUS_ABORTED && camera.steps==0);
    assert(camera.position==0);

    reset();
    camera.at_ms = 51; // Midway through 100 ms acquisition confirmation.
    r=run(2,1);
    assert(r.status==ASTRO_FOCUS_ABORTED && camera.steps==0);
    assert(camera.time_ms==75 && camera.max_sleep_ms<=25);

    reset();
    camera.on_step=1; // Cancel during first blocking Canon focus command.
    r=run(2,1);
    assert(r.status==ASTRO_FOCUS_ABORTED && r.commands==1);
    assert(camera.steps==1 && camera.position==-1 && r.final_position==-1);

    reset();
    camera.on_step=3; // Cancel halfway through a multi-step sweep.
    r=run(2,1);
    assert(r.status==ASTRO_FOCUS_ABORTED && camera.steps==3);
    assert(r.commands==3 && r.final_position==camera.position);

    reset();
    camera.at_ms=150; // Cancel during first 200 ms sample hold.
    r=run(2,1);
    assert(r.status==ASTRO_FOCUS_ABORTED && camera.steps==2);
    assert(camera.time_ms==150 && camera.max_sleep_ms<=25);

    reset();
    camera.on_frame=3; // Cancel during the first capture after moving.
    r=run(2,1);
    assert(r.status==ASTRO_FOCUS_ABORTED && camera.steps==2);
    assert(camera.frames==3);

    reset();
    camera.on_position=1; // Cancel while checking initial position.
    r=run(2,1);
    assert(r.status==ASTRO_FOCUS_ABORTED && camera.steps==0);

    reset(); // No cancellation: same scan and final position as before.
    r=run(2,1);
    assert(r.status==ASTRO_FOCUS_OK && camera.steps>0);
    assert(camera.position==1 && r.final_position==1 && r.best_offset==1);

    reset(); // Existing users of the optional callback remain valid.
    r=run(2,0);
    assert(r.status==ASTRO_FOCUS_OK && camera.position==1);

    assert(strstr(astro_focus_status_text(ASTRO_FOCUS_ABORTED), "cancelled")!=NULL);
    puts("PASS: Q before/during focus, before every motor step, 25ms waits, normal scan");
    return 0;
}
"""

def test_af_cancellation():
    with tempfile.TemporaryDirectory(prefix="midnight-af-") as d:
        source = Path(d) / "focus_mock.c"
        source.write_text(HARNESS)
        executable = Path(d) / "focus_test"
        cmd = [
            "gcc", "-std=c99", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
            "-I", str(CORE),
            str(CORE / "autofocus.c"), str(source), "-o", str(executable),
        ]
        subprocess.run(cmd, check=True)
        run = subprocess.run([str(executable)], capture_output=True, text=True)
        assert run.returncode == 0, run.stdout + run.stderr
        assert "PASS:" in run.stdout
