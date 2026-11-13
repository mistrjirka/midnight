/* SPDX-License-Identifier: GPL-2.0-only */
/* Sky background estimate shared by detection, trail masking and the focus metric. */
#include "star.h"

#define SAMPLE_STEP   5
#define SAMPLE_MARGIN 2

int astro_sky_background(const uint8_t *image, int width, int height, int stride)
{
    uint16_t histogram[256] = {0};
    int count = 0;
    int cumulative = 0;

    /* A sparse grid is enough for a median and skips most isolated hot pixels. */
    for (int y = SAMPLE_MARGIN; y < height - SAMPLE_MARGIN; y += SAMPLE_STEP) {
        for (int x = SAMPLE_MARGIN; x < width - SAMPLE_MARGIN; x += SAMPLE_STEP) {
            histogram[image[y * stride + x]]++;
            count++;
        }
    }
    for (int level = 0; level < 256; level++) {
        cumulative += histogram[level];
        if (cumulative >= count / 2) {
            return level;
        }
    }
    return 0;
}
