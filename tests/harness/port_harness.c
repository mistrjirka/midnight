/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Host harness for astro/core: stdio readers that behave like the camera's (each call is
 * limited to ASTRO_MAX_READ_BYTES and counted), plus thin entry points for ctypes.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "catalog.h"
#include "scene.h"
#include "sky_solver.h"
#include "star_detect.h"

typedef struct {
    FILE *file;
    unsigned reads;
    unsigned oversized; /* requests above the camera's per-call limit */
    int traced;
} host_file;

#define TRACE_MAX 2000000
static uint32_t trace_log[TRACE_MAX][3]; /* offset, bytes, file (0 patterns, 1 fields) */
static unsigned trace_count;

static int host_read(void *context, uint32_t offset, void *dest, uint32_t bytes)
{
    host_file *f = context;

    f->reads++;
    if (f->traced && trace_count < TRACE_MAX) {
        trace_log[trace_count][0] = offset;
        trace_log[trace_count][1] = bytes;
        trace_log[trace_count++][2] = (uint32_t)f->traced - 1;
    }
    if (bytes > ASTRO_MAX_READ_BYTES) {
        f->oversized++;
        return 0;
    }
    return !fseek(f->file, (long)offset, SEEK_SET) && fread(dest, 1, bytes, f->file) == bytes;
}

/* ---- plate solver ---- */

static struct {
    astro_sky_dir_entry *dir;
    host_file patterns;
    host_file fields;
    astro_sky_index index;
    astro_sky_workspace *workspace;
} sky;

void harness_close_sky(void)
{
    if (sky.patterns.file) {
        fclose(sky.patterns.file);
    }
    if (sky.fields.file) {
        fclose(sky.fields.file);
    }
    free(sky.dir);
    free(sky.workspace);
    memset(&sky, 0, sizeof(sky));
}

static FILE *open_index_file(const char *root, const char *prefix, const char *ext,
                             const char *magic, uint32_t header[4])
{
    char path[1024];

    snprintf(path, sizeof(path), "%s/%s.%s", root, prefix, ext);
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    if (fread(header, 4, 4, f) != 4 || memcmp(header, magic, 4)) {
        fclose(f);
        return NULL;
    }
    return f;
}

int harness_open_sky(const char *root, const char *prefix)
{
    uint32_t header[4];

    harness_close_sky();
    FILE *dir = open_index_file(root, prefix, "DIR", "SDIR", header);
    if (!dir) {
        return -1;
    }
    sky.index.nkeys = header[2];
    sky.index.npatterns = header[3];
    sky.dir = malloc((sky.index.nkeys + 1) * sizeof(*sky.dir));
    int ok = sky.dir &&
             fread(sky.dir, sizeof(*sky.dir), sky.index.nkeys + 1, dir) == sky.index.nkeys + 1;
    fclose(dir);
    sky.fields.file = open_index_file(root, prefix, "FLD", "SFLD", header);
    sky.index.nfields = header[2];
    sky.patterns.file = open_index_file(root, prefix, "PAT", "SPAT", header);
    sky.workspace = malloc(astro_sky_workspace_size());
    if (!ok || !sky.fields.file || !sky.patterns.file || !sky.workspace) {
        harness_close_sky();
        return -2;
    }
    sky.index.dir = sky.dir;
    sky.patterns.traced = 1;
    sky.fields.traced = 2;
    sky.index.read_patterns = host_read;
    sky.index.patterns_context = &sky.patterns;
    sky.index.read_fields = host_read;
    sky.index.fields_context = &sky.fields;
    return 0;
}

/* Card reads since the last reset, as {offset, bytes, file} triples. */
const uint32_t *harness_trace(unsigned *count)
{
    *count = trace_count;
    return &trace_log[0][0];
}

void harness_trace_reset(void)
{
    trace_count = 0;
}

int harness_solve(const astro_star *stars, int count, int width, int height,
                  const astro_sky_options *options, astro_sky_solution *out)
{
    if (!sky.dir) {
        return -16;
    }
    return astro_sky_solve(stars, count, width, height, &sky.index, options, NULL, NULL,
                           sky.workspace, out);
}

unsigned harness_sky_oversized_reads(void)
{
    return sky.patterns.oversized + sky.fields.oversized;
}

/* ---- overlay data ---- */

#define NUM_SKY_FILES 7
static host_file overlay_files[NUM_SKY_FILES];
static astro_sky_files files;

int harness_open_files(const char *root)
{
    static const char *const names[NUM_SKY_FILES] = {
        "STARS.BIN", "STARS.IDX", "STARS.NAM", "DSO.BIN", "DSO.KEY", "DSO.NAM", "CONST.BIN"};
    astro_file *slots[NUM_SKY_FILES] = {&files.stars,         &files.stars_idx, &files.stars_nam,
                                        &files.dso,           &files.dso_key,   &files.dso_nam,
                                        &files.constellations};
    char path[1024];

    memset(&files, 0, sizeof(files));
    for (int i = 0; i < NUM_SKY_FILES; i++) {
        snprintf(path, sizeof(path), "%s/%s", root, names[i]);
        memset(&overlay_files[i], 0, sizeof(overlay_files[i]));
        overlay_files[i].file = fopen(path, "rb");
        if (overlay_files[i].file) {
            slots[i]->read = host_read;
            slots[i]->context = &overlay_files[i];
        }
    }
    return files.stars.read && files.dso.read && files.constellations.read;
}

unsigned harness_overlay_oversized_reads(void)
{
    unsigned total = 0;
    for (int i = 0; i < NUM_SKY_FILES; i++) {
        total += overlay_files[i].oversized;
    }
    return total;
}

int harness_lookup(int catalogue, int number, astro_target *out)
{
    return astro_dso_lookup(&files, catalogue, number, out);
}

void harness_common_name(const astro_dso *object, char *out, int size)
{
    astro_dso_common_name(&files, object, out, size);
}

int harness_scene(const astro_wcs *wcs, int width, int height, const astro_overlay_opts *opts,
                  const astro_target *target, astro_scene *scene)
{
    return astro_build_scene(wcs, width, height, opts, target, &files, scene);
}

/* ---- detection ---- */

int harness_detect(const uint8_t *image, int width, int height, int stride, astro_star *out,
                   int max_stars)
{
    astro_detect_scratch *scratch = malloc(sizeof(*scratch));
    int count =
        scratch ? astro_detect_stars(image, width, height, stride, scratch, out, max_stars) : -1;
    free(scratch);
    return count;
}
