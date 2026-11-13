/* SPDX-License-Identifier: GPL-2.0-only */
/* Frozen, annotated result screen for one-shot solves. Q or SET closes it. */
#ifndef ASTRO_RESULT_VIEW_H
#define ASTRO_RESULT_VIEW_H

#include <stdint.h>
#include "core/scene.h"
#include "solve.h"

/* Keeps a copy of the frame as captured, before trail masking alters it. */
int astro_result_keep_frame(const uint8_t *image);

/* Shows the kept frame with this solution and scene, leaving the menu. */
void astro_result_show(const astro_solve_result *result, const astro_scene *scene);

int astro_result_visible(void);
void astro_result_hide(void);
void astro_result_redraw(void); /* from the 1 Hz callback: Canon may repaint the screen */

#endif
