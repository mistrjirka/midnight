/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AstroML: plate solving, target guiding, polar alignment and star focus for Magic Lantern
 * on Canon 550D (1.0.9), 600D (1.0.2), 700D (1.1.5) and 6D (1.1.6).
 * done on the worker task (worker.c).
 */
#include <module.h>
#include <dryos.h>
#include <menu.h>
#include "menu.h"
#include "playback.h"
#include "result_view.h"
#include "star_focus.h"
#include "state.h"
#include "worker.h"

static unsigned int astro_init(void)
{
    if (!is_camera("550D", "1.0.9") && !is_camera("600D", "1.0.2") &&
        !is_camera("700D", "1.1.5") && !is_camera("6D", "1.1.6")) {
        return CBR_RET_ERROR;
    }
    /* This setting defaults to the 600D DISP / 6D light shortcut on old builds.
     * 550D and 700D have no equivalent raw button. Default to Off here,
     * preserving explicit INFO or half-shutter selections on those models. */
    if ((is_camera("550D", "1.0.9") || is_camera("700D", "1.1.5")) &&
        astro_cfg_focus_shortcut == ASTRO_SHORTCUT_LIGHT) {
        astro_cfg_focus_shortcut = ASTRO_SHORTCUT_OFF;
    }
    if (!astro_worker_start()) {
        return CBR_RET_ERROR;
    }
    astro_menu_add();
    return 0;
}

static unsigned int astro_deinit(void)
{
    if (!astro_worker_stop()) {
        return CBR_RET_ERROR; /* still working: refuse to unload */
    }
    astro_result_release();
    astro_menu_remove();
    return 0;
}

/* Translated keys. Return 1 to let Canon handle the key, 0 to consume it. */
static unsigned int astro_keypress(unsigned int key)
{
    if (astro_result_visible()) {
        if (key == MODULE_KEY_Q || key == MODULE_KEY_PRESS_SET) {
            astro_result_hide();
            gui_open_menu();
            return 0;
        }
        return 1;
    }
    if (astro_busy() && key == MODULE_KEY_Q) {
        astro_cancel();
        return 0;
    }
    if (!astro_playback_key(key)) {
        return 0;
    }
    return astro_focus_key(key);
}

static unsigned int astro_keypress_raw(unsigned int event_pointer)
{
    return astro_focus_raw_key(event_pointer);
}

/* Runs on ML's 1 Hz timer: keep it short, anything heavy goes to the worker. */
static unsigned int astro_seconds(unsigned int unused)
{
    if (astro_result_visible()) {
        astro_result_redraw();
        return 0;
    }
    astro_playback_tick();
    astro_focus_tick();
    return 0;
}

MODULE_INFO_START()
MODULE_INIT(astro_init)
MODULE_DEINIT(astro_deinit)
MODULE_INFO_END()

MODULE_CBRS_START()
MODULE_CBR(CBR_KEYPRESS, astro_keypress, 0)
MODULE_CBR(CBR_KEYPRESS_RAW, astro_keypress_raw, 0)
MODULE_CBR(CBR_SECONDS_CLOCK, astro_seconds, 0)
MODULE_CBRS_END()
