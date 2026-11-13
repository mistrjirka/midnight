/* SPDX-License-Identifier: GPL-2.0-only */
/* Injected random-access file reads, so the same core code runs on the camera and host. */
#ifndef ASTRO_READER_H
#define ASTRO_READER_H

#include <stdint.h>

/* The core never asks for more than this per call. Camera readers may rely on it. */
#define ASTRO_MAX_READ_BYTES 4096

/* Reads exactly `bytes` at `offset`. Returns nonzero on success. */
typedef int (*astro_read_fn)(void *context, uint32_t offset, void *dest, uint32_t bytes);

typedef struct {
    astro_read_fn read; /* NULL: file absent */
    void *context;
} astro_file;

/* Reads any size by splitting it into ASTRO_MAX_READ_BYTES calls. Returns 0 on failure or
 * when the file is absent. */
int astro_file_read(const astro_file *file, uint32_t offset, void *dest, uint32_t bytes);

/* Little-endian field decoding for on-card records. */
uint32_t astro_get_u32(const uint8_t *p);
uint16_t astro_get_u16(const uint8_t *p);

#endif
