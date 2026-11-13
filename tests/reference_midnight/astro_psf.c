#include "astro_psf.h"
#include <string.h>

/* Separation between compact stars and broad/trail noise by a
 * Difference of Gaussians (DoG). Both 1D filters sum to 1024.
 * Store horizontal passes at Q8. Vertical produces Q4 differences.
 *
 * A 31-row ring (~45 KiB) avoids two full-image Gaussian buffers.
 * Response scratch is 360*240 signed Q4 (~169 KiB). This is only
 * used by the plate solver, NEVER in the autofocus path.
 */
#define PSF_W 360
#define PSF_H 240
#define PSF_R 15
#define PSF_RING 31
static uint16_t psf_hsmall[PSF_RING][PSF_W];
static uint16_t psf_hlarge[PSF_RING][PSF_W];
static int16_t psf_response[PSF_W*PSF_H];

static const uint16_t psf_small[13] = {
     0,0,5,29,105,226,294,226,105,29,5,0,0
};
static const uint16_t psf_large[31] = {
     0,0,0,1,2,3,7,12,20,31,45,62,79,94,104,104,
     104,94,79,62,45,31,20,12,7,3,2,1,0,0,0
};
static int bound(int x,int limit) {
    return x<0?0:(x>=limit?limit-1:x);
}
static int absi(int x) {return x<0?-x:x;}
int astro_find_stars_psf(const uint8_t *im,int w,int h,int stride,
                        astro_star *out,int max_stars)
{
    int n=0;
    if(!im || !out || w<32 || h<32 || w>PSF_W || h>PSF_H ||
       stride<w || max_stars<1)return 0;
    if(max_stars>ASTRO_MAX_STARS)max_stars=ASTRO_MAX_STARS;

    /* Fill the two horizontal Q8 Gaussian filters, reusing each row slot
     * after the corresponding vertical response has been computed. */
    for(int step=0;step<h+PSF_R;step++) {
        int sy=bound(step,h),slot=step%PSF_RING;
        const uint8_t *row=im+sy*stride;
        for(int x=0;x<w;x++) {
            uint32_t sum_small=0,sum_large=0;
            if(x>=12 && x<w-12){
                /* Only the 24 edge pixels need clipping. Zero Gaussian
                 * taps are omitted without changing the integer sums. */
                const uint8_t *p=row+x;
                for(int j=-4;j<=4;j++)sum_small+=(uint32_t)psf_small[j+6]*p[j];
                for(int j=-12;j<=12;j++)sum_large+=(uint32_t)psf_large[j+PSF_R]*p[j];
            } else {
                for(int j=-4;j<=4;j++)sum_small+=(uint32_t)psf_small[j+6]*row[bound(x+j,w)];
                for(int j=-12;j<=12;j++)sum_large+=(uint32_t)psf_large[j+PSF_R]*row[bound(x+j,w)];
            }
            psf_hsmall[slot][x]=(uint16_t)((sum_small+2)/4);
            psf_hlarge[slot][x]=(uint16_t)((sum_large+2)/4);
        }
        if(step<PSF_R)continue;
        int y=step-PSF_R;
        /* Ring-buffer modulo and boundary decisions are invariant across
         * a whole row. Computing them per pixel made millions of slow
         * software divisions on ARMv5 under -Os. */
        const uint16_t *small_rows[9],*large_rows[25];
        for(int k=-4;k<=4;k++)small_rows[k+4]=psf_hsmall[bound(y+k,h)%PSF_RING];
        for(int k=-12;k<=12;k++)large_rows[k+12]=psf_hlarge[bound(y+k,h)%PSF_RING];
        for(int x=0;x<w;x++) {
            uint32_t small=0,large=0;
            for(int k=0;k<9;k++)small+=(uint32_t)psf_small[k+2]*small_rows[k][x];
            for(int k=0;k<25;k++)large+=(uint32_t)psf_large[k+3]*large_rows[k][x];
            psf_response[y*w+x]=(int16_t)((int32_t)(small>>14)-(int32_t)(large>>14));
        }
    }

    /* Strict DoG local maximum, with deterministic plateau selection.
     * The 8px border and 9x9 nonmaximum suppression exclude partial
     * sources and repeated detections of a single star. */
    for(int y=8;y<h-8;y++)for(int x=8;x<w-8;x++) {
        int v=psf_response[y*w+x];
        if(v<=16)continue; /* at least one gray level above broad halo */
        for(int dy=-4;dy<=4;dy++)for(int dx=-4;dx<=4;dx++) {
            if(!dx&&!dy)continue;
            int neighbor=psf_response[(y+dy)*w+x+dx];
            if(neighbor>v || (neighbor==v &&
                (dy<0 || (dy==0 && dx<0))))goto next_pixel;
        }
        {
            int duplicate=0;
            for(int i=0;i<n;i++) {
                if(absi(out[i].x-x)<=5 && absi(out[i].y-y)<=5) {
                    duplicate=1;
                    break;
                }
            }
            if(duplicate)goto next_pixel;
        }
        if(n<max_stars) {
            out[n].x=x;out[n].y=y;out[n].flux=v;
            n++;
        } else if(v<=out[n-1].flux)continue;
        else {
            out[n-1].x=x;out[n-1].y=y;out[n-1].flux=v;
        }
        for(int i=n-1;i>0&&out[i].flux>out[i-1].flux;i--) {
            astro_star tmp=out[i];out[i]=out[i-1];out[i-1]=tmp;
        }
    next_pixel:;
    }
    return n;
}
