/* SPDX-License-Identifier: GPL-2.0-only */
/* Focus shortcut on the 6D's LCD-light button, only in photo Live View. */
#include "shortcut.h"

astro_shortcut_action astro_shortcut_decide(int raw_key, const astro_shortcut_state *state)
{
    if (!state->enabled || !state->photo_liveview || state->menu_open || state->busy ||
        raw_key != ASTRO_KEY_6D_LIGHT) {
        return ASTRO_SHORTCUT_PASS;
    }
    return state->electronic_af ? ASTRO_SHORTCUT_AUTOFOCUS : ASTRO_SHORTCUT_TOGGLE_READOUT;
}
