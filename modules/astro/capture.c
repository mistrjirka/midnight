/* SPDX-License-Identifier: GPL-2.0-only */
/* Frame acquisition. Canon's display buffers are UYVY: luma is byte 1 of each pixel pair. */
#include <dryos.h>
#include <propvalues.h>
#include <fio-ml.h>
#include <gui-common.h>
#include <property.h>
#include <shoot.h>
#include <vram.h>
#include <string.h>
#include "capture.h"
#include "sdcard.h"
#include "state.h"

#define MAX_BUFFER_WIDTH  4096
#define MAX_BUFFER_HEIGHT 2160
#define TEST_FILES        6
#define PGM_HEADER        "P5\n360 240\n255\n"
#define PGM_HEADER_BYTES  15
#define FNV_OFFSET        2166136261u
#define FNV_PRIME         16777619u

static const char *const test_files[TEST_FILES] = {"TEST.PGM",  "TEST1.PGM", "TEST2.PGM",
                                                   "TEST3.PGM", "RAW1.PGM",  "RAW2.PGM"};

int astro_liveview_ready(void)
{
    return lv && !is_movie_mode() && lv_dispsize == 1;
}

/* Only trust buffers whose geometry ML reports consistently. */
static struct vram_info *display_buffer(void)
{
    struct vram_info *v = get_yuv422_vram();

    if (!v || !v->vram || v->width < ASTRO_IMAGE_WIDTH || v->height < ASTRO_IMAGE_HEIGHT ||
        v->pitch < v->width * 2 || v->width > MAX_BUFFER_WIDTH || v->height > MAX_BUFFER_HEIGHT) {
        return NULL;
    }
    return v;
}

int astro_capture_display(uint8_t *image)
{
    struct vram_info *v;

    if (!image || (!lv && !is_pure_play_photo_mode()) || !(v = display_buffer())) {
        return 0;
    }
    /* Average each block of source pixels. Keeping every 2nd pixel instead lets hot pixels and
     * noise through at full strength and drops faint stars, which swamps the solver. */
    int block_w = v->width / ASTRO_IMAGE_WIDTH;
    int block_h = v->height / ASTRO_IMAGE_HEIGHT;
    for (int y = 0; y < ASTRO_IMAGE_HEIGHT; y++) {
        const uint8_t *top = v->vram + (y * v->height / ASTRO_IMAGE_HEIGHT) * v->pitch;
        uint8_t *out = image + y * ASTRO_IMAGE_WIDTH;
        for (int x = 0; x < ASTRO_IMAGE_WIDTH; x++) {
            int first = x * v->width / ASTRO_IMAGE_WIDTH;
            int sum = 0;
            for (int dy = 0; dy < block_h; dy++) {
                const uint8_t *row = top + dy * v->pitch + first * 2 + 1;
                for (int dx = 0; dx < block_w; dx++) {
                    sum += row[dx * 2];
                }
            }
            out[x] = (uint8_t)(sum / (block_w * block_h));
        }
    }
    return 1;
}

const char *astro_test_file_name(int index)
{
    return test_files[(unsigned)index < TEST_FILES ? index : 0];
}

int astro_capture_test_file(uint8_t *image, int index)
{
    char header[PGM_HEADER_BYTES];
    astro_card_file f;
    const char *name = astro_test_file_name(index);

    if (!astro_card_open(&f, name)) {
        astro_set_status("%s not found", name);
        return 0;
    }
    int ok = astro_read_exact(f.file, header, PGM_HEADER_BYTES) &&
             !memcmp(header, PGM_HEADER, PGM_HEADER_BYTES) &&
             astro_read_exact(f.file, image, ASTRO_IMAGE_BYTES);
    astro_card_close(&f);
    if (!ok) {
        astro_set_status("%s: need a 360x240 8-bit PGM", name);
    }
    return ok;
}

uint32_t astro_playback_signature(void)
{
    struct vram_info *v;
    uint32_t hash = FNV_OFFSET;

    if (!is_pure_play_photo_mode() || !(v = display_buffer())) {
        return 0;
    }
    /* A sparse grid of luma samples changes whenever another photo is shown. */
    for (int y = 14; y < ASTRO_IMAGE_HEIGHT; y += 13) {
        const uint8_t *row = v->vram + (y * v->height / ASTRO_IMAGE_HEIGHT) * v->pitch;
        for (int x = 13; x < ASTRO_IMAGE_WIDTH; x += 19) {
            hash ^= row[(x * v->width / ASTRO_IMAGE_WIDTH) * 2 + 1];
            hash *= FNV_PRIME;
        }
    }
    return hash;
}
