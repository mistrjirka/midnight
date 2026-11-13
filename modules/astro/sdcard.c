/* SPDX-License-Identifier: GPL-2.0-only */
/* Bounded, exact reads from the card. */
#include <dryos.h>
#include <fio-ml.h>
#include <string.h>
#include "core/reader.h"
#include "sdcard.h"
#include "state.h"

#define READ_CHUNK 4096
#define PATH_BYTES 48

int astro_read_exact(void *file, void *dest, uint32_t bytes)
{
    uint8_t *out = dest;

    while (bytes > 0) {
        int chunk = bytes > READ_CHUNK ? READ_CHUNK : (int)bytes;
        int got = FIO_ReadFile((FILE *)file, out, chunk);
        if (got <= 0 || got > chunk) {
            return 0;
        }
        out += got;
        bytes -= (uint32_t)got;
    }
    return 1;
}

int astro_card_open(astro_card_file *f, const char *name)
{
    char path[PATH_BYTES];

    snprintf(path, sizeof(path), ASTRO_DATA_DIR "%s", name);
    f->file = FIO_OpenFile(path, O_RDONLY | O_SYNC);
    f->position = 0;
    return f->file != NULL;
}

void astro_card_close(astro_card_file *f)
{
    if (f->file) {
        FIO_CloseFile((FILE *)f->file);
        f->file = NULL;
    }
}

int astro_card_read(void *context, uint32_t offset, void *dest, uint32_t bytes)
{
    astro_card_file *f = context;

    if (!f || !f->file || bytes > ASTRO_MAX_READ_BYTES) {
        return 0;
    }
    if (f->position != offset) {
        if (FIO_SeekSkipFile((FILE *)f->file, offset, SEEK_SET) != (int64_t)offset) {
            return 0;
        }
        f->position = offset;
    }
    if (!astro_read_exact(f->file, dest, bytes)) {
        f->position = UINT32_MAX; /* unknown: force a seek next time */
        return 0;
    }
    f->position += bytes;
    return 1;
}

void astro_catalogue_open(astro_catalogue_files *c)
{
    static const char *const names[7] = {"STARS.BIN", "STARS.IDX", "STARS.NAM", "DSO.BIN",
                                         "DSO.KEY",   "DSO.NAM",   "CONST.BIN"};
    astro_file *slots[7] = {&c->files.stars,         &c->files.stars_idx, &c->files.stars_nam,
                            &c->files.dso,           &c->files.dso_key,   &c->files.dso_nam,
                            &c->files.constellations};

    memset(c, 0, sizeof(*c));
    for (int i = 0; i < 7; i++) {
        if (astro_card_open(&c->handles[i], names[i])) {
            slots[i]->read = astro_card_read;
            slots[i]->context = &c->handles[i];
        }
    }
}

void astro_catalogue_close(astro_catalogue_files *c)
{
    for (int i = 0; i < 7; i++) {
        astro_card_close(&c->handles[i]);
    }
}
