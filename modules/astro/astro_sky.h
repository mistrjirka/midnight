#ifndef ASTRO_SKY_H
#define ASTRO_SKY_H
#include "astro_core.h"
#include <stdint.h>
#define SKY_VERIFY_STARS 96
#define SKY_FIELD_BYTES (16+SKY_VERIFY_STARS*8)
typedef struct {uint32_t key,offset;} sky_dir_entry;
typedef struct {
    uint32_t ra; int32_t dec;
    uint32_t field,quads,reads,checked;
    int matched,mse100,fov_cdeg;
    uint32_t matched_mask;
    uint16_t names[ASTRO_MAX_STARS];
} sky_solution;
typedef int (*sky_read)(void*,uint32_t,void*,uint32_t);
/* Called periodically. Return zero to cancel. */
typedef int (*sky_progress)(void*,unsigned quads,unsigned checked);
int astro_sky_solve(const astro_star*,int n,int width,int height,
                    const sky_dir_entry*,unsigned nkeys,unsigned npatterns,
                    unsigned nfields,sky_read,void *patterns,
                    sky_read,void *fields,sky_progress,void *progress_ctx,
                    sky_solution*);
#endif
