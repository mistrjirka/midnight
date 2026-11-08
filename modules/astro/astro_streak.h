/* Low-memory trail rejection, used only when detecting stars for solving.
 * Never modifies the sensor/LiveView buffers. No image data is stored. */
#ifndef ASTRO_STREAK_H
#define ASTRO_STREAK_H
#include <stdint.h>
/* Mutates only the caller-owned 8-bit plate-solving scratch image.
 * Returns 1 if a long, continuous streak was masked; 0 if none.
 * Only supports up to 720x480 to bound processing on camera. */
int astro_mask_streak(uint8_t *im,int w,int h,int stride);
#endif
