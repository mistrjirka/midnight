/* SPDX-License-Identifier: GPL-2.0-only */
/* Shared internals of scene.c and scene_layers.c. Not a public API. */
#ifndef ASTRO_SCENE_INTERNAL_H
#define ASTRO_SCENE_INTERNAL_H

#include "scene.h"

#define SCENE_CHUNK_BYTES 1536

typedef struct {
    astro_scene *scene;
    const astro_wcs *wcs;
    const astro_sky_files *files;
    const astro_overlay_opts *opts;
    const astro_target *target;
    const uint16_t *tiles; /* sky tiles overlapping the frame */
    int ntiles;
    uint32_t *tile_offsets;
    uint8_t *chunk;
} scene_ctx;

int astro_scene_in_frame(const scene_ctx *ctx, int32_t x_q4, int32_t y_q4, int32_t margin_q4);
void astro_scene_add_label(scene_ctx *ctx, int32_t x_q4, int32_t y_q4, int kind, int priority,
                           const char *text);
void astro_scene_add_line(scene_ctx *ctx, int64_t x1, int64_t y1, int64_t x2, int64_t y2);
int astro_scene_ellipse_axes(const scene_ctx *ctx, const astro_dso *object, astro_ellipse *out);
int astro_scene_load_tile_offsets(scene_ctx *ctx, const astro_file *file, uint32_t offset,
                                  uint32_t count);

int astro_scene_add_stars(scene_ctx *ctx);
int astro_scene_add_dso(scene_ctx *ctx);
int astro_scene_add_constellations(scene_ctx *ctx);

#endif
