/* SPDX-License-Identifier: GPL-2.0-only */
/* Star focus: live sharpness readout, one-shot measurement and lens autofocus. */
#ifndef ASTRO_STAR_FOCUS_H
#define ASTRO_STAR_FOCUS_H

/* Worker actions. */
void astro_focus_run_autofocus(void);
void astro_focus_measure(void);
void astro_focus_update_score(void);

/* 1 Hz callback: draws the readout and requests the next score. */
void astro_focus_tick(void);

/* Key callbacks. Return 1 to pass the key to Canon, 0 to consume it. */
unsigned int astro_focus_key(unsigned int key);
unsigned int astro_focus_raw_key(unsigned int event_pointer);

/* An AF lens in photo Live View that accepts focus commands. */
int astro_focus_lens_ready(void);

#endif
