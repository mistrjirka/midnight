/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AstroML: plate solving, target guiding, polar alignment and star focus for Magic Lantern
 * on Canon EOS 550D/600D/650D/700D/100D/60D/1100D/70D/6D.
 * Heavy work runs on the worker task (worker.c).
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
    const astro_camera_profile *camera = astro_camera_detect();
    if (!camera) {
        return CBR_RET_ERROR;
    }
    /* The legacy special-button shortcut only exists on two cameras.
     * Keep explicit INFO or half-shutter choices on other bodies. */
    if (camera->raw_focus_key == ASTRO_FOCUS_KEY_NONE &&
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
