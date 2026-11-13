/* SPDX-License-Identifier: GPL-2.0-only */
/* Live View preview gain (photo mode only), shared with ML's Display > LV display gain. */
#ifndef ASTRO_GAIN_H
#define ASTRO_GAIN_H

#include <menu.h>

#define ASTRO_GAIN_MAX_EV 6

int astro_gain_ev(void);

/* Menu handlers for the "Live View gain" row. */
MENU_SELECT_FUNC(astro_gain_select);
MENU_UPDATE_FUNC(astro_gain_update);

#endif
