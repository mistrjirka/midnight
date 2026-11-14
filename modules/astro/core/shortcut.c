/* SPDX-License-Identifier: GPL-2.0-only */
/* Focus action policy after the camera-specific raw key has been verified. */
#include "shortcut.h"

astro_shortcut_action astro_shortcut_decide(const astro_shortcut_state *state)
{
    if (!state->enabled || !state->photo_liveview || state->menu_open || state->busy) {
        return ASTRO_SHORTCUT_PASS;
    }
    return state->electronic_af ? ASTRO_SHORTCUT_AUTOFOCUS : ASTRO_SHORTCUT_TOGGLE_READOUT;
}
