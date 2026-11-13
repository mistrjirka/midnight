/* SPDX-License-Identifier: GPL-2.0-only */
/* Solve the photo shown in Canon playback: Q opens a prompt, SET solves, Q hides. */
#ifndef ASTRO_PLAYBACK_H
#define ASTRO_PLAYBACK_H

/* Worker action. */
void astro_playback_run_solve(void);

/* Key callback in playback. Returns 1 to pass the key to Canon. */
unsigned int astro_playback_key(unsigned int key);

/* 1 Hz callback: Canon repaints playback, so the overlay is drawn again. */
void astro_playback_tick(void);

#endif
