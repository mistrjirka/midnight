/* Sky overlay scene: what to draw over a solved frame. GPL-2.0-or-later.
 * Portable (no Canon/ML calls): file access is injected, so the identical
 * code is tested on the desktop. The scene is built once per solve by the
 * worker task; drawing it never touches the SD card. */
#ifndef ASTRO_OVERLAY_H
#define ASTRO_OVERLAY_H
#include <stdint.h>
#include "astro_wcs.h"

/* Catalogue IDs, shared with tools/make_dso.py. Picker order. */
enum { ASTRO_CAT_M=1, ASTRO_CAT_NGC, ASTRO_CAT_IC, ASTRO_CAT_C, ASTRO_CAT_B,
       ASTRO_CAT_SH2, ASTRO_CAT_COUNT=ASTRO_CAT_SH2 };
extern const uint16_t astro_catalogue_max[ASTRO_CAT_COUNT+1];
/* Object types, shared with tools/make_dso.py. */
enum { ASTRO_DSO_GALAXY=1, ASTRO_DSO_OPEN, ASTRO_DSO_GLOBULAR, ASTRO_DSO_PLANETARY,
       ASTRO_DSO_NEBULA, ASTRO_DSO_REFLECTION, ASTRO_DSO_DARK, ASTRO_DSO_SNR,
       ASTRO_DSO_CLUSTER_NEBULA, ASTRO_DSO_ASTERISM, ASTRO_DSO_STAR };
#define ASTRO_MAG_UNKNOWN 32767

typedef int (*astro_read)(void *ctx, uint32_t offset, void *dest, uint32_t bytes);
typedef struct { astro_read read; void *ctx; } astro_file;  /* read==0: absent */
typedef struct {
    astro_file stars, stars_idx, stars_nam;   /* STARS.BIN / .IDX / .NAM */
    astro_file dso, dso_key, dso_nam;         /* DSO.BIN / .KEY / .NAM */
    astro_file constellations;                /* CONST.BIN */
} astro_sky_files;

/* One DSO.BIN record (24 bytes, little endian). */
typedef struct {
    uint32_t ra; int32_t dec;
    uint16_t maj, min;        /* 0.1 arcmin */
    int16_t pa, mag10;        /* degrees (-1 unknown); magnitude*10 */
    uint8_t type, cat;        /* best designation */
    uint16_t num, name, reserved;
} astro_dso;

typedef struct {
    int valid;
    astro_dso obj;
    char label[24];           /* "M42", "NGC 7000", "Sh2-155" */
} astro_target;

typedef struct {
    int stars, dso, constellations;  /* layer switches */
    int star_mag10;                  /* label stars up to magnitude*10 */
    int dso_mag10;                   /* NGC/IC objects up to magnitude*10 */
} astro_overlay_opts;

enum { ASTRO_LABEL_STAR=1, ASTRO_LABEL_DSO, ASTRO_LABEL_CONSTELLATION, ASTRO_LABEL_TARGET };
#define ASTRO_SCENE_LINES 384
#define ASTRO_SCENE_ELLIPSES 48
#define ASTRO_SCENE_LABELS 64
/* All positions are Q4 (1/16 pixel) in solver image coordinates. */
typedef struct { int16_t x1, y1, x2, y2; } astro_line;
typedef struct { int16_t x, y, ax, ay, bx, by; uint8_t kind, pad; } astro_ellipse; /* semi-axis vectors */
typedef struct { int16_t x, y; uint8_t kind, priority; char text[24]; } astro_label;
typedef struct {
    int width, height;
    int nlines, nellipses, nlabels;
    astro_line lines[ASTRO_SCENE_LINES];
    astro_ellipse ellipses[ASTRO_SCENE_ELLIPSES];
    astro_label labels[ASTRO_SCENE_LABELS];   /* most important first */
    /* Target: in frame (position + ellipse) or an arrow from the centre. */
    int target;                  /* 0 none, 1 in frame, 2 outside */
    int16_t target_x, target_y;  /* Q4, valid when in frame */
    int16_t target_dx, target_dy;/* Q14 unit vector towards target */
    uint32_t target_sep;         /* 1e-4 degree from image centre */
    char target_label[24];
} astro_scene;

void astro_designation(int cat, int num, char *out, int size);
const char *astro_dso_type_name(int type);
/* Binary search of DSO.KEY, then one DSO.BIN record read.
 * Returns 1 found, 0 not in catalogue, -1 missing/corrupt files. */
int astro_dso_lookup(const astro_sky_files *files, int cat, int num, astro_target *out);
/* Copies the common name ("Orion Nebula") or an empty string. */
void astro_dso_common_name(const astro_sky_files *files, const astro_dso *obj, char *out, int size);
/* Returns 0 on success, -1 on a corrupt data file. Missing files simply
 * leave their layer empty. */
int astro_build_scene(const astro_wcs *wcs, int width, int height,
                      const astro_overlay_opts *opts, const astro_target *target,
                      const astro_sky_files *files, astro_scene *scene);
#endif
