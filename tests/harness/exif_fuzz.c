/* SPDX-License-Identifier: GPL-2.0-only */
/* ASan/UBSan fuzz driver for astro_exif_parse: mutates seed files deterministically. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "exif.h"

static uint32_t rng_state = 12345;

static uint32_t next_random(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static void mutate(uint8_t *data, uint32_t size)
{
    int edits = 1 + (int)(next_random() % 8);
    for (int i = 0; i < edits; i++) {
        uint32_t at = next_random() % size;
        switch (next_random() % 4) {
            case 0:
                data[at] = (uint8_t)next_random();
                break;
            case 1:
                data[at] ^= (uint8_t)(1u << (next_random() % 8));
                break;
            case 2:
                data[at] = 0xFF;
                break;
            default:
                data[at] = 0;
                break;
        }
    }
}

int main(int argc, char **argv)
{
    int iterations = argc > 1 ? atoi(argv[1]) : 100000;
    int parsed = 0;

    for (int seed = 2; seed < argc; seed++) {
        FILE *f = fopen(argv[seed], "rb");
        if (!f) {
            return 2;
        }
        static uint8_t original[ASTRO_EXIF_HEAD_BYTES];
        uint32_t size = (uint32_t)fread(original, 1, sizeof(original), f);
        fclose(f);
        for (int i = 0; i < iterations; i++) {
            /* Exact-size heap copy so ASan catches any read past the end. */
            uint32_t length = size - next_random() % (size / 2 + 1);
            uint8_t *copy = malloc(length);
            memcpy(copy, original, length);
            mutate(copy, length);
            astro_exif out;
            parsed += astro_exif_parse(copy, length, &out);
            free(copy);
        }
    }
    printf("parsed %d\n", parsed);
    return 0;
}
