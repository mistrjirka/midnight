/* SPDX-License-Identifier: GPL-2.0-only */
/* A log window drawn over the screen while a solve runs, so the user can see it working. */
#ifndef ASTRO_PROGRESS_H
#define ASTRO_PROGRESS_H

/* Closes the ML menu, opens the window and starts the clock. Until this is called, every
 * other function here does nothing, so guiding and playback solves stay quiet. */
void astro_log_begin(const char *title);

/* Appends a line, prefixed with the seconds since begin. */
void astro_log(const char *format, ...);

/* Appends a line, or rewrites it if the previous call was also a progress call. For counters. */
void astro_log_progress(const char *format, ...);

/* The next screen replaces the window: stop drawing, leave the pixels alone. */
void astro_log_handoff(void);

/* Keeps the finished log up for hold_ms (Q closes it sooner), then erases it and gives the ML
 * menu back. */
void astro_log_end(int hold_ms);

#endif
