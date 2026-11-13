/* SPDX-License-Identifier: GPL-2.0-only */
/* Card file access for the portable core's injected readers. */
#ifndef ASTRO_SDCARD_H
#define ASTRO_SDCARD_H

#include <stdint.h>
#include "core/catalog.h"

typedef struct {
    void *file;        /* FILE * from FIO_OpenFile, NULL when closed or absent */
    uint32_t position; /* current offset: sequential reads skip the costly FAT seek */
} astro_card_file;

/* Opens ML/ASTRO/<name> read-only. Returns 0 if the file does not exist. */
int astro_card_open(astro_card_file *f, const char *name);
void astro_card_close(astro_card_file *f);

/* astro_read_fn for astro_card_file contexts. */
int astro_card_read(void *context, uint32_t offset, void *dest, uint32_t bytes);

/* Reads exactly `bytes` from the current position; the card may return partial reads. */
int astro_read_exact(void *file, void *dest, uint32_t bytes);

/* The seven overlay/catalogue files. Missing files leave their reader NULL. */
typedef struct {
    astro_card_file handles[7];
    astro_sky_files files;
} astro_catalogue_files;

void astro_catalogue_open(astro_catalogue_files *c);
void astro_catalogue_close(astro_catalogue_files *c);

#endif
