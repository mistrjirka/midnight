/* SPDX-License-Identifier: GPL-2.0-only */
/* Actions used by the Midnight menu layout. Keep menu.c declarative. */
#ifndef ASTRO_MENU_HANDLERS_H
#define ASTRO_MENU_HANDLERS_H

#include <menu.h>

MENU_SELECT_FUNC(astro_menu_open_intervalometer);
MENU_SELECT_FUNC(astro_menu_open_bulb_timer);
MENU_SELECT_FUNC(astro_menu_open_full_magic_lantern);
MENU_SELECT_FUNC(astro_menu_start_solve_liveview);
MENU_SELECT_FUNC(astro_menu_start_solve_file);
MENU_SELECT_FUNC(astro_menu_start_guide);
MENU_SELECT_FUNC(astro_menu_start_polar_first);
MENU_SELECT_FUNC(astro_menu_start_polar_second);
MENU_SELECT_FUNC(astro_menu_start_polar_adjust);
MENU_SELECT_FUNC(astro_menu_start_autofocus);
MENU_SELECT_FUNC(astro_menu_start_measure);
MENU_SELECT_FUNC(astro_menu_start_read_location);

MENU_UPDATE_FUNC(astro_menu_status_update);
MENU_UPDATE_FUNC(astro_menu_target_number_update);
MENU_UPDATE_FUNC(astro_menu_target_object_update);
MENU_UPDATE_FUNC(astro_menu_target_sky_update);
MENU_UPDATE_FUNC(astro_menu_polar_update);
MENU_UPDATE_FUNC(astro_menu_location_source_update);
MENU_UPDATE_FUNC(astro_menu_location_position_update);
MENU_UPDATE_FUNC(astro_menu_utc_offset_update);
MENU_UPDATE_FUNC(astro_menu_focal_source_update);
MENU_UPDATE_FUNC(astro_menu_manual_focal_update);
MENU_UPDATE_FUNC(astro_menu_last_status_update);
MENU_UPDATE_FUNC(astro_menu_last_ra_update);
MENU_UPDATE_FUNC(astro_menu_last_dec_update);
MENU_UPDATE_FUNC(astro_menu_last_lens_update);

#endif
