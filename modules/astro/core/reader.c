/* SPDX-License-Identifier: GPL-2.0-only */
/* Bounded reads and little-endian decoding. */
#include "reader.h"

int astro_file_read(const astro_file *file, uint32_t offset, void *dest, uint32_t bytes)
{
    uint8_t *out = (uint8_t *)dest;

    if (!file || !file->read) {
        return 0;
    }
    while (bytes > 0) {
        uint32_t chunk = bytes > ASTRO_MAX_READ_BYTES ? ASTRO_MAX_READ_BYTES : bytes;
        if (!file->read(file->context, offset, out, chunk)) {
            return 0;
        }
        offset += chunk;
        out += chunk;
        bytes -= chunk;
    }
    return 1;
}

uint32_t astro_get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

uint16_t astro_get_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}
