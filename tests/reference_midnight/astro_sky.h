#ifndef ASTRO_SKY_H
#define ASTRO_SKY_H
#include "astro_core.h"
#include "astro_wcs.h"
#include <stdint.h>
#define SKY_VERIFY_STARS 96
#define SKY_FIELD_BYTES (16+SKY_VERIFY_STARS*8)
/* Accepted image scale, Q16 pixels per millidegree. For a 360-pixel-wide
 * image: 500 ~ 47 degree field width, 30000 ~ 0.8 degree. */
#define SKY_SCALE_MIN 140
#define SKY_SCALE_MAX 30000
typedef struct {uint32_t key,offset;} sky_dir_entry;
typedef struct {
    uint32_t ra; int32_t dec;   /* image centre */
    uint32_t field,quads,reads,checked;
    int matched,mse100,fov_cdeg;
    uint32_t matched_mask;
    uint16_t names[ASTRO_MAX_STARS];
    astro_wcs wcs;              /* sky -> image transform of the solution */
} sky_solution;
/* Optional limits. scale_min == 0 means the full supported range. A focal
 * length hint narrows the window: fewer false checks, earlier success. */
typedef struct {uint32_t scale_min,scale_max;} sky_options;
typedef int (*sky_read)(void*,uint32_t,void*,uint32_t);
/* Called periodically. Return zero to cancel. */
typedef int (*sky_progress)(void*,unsigned quads,unsigned checked);
int astro_sky_solve(const astro_star*,int n,int width,int height,
                    const sky_dir_entry*,unsigned nkeys,unsigned npatterns,
                    unsigned nfields,sky_read,void *patterns,
                    sky_read,void *fields,sky_progress,void *progress_ctx,
                    const sky_options*,sky_solution*);
#endif
