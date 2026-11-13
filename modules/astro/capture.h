/* SPDX-License-Identifier: GPL-2.0-only */
/* 360x240 8-bit luma frames from Live View, playback or a test file. */
#ifndef ASTRO_CAPTURE_H
#define ASTRO_CAPTURE_H

#include <stdint.h>

/* Photo-mode Live View at 1x zoom: the only mode the solver's scale assumptions fit. */
int astro_liveview_ready(void);

/* Downscaled luma of the current Live View (or playback) YUV buffer. */
int astro_capture_display(uint8_t *image);

/* ML/ASTRO/TEST.PGM or TEST<index>.PGM: strict "P5\n360 240\n255\n" header. */
int astro_capture_test_file(uint8_t *image, int index);
const char *astro_test_file_name(int index);

/* Cheap hash of the displayed playback photo, to notice when the user changes photos. */
uint32_t astro_playback_signature(void);

#endif
