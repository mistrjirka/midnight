/* SPDX-License-Identifier: GPL-2.0-only */
/* Playback solving reads the displayed preview, not the original RAW. The photo itself is
 * never modified; annotations are bitmap overlays only. */
#include <dryos.h>
#include <bmp.h>
#include <gui-common.h>
#include <menu.h>
#include <module.h>
#include <timer.h>
#include <notify_box.h>
#include "capture.h"
#include "draw.h"
#include "overlay.h"
#include "playback.h"
#include "solve.h"
#include "solve_actions.h"
#include "state.h"
#include "worker.h"

#define SOLVE_BUDGET_MS 45000
#define NOTIFY_MS       4500
#define PROMPT_TIMEOUT_MS 15000
#define PLAYBACK_READY_MS   500

typedef enum { VIEW_IDLE = 0, VIEW_PROMPT, VIEW_RUNNING, VIEW_RESULTS, VIEW_FAILURE } view_state;

static struct {
    volatile view_state state;
    uint32_t signature; /* of the photo being solved */
    uint32_t prompt_time_ms;
    uint32_t run_time_ms;
    astro_solve_result result;
    astro_scene scene;
} pb;

/* paint_* draw without locking; draw_* take the bitmap lock around them. */
static void paint_prompt(void)
{
    bmp_fill(COLOR_BLACK, 40, 174, 640, 122);
    bmp_printf(FONT(FONT_LARGE, COLOR_YELLOW, COLOR_BLACK), 56, 183, "Solve this photo?");
    bmp_printf(FONT(FONT_MED, COLOR_WHITE, COLOR_BLACK), 56, 226, "SET: solve    Q: cancel");
    bmp_printf(FONT(FONT_SMALL, COLOR_WHITE, COLOR_BLACK), 56, 260,
               "No sky coordinates or lens settings needed");
}

static void paint_progress(void)
{
    bmp_fill(COLOR_BLACK, 12, 407, 695, 43);
    bmp_printf(FONT(FONT_MED, COLOR_YELLOW, COLOR_BLACK), 20, 414, "Finding sky: %s",
               astro_status());
}

static void paint_results(void)
{
    astro_draw_scene(&pb.scene, &astro_liveview_map, 0);
    astro_draw_matches(&pb.result, &astro_liveview_map);
    bmp_fill(COLOR_BLACK, 8, 6, 345, 102);
    bmp_printf(FONT(FONT_MED, COLOR_YELLOW, COLOR_BLACK), 16, 12, "SKY FOUND (%d stars)",
               pb.result.sky.matched);
    bmp_printf(FONT(FONT_MED, COLOR_WHITE, COLOR_BLACK), 16, 43, "RA: %s", pb.result.ra_text);
    bmp_printf(FONT(FONT_MED, COLOR_WHITE, COLOR_BLACK), 16, 72, "Dec: %s", pb.result.dec_text);
    bmp_printf(FONT(FONT_SMALL, COLOR_WHITE, COLOR_BLACK), 12, 450,
               "Q: hide  |  LEFT/RIGHT: other photo");
}

static void paint_failure(void)
{
    bmp_fill(COLOR_BLACK, 16, 365, 688, 88);
    bmp_printf(FONT(FONT_MED, COLOR_YELLOW, COLOR_BLACK), 24, 372,
               "MIDNIGHT: COULD NOT SOLVE");
    bmp_printf(FONT(FONT_MED, COLOR_WHITE, COLOR_BLACK), 24, 405, "%s", astro_status());
    bmp_printf(FONT(FONT_SMALL, COLOR_WHITE, COLOR_BLACK), 24, 434,
               "Q: dismiss  |  LEFT/RIGHT: other photo");
}

static void draw_failure(void)
{
    BMP_LOCK(paint_failure());
}

static void fail_playback(const char *message)
{
    astro_set_status("%s", message);
    pb.state = VIEW_FAILURE;
    if (is_pure_play_photo_mode()) {
        draw_failure();
    } else {
        NotifyBox(NOTIFY_MS, "Astro: %s", astro_status());
    }
}

static void draw_prompt(void)
{
    BMP_LOCK(paint_prompt());
}

static void draw_progress(void)
{
    BMP_LOCK(paint_progress());
}

static void draw_results(void)
{
    BMP_LOCK(paint_results());
}

void astro_playback_run_solve(void)
{
    uint8_t *image = astro_image();
    /* A camera dialog may briefly change state after SET. The worker, not
     * the key handler, can safely wait for the photo to be displayed again. */
    for (int elapsed = 0; elapsed < PLAYBACK_READY_MS && is_play_or_qr_mode() &&
                          !is_pure_play_photo_mode() && !astro_cancel_requested(); elapsed += 25) {
        msleep(25);
    }
    pb.signature = astro_playback_signature();
    if (!is_pure_play_photo_mode() || !pb.signature) {
        fail_playback("Photo playback not ready");
        return;
    }
    if (!astro_capture_display(image)) {
        fail_playback("No playback YUV image");
        return;
    }
    astro_set_status("Detecting stars...");
    astro_solve_status status = astro_solve_frame(image, SOLVE_BUDGET_MS, &pb.result);
    if (status == ASTRO_SOLVE_OK) {
        astro_build_overlay(&pb.result, &pb.scene);
    }
    astro_solve_release();

    if (status != ASTRO_SOLVE_OK) {
        fail_playback(astro_solve_status_text(status));
    } else if (!is_pure_play_photo_mode() ||
               astro_playback_signature() != pb.signature) {
        fail_playback("Photo changed; result hidden");
    } else {
        astro_remember_solution(&pb.result);
        astro_set_status("Solved %d stars", pb.result.sky.matched);
        pb.state = VIEW_RESULTS;
        draw_results();
    }
}

static void close_view(void)
{
    pb.state = VIEW_IDLE;
    clrscr();
}

/* The SET press is consumed BEFORE looking at Canon's transient playback
 * dialog state. Otherwise SET+MainDial ("Scrollwheel: Exposure Adjust")
 * receives it and the solve never starts. */
unsigned int astro_playback_key(unsigned int key)
{
    if (pb.state == VIEW_PROMPT &&
        (uint32_t)(get_ms_clock() - pb.prompt_time_ms) > PROMPT_TIMEOUT_MS) {
        close_view();
        astro_set_status("Gallery: confirmation timed out");
    }
    if (pb.state == VIEW_PROMPT && !gui_menu_shown()) {
        if (key == MODULE_KEY_PRESS_SET) {
            /* Mark the view running BEFORE waking the worker. It may reject
             * the frame immediately, so a later state assignment would hide
             * the worker's failure. */
            pb.state = VIEW_RUNNING;
            pb.run_time_ms = (uint32_t)get_ms_clock();
            astro_set_status("Starting gallery solve...");
            clrscr();
            if (!astro_request(ASTRO_ACTION_SOLVE_PLAYBACK)) {
                fail_playback("Gallery: busy, could not queue solve");
            } else if (pb.state == VIEW_RUNNING && is_pure_play_photo_mode()) {
                draw_progress();
            }
            return 0;
        }
        if (key == MODULE_KEY_Q) {
            close_view();
            astro_set_status("Gallery: cancelled before solve");
            return 0;
        }
        if (key == MODULE_KEY_PRESS_LEFT || key == MODULE_KEY_PRESS_RIGHT ||
            key == MODULE_KEY_PLAY || key == MODULE_KEY_MENU ||
            key == MODULE_KEY_TRASH) {
            close_view();
        }
        /* Do not clear the prompt on key releases or unknown Canon events. */
        return 1;
    }
    if (pb.state == VIEW_RUNNING) {
        if (key == MODULE_KEY_Q) {
            astro_cancel();
            return 0;
        }
        /* A second SET must not enable the unrelated exposure-adjust action. */
        return key == MODULE_KEY_PRESS_SET ? 0 : 1;
    }
    if (!is_pure_play_photo_mode() || gui_menu_shown()) {
        if (gui_menu_shown() || !is_play_or_qr_mode()) pb.state = VIEW_IDLE;
        return 1;
    }
    if (pb.state == VIEW_RESULTS || pb.state == VIEW_FAILURE) {
        if (key == MODULE_KEY_Q) {
            close_view();
            return 0;
        }
        if (key == MODULE_KEY_PRESS_LEFT || key == MODULE_KEY_PRESS_RIGHT ||
            key == MODULE_KEY_PLAY || key == MODULE_KEY_MENU ||
            key == MODULE_KEY_TRASH) close_view();
        return 1;
    }
    if (key == MODULE_KEY_Q && !astro_busy()) {
        pb.state = VIEW_PROMPT;
        pb.prompt_time_ms = (uint32_t)get_ms_clock();
        astro_set_status("Gallery: waiting for SET");
        draw_prompt();
        return 0;
    }
    return 1;
}

void astro_playback_tick(void)
{
    if (pb.state == VIEW_IDLE) return;
    if (pb.state == VIEW_PROMPT &&
        (uint32_t)(get_ms_clock() - pb.prompt_time_ms) > PROMPT_TIMEOUT_MS) {
        close_view();
        astro_set_status("Gallery: confirmation timed out");
        return;
    }
    if (pb.state == VIEW_RUNNING && !astro_busy() &&
        (uint32_t)(get_ms_clock() - pb.run_time_ms) > 500) {
        /* Covers worker allocation errors that prevent the solve callback. */
        char reason[64];
        snprintf(reason, sizeof(reason), "%s", astro_status());
        fail_playback(reason);
    }
    if (!is_pure_play_photo_mode()) {
        if (!is_play_or_qr_mode() && pb.state != VIEW_RUNNING) pb.state = VIEW_IDLE;
        return;
    }
    switch (pb.state) {
        case VIEW_PROMPT: draw_prompt(); break;
        case VIEW_RUNNING: draw_progress(); break;
        case VIEW_RESULTS: draw_results(); break;
        case VIEW_FAILURE: draw_failure(); break;
        default: break;
    }
}
