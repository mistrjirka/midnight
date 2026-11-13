/* SPDX-License-Identifier: GPL-2.0-only */
/* Glue between the fail-closed autofocus core and ML's lens and Live View APIs. */
#include <dryos.h>
#include <propvalues.h>
#include <bmp.h>
#include <focus.h>
#include <gui-common.h>
#include <lens.h>
#include <menu.h>
#include <module.h>
#include <notify_box.h>
#include <shoot.h>
#include <zebra.h>
#include "core/autofocus.h"
#include "core/focus_metric.h"
#include "core/shortcut.h"
#include "capture.h"
#include "star_focus.h"
#include "state.h"
#include "worker.h"

#define LENS_EXTRA_DELAY_MS 120
#define LOCK_RADIUS         12
#define MAX_LOST_SCORES     3
#define NOTIFY_MS           4500
#define READOUT_X           22
#define READOUT_Y           56

static struct {
    volatile int score; /* -1: no star */
    int x;
    int y;
    int lost;
} readout = {-1, ASTRO_IMAGE_WIDTH / 2, ASTRO_IMAGE_HEIGHT / 2, 0};

int astro_focus_lens_ready(void)
{
    return lv && lens_info.lens_exists && !is_manual_focus() && !is_continuous_af() &&
           !is_movie_mode() && lens_info.job_state == 0;
}

static int io_ready(void *unused)
{
    return astro_focus_lens_ready();
}

static int io_position(void *unused, int *out)
{
    if (!astro_focus_lens_ready()) {
        return 0;
    }
    *out = lens_info.focus_pos;
    return 1;
}

/* lens_focus() takes a signed step size; its sign is the direction (src/lens.c). */
static int io_step(void *unused, int direction, int speed)
{
    return astro_focus_lens_ready() && lens_focus(1, direction * speed, 1, LENS_EXTRA_DELAY_MS) &&
           astro_focus_lens_ready();
}

static int io_frame(void *unused, uint8_t *pixels, int width, int height)
{
    return width == ASTRO_IMAGE_WIDTH && height == ASTRO_IMAGE_HEIGHT && lv &&
           astro_capture_display(pixels);
}

static void io_sleep(void *unused, int milliseconds)
{
    msleep(milliseconds);
}

static void format_metric(char *out, int size, int metric)
{
    /* The metric is radius^2 * 16; show it with two decimals. */
    snprintf(out, size, "%d.%02d", metric / 16, metric % 16 * 100 / 16);
}

static int io_cancelled(void *unused)
{
    return astro_cancel_requested();
}

void astro_focus_run_autofocus(void)
{
    astro_focus_io io = {NULL, io_ready, io_position, io_step, io_frame, io_sleep, io_cancelled};
    astro_focus_result result;
    char metric[16];

    astro_focus_status status =
        astro_run_autofocus(&io, astro_image(), ASTRO_IMAGE_WIDTH, ASTRO_IMAGE_HEIGHT,
                            astro_cfg_focus_radius, astro_cfg_focus_step, &result);
    if (status == ASTRO_FOCUS_OK) {
        format_metric(metric, sizeof(metric), result.best_metric);
        astro_set_status("Focused: %d commands, size %s", result.commands, metric);
    } else {
        astro_set_status("%s", astro_focus_status_text(status));
    }
    NotifyBox(NOTIFY_MS, "Astro: %s", astro_status());
}

void astro_focus_measure(void)
{
    int x = ASTRO_IMAGE_WIDTH / 2;
    int y = ASTRO_IMAGE_HEIGHT / 2;
    char metric[16];

    if (!lv || !astro_capture_display(astro_image())) {
        astro_set_status("No Live View frame");
    } else {
        int score = astro_focus_metric(astro_image(), ASTRO_IMAGE_WIDTH, ASTRO_IMAGE_HEIGHT,
                                       ASTRO_IMAGE_WIDTH, &x, &y);
        if (score < 0) {
            astro_set_status("No bright star; try 5x/10x Live View");
        } else {
            format_metric(metric, sizeof(metric), score);
            astro_set_status("Star at (%d,%d): size %s", x, y, metric);
        }
    }
    NotifyBox(NOTIFY_MS, "Astro: %s", astro_status());
}

/* Keeps scoring the same star between frames; re-acquires after a few misses. */
void astro_focus_update_score(void)
{
    uint8_t *image = astro_image();
    int x = readout.x;
    int y = readout.y;
    int score = -1;

    if (lv && astro_capture_display(image)) {
        if (readout.lost < MAX_LOST_SCORES && readout.score >= 0) {
            score = astro_focus_metric_locked(image, ASTRO_IMAGE_WIDTH, ASTRO_IMAGE_HEIGHT,
                                              ASTRO_IMAGE_WIDTH, &x, &y, LOCK_RADIUS);
        } else {
            score = astro_focus_metric(image, ASTRO_IMAGE_WIDTH, ASTRO_IMAGE_HEIGHT,
                                       ASTRO_IMAGE_WIDTH, &x, &y);
        }
    }
    if (score >= 0) {
        readout.x = x;
        readout.y = y;
        readout.lost = 0;
    } else {
        readout.lost++;
    }
    readout.score = score;
}

void astro_focus_tick(void)
{
    char metric[16];

    if (!astro_cfg_focus_readout || !lv || is_movie_mode() || gui_menu_shown()) {
        return;
    }
    if (readout.score >= 0) {
        format_metric(metric, sizeof(metric), readout.score);
        bmp_printf(FONT(FONT_MED, COLOR_WHITE, COLOR_BLACK), READOUT_X, READOUT_Y,
                   "Star size: %s (lower = sharper)  ", metric);
    } else {
        bmp_printf(FONT(FONT_MED, COLOR_WHITE, COLOR_BLACK), READOUT_X, READOUT_Y,
                   "Star size: no star (try 5x LV)   ");
    }
    astro_request(ASTRO_ACTION_FOCUS_SCORE); /* dropped if the worker is busy */
}

static void toggle_readout(void)
{
    astro_cfg_focus_readout = !astro_cfg_focus_readout;
    readout.score = -1;
    readout.lost = 0;
    NotifyBox(2000, "Star size readout %s", astro_cfg_focus_readout ? "ON" : "OFF");
}

/* Portable keys: double half-press or INFO, depending on the shortcut setting. */
unsigned int astro_focus_key(unsigned int key)
{
    int shortcut = astro_cfg_focus_shortcut;

    if ((shortcut != ASTRO_SHORTCUT_HALFSHUTTER && shortcut != ASTRO_SHORTCUT_INFO) ||
        astro_busy() || !lv || is_movie_mode() || gui_menu_shown() || !display_idle()) {
        return 1;
    }
    if (shortcut == ASTRO_SHORTCUT_HALFSHUTTER &&
        detect_double_click(key, MODULE_KEY_PRESS_HALFSHUTTER, MODULE_KEY_UNPRESS_HALFSHUTTER)) {
        astro_request(ASTRO_ACTION_AUTOFOCUS);
        return 1; /* never swallow the shutter button */
    }
    if (shortcut == ASTRO_SHORTCUT_INFO && key == MODULE_KEY_INFO) {
        astro_request(ASTRO_ACTION_AUTOFOCUS);
        return 0;
    }
    return 1;
}

/* DISP on the 600D and LCD-light on the 6D are raw Canon events. */
unsigned int astro_focus_raw_key(unsigned int event_pointer)
{
    struct event *event = (struct event *)(uintptr_t)event_pointer;

    if (astro_cfg_focus_shortcut != ASTRO_SHORTCUT_LIGHT || !event || event->type != 0) {
        return 1;
    }
    int key = event->param;
    if (is_camera("600D", "1.0.2")) {
        if (key != ASTRO_KEY_600D_DISP) {
            return 1;
        }
        /* Feed the common policy a camera-neutral special-button event. */
        key = ASTRO_KEY_6D_LIGHT;
    } else if (!is_camera("6D", "1.1.6")) {
        return 1;
    }
    astro_shortcut_state state = {1, lv && !is_movie_mode() && display_idle(), gui_menu_shown(),
                                  astro_busy(), astro_focus_lens_ready()};
    astro_shortcut_action action = astro_shortcut_decide(key, &state);
    if (action == ASTRO_SHORTCUT_AUTOFOCUS) {
        astro_request(ASTRO_ACTION_AUTOFOCUS);
    } else if (action == ASTRO_SHORTCUT_TOGGLE_READOUT) {
        toggle_readout();
    }
    return action == ASTRO_SHORTCUT_PASS;
}
