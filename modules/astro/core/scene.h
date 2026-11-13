/* SPDX-License-Identifier: GPL-2.0-only */
/* Overlay scene for a solved frame: what to draw, in image coordinates. Built once per
 * solve by the worker; drawing it never touches the card. */
#ifndef ASTRO_SCENE_H
#define ASTRO_SCENE_H

#include <stdint.h>
#include "catalog.h"
#include "wcs.h"

typedef struct {
    int stars; /* layer switches */
    int dso;
    int constellations;
    int star_mag10; /* label stars up to this magnitude * 10 */
    int dso_mag10;  /* NGC/IC objects up to this magnitude * 10 */
} astro_overlay_opts;

enum { ASTRO_LABEL_STAR = 1, ASTRO_LABEL_DSO, ASTRO_LABEL_CONSTELLATION, ASTRO_LABEL_TARGET };

enum { ASTRO_TARGET_NONE = 0, ASTRO_TARGET_IN_FRAME = 1, ASTRO_TARGET_OUTSIDE = 2 };

#define ASTRO_SCENE_LINES    384
#define ASTRO_SCENE_ELLIPSES 48
#define ASTRO_SCENE_LABELS   64

/* All positions are Q4 (1/16 pixel) in solver image coordinates. */
typedef struct {
    int16_t x1, y1, x2, y2;
} astro_line;

/* Centre plus semi-axis vectors. */
typedef struct {
    int16_t x, y;
    int16_t ax, ay;
    int16_t bx, by;
    uint8_t kind;
    uint8_t pad;
} astro_ellipse;

typedef struct {
    int16_t x, y;
    uint8_t kind;
    uint8_t priority; /* lower is more important */
    char text[ASTRO_NAME_BYTES];
} astro_label;

typedef struct {
    int width;
    int height;
    int nlines;
    int nellipses;
    int nlabels;
    astro_line lines[ASTRO_SCENE_LINES];
    astro_ellipse ellipses[ASTRO_SCENE_ELLIPSES];
    astro_label labels[ASTRO_SCENE_LABELS]; /* most important first */
    int target;                             /* ASTRO_TARGET_* */
    int16_t target_x, target_y;             /* Q4, when in frame */
    int16_t target_dx, target_dy;           /* Q14 unit vector towards the target */
    uint32_t target_separation;             /* 1e-4 degree from the image centre */
    char target_label[ASTRO_NAME_BYTES];
} astro_scene;

/* Returns 0 on success, -1 on a corrupt data file. Missing files leave their layer empty. */
int astro_build_scene(const astro_wcs *wcs, int width, int height, const astro_overlay_opts *opts,
                      const astro_target *target, const astro_sky_files *files, astro_scene *scene);

#endif
