/* SPDX-License-Identifier: GPL-2.0-only */
/* The picked target object and the overlay scene of the latest solution. */
#ifndef ASTRO_OVERLAY_H
#define ASTRO_OVERLAY_H

#include "core/catalog.h"
#include "core/scene.h"
#include "solve.h"

/* Worker: resolves the configured catalogue + number (card reads). */
void astro_target_lookup(void);

/* Menu side: is the loaded target the configured one? Posts a lookup if not. */
int astro_target_current(void);
const astro_target *astro_target_get(void);
const char *astro_target_info(void); /* "Orion Nebula" / "galaxy, mag 8.9" / "Not in ..." */

/* Worker: builds `scene` for a solution, including the target if marking is enabled. */
void astro_build_overlay(const astro_solve_result *result, astro_scene *scene);

/* "M42 in view, 1.2 deg from centre" or "M42 12.3 deg away: follow the arrow". */
void astro_target_status(const astro_scene *scene, char *out, int size);

#endif
