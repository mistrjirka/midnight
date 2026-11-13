/* SPDX-License-Identifier: GPL-2.0-only */
/* Polar alignment: two solves around the RA axis, then live alt/az adjustment. */
#ifndef ASTRO_POLAR_H
#define ASTRO_POLAR_H

/* Worker: solve frame A (second == 0) or frame B and compute the mount axis. */
void astro_polar_capture(int second);

/* Worker: live loop showing the axis-to-pole arrow and the knob corrections. */
void astro_polar_adjust(void);

/* Menu readouts. */
int astro_polar_step(void); /* 0 nothing, 1 A captured, 2 axis known */
const char *astro_polar_summary(void);

#endif
