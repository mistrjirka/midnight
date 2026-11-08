#include "astro_core.h"
#include <string.h>

#define MIN2(a,b) ((a)<(b)?(a):(b))
#define ABSI(a) ((a)<0?-(a):(a))
static int64_t sq(int64_t x) { return x*x; }

/* Percentile background from a sparse sample. Rejects isolated hot pixels. */
static int background(const uint8_t *im, int w, int h, int stride)
{
    uint16_t hist[256] = {0};
    int count = 0;
    int x, y, b, sum = 0;
    for (y = 2; y < h-2; y += 5)
        for (x = 2; x < w-2; x += 5) {
            hist[im[y*stride+x]]++;
            count++;
        }
    for (b=0; b<256; b++) {
        sum += hist[b];
        if (sum >= count/2) return b;
    }
    return 0;
}
int astro_find_stars(const uint8_t *im, int w, int h, int stride,
                     astro_star *out, int max_stars)
{
    int x, y, dx, dy, n=0;
    int bg;
    if (!im || !out || w < 32 || h < 32 || stride < w || max_stars < 1)
        return 0;
    max_stars = MIN2(max_stars, ASTRO_MAX_STARS);
    bg = background(im,w,h,stride);
    for (y=4; y<h-4; y+=1) for (x=4; x<w-4; x+=1) {
        int peak=im[y*stride+x], flux=0, wx=0, wy=0;
        int brighter_neighbours=0, duplicate=0, i;
        if (peak < bg+12 || peak < 15) continue;
        /* Strict local maximum with deterministic handling of flat pixels. */
        for (dy=-2; dy<=2; dy++) for (dx=-2; dx<=2; dx++) {
            int p = im[(y+dy)*stride+x+dx];
            if ((dx || dy) && (p>peak ||
                (p==peak && (dy<0 || (dy==0 && dx<0)))))
                goto next_pixel;
            if ((dx || dy) && p>bg+4) brighter_neighbours++;
        }
        if (brighter_neighbours < 2) continue;
        for (dy=-3; dy<=3; dy++) for (dx=-3; dx<=3; dx++) {
            int p=(int)im[(y+dy)*stride+x+dx] - bg - 3;
            if (p>0) { flux+=p; wx+=(x+dx)*p; wy+=(y+dy)*p; }
        }
        if (flux<28) continue;
        {
            int cx=(wx+flux/2)/flux, cy=(wy+flux/2)/flux;
            for (i=0; i<n; i++) if (ABSI(out[i].x-cx)<=5 && ABSI(out[i].y-cy)<=5) {
                duplicate=1;
                if (flux>out[i].flux) {
                    out[i].x=cx; out[i].y=cy;
                    out[i].flux=(uint16_t)MIN2(flux,65535);
                }
                break;
            }
            if (duplicate) continue;
            if (n < max_stars) {
                out[n].x=cx; out[n].y=cy;
                out[n].flux=(uint16_t)MIN2(flux,65535);
                n++;
            } else if (flux <= out[n-1].flux) continue;
            else {
                out[n-1].x=cx; out[n-1].y=cy;
                out[n-1].flux=(uint16_t)MIN2(flux,65535);
            }
            for (i=n-1; i>0 && out[i].flux>out[i-1].flux; i--) {
                astro_star tmp=out[i]; out[i]=out[i-1]; out[i-1]=tmp;
            }
        }
next_pixel: ;
    }
    return n;
}

/* A point spread function (PSF) is the star's light distribution. Single
 * hot pixels are excluded because real stars spread over several pixels.
 * A fixed target remains locked through all autofocus samples. */
int astro_focus_metric_locked(const uint8_t *im,int w,int h,int stride,
                              int *star_x,int *star_y,int radius)
{
    int bg, px=-1, py=-1, peak=-1, peakdist=0;
    if(!im||!star_x||!star_y||w<32||h<32||stride<w||
       radius<1||radius>15||*star_x<8||*star_x>=w-8||
       *star_y<8||*star_y>=h-8) return -1;
    bg=background(im,w,h,stride);
    int ox=*star_x,oy=*star_y;
    /* Candidate local maxima must have an extended bright footprint. */
    for(int y=oy-radius;y<=oy+radius;y++) {
        if(y<8||y>=h-8)continue;
        for(int x=ox-radius;x<=ox+radius;x++) {
            if(x<8||x>=w-8)continue;
            int v=im[y*stride+x],neighbors=0,rank,dist;
            if(v<bg+18||v<24)continue;
            int cut=bg+(v-bg)/5;
            for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){
                if(!dx&&!dy)continue;
                int t=im[(y+dy)*stride+x+dx];
                if(t>v)goto next_candidate;
                if(t>=cut)neighbors++;
            }
            /* no 1-pixel hot spots / isolated cosmic-ray pixels */
            if(neighbors<3)continue;
            dist=ABSI(x-ox)+ABSI(y-oy);
            rank=v-bg;
            if(rank>peak || (rank==peak&&dist<peakdist)){
                peak=rank;peakdist=dist;px=x;py=y;
            }
next_candidate: ;
        }
    }
    if(px<0)return -1;
    /* Determine aperture radius from the radial light profile, instead of
     * a fixed 6-pixel aperture. This works at different focal lengths and
     * star sizes and preserves metric sensitivity during moderate defocus. */
    int maxrad=14,outer=4;
    if(px<17 || px>=w-17 || py<17 || py>=h-17)return -1;
    int64_t peakv=peak;
    for(int rad=4;rad<=maxrad;rad++) {
        int64_t annulus=0;
        int np=0;
        for(int dy=-rad;dy<=rad;dy++)for(int dx=-rad;dx<=rad;dx++){
            int rr=dx*dx+dy*dy;
            if(rr<=rad*rad && rr>(rad-1)*(rad-1)){
                int v=(int)im[(py+dy)*stride+px+dx]-bg-4;
                if(v>0)annulus+=v;
                np++;
            }
        }
        outer=rad;
        /* Ring mean below 7% of star peak means the star fades into
         * background. Extend one extra ring to catch wings. */
        if(rad>=5 && annulus*100 < (int64_t)np*peakv*7) {
            if(outer<maxrad)outer++;
            break;
        }
    }
    int64_t flux=0,r2=0,wx=0,wy=0;
    for(int dy=-outer;dy<=outer;dy++)for(int dx=-outer;dx<=outer;dx++){
        if(dx*dx+dy*dy>outer*outer)continue;
        int v=(int)im[(py+dy)*stride+px+dx]-bg-4;
        if(v<=0)continue;
        flux+=v; wx+=(int64_t)dx*v; wy+=(int64_t)dy*v;
        r2+=(int64_t)v*(dx*dx+dy*dy);
    }
    if(flux<65 || peak<18) return -1;
    int cx=px+(int)((wx+(wx>=0?flux/2:-flux/2))/flux);
    int cy=py+(int)((wy+(wy>=0?flux/2:-flux/2))/flux);
    if(ABSI(cx-ox)>radius+2 || ABSI(cy-oy)>radius+2 ||
       ABSI(cx-px)>4 || ABSI(cy-py)>4)
        return -1;
    *star_x=cx; *star_y=cy;
    return (int)(r2*16/flux);
}
int astro_focus_metric(const uint8_t *im,int w,int h,int stride,
                       int *star_x,int *star_y)
{
    astro_star stars[ASTRO_MAX_STARS];
    int n,i;
    if(!im||!star_x||!star_y||w<32||h<32||stride<w)return -1;
    n=astro_find_stars(im,w,h,stride,stars,ASTRO_MAX_STARS);
    /* Initially pick the strongest *stellar* source in the central area.
     * The subsequent motor sequence always uses the locked variant above. */
    for(i=0;i<n;i++) {
        int x=stars[i].x,y=stars[i].y;
        if(x<w/5 || x>=w-w/5 || y<h/5 || y>=h-h/5)continue;
        int metric=astro_focus_metric_locked(im,w,h,stride,&x,&y,3);
        if(metric>=0) {
            *star_x=x;*star_y=y;
            return metric;
        }
    }
    return -1;
}

int astro_fov_cdeg(int sensor_width_x10,int focal_mm)
{
    /* 57.3 radians->degrees; small-angle approximation. */
    if(sensor_width_x10<100 || sensor_width_x10>600 ||
       focal_mm<10 || focal_mm>3000)return 0;
    return (sensor_width_x10*573 + focal_mm/2)/focal_mm;
}

int astro_solve_hinted(const astro_star *obs,int nobs,
                       const astro_catalog *cat,int ncat,
                       int w,int h,int fov_cdeg,astro_solution *res)
{
    int best=0, best_rms=0;
    int64_t best_cx=0,best_cy=0;
    if (!obs||!cat||!res||nobs<4||ncat<4||ncat>ASTRO_MAX_CATALOG ||
        fov_cdeg<50||w<50||h<50) return 0;
    /* Candidate pairs: brightest 9 observed, brightest 40 catalog entries. */
    int no=MIN2(nobs,9),nc=MIN2(ncat,40);
    for (int i=0;i<no;i++) for (int j=i+1;j<no;j++) {
        int dxo=obs[j].x-obs[i].x, dyo=obs[j].y-obs[i].y;
        int64_t do2=sq(dxo)+sq(dyo);
        if (do2<400) continue;
        for (int a=0;a<nc;a++) for(int b=0;b<nc;b++) {
            if(a==b) continue;
            int dxc=cat[b].x-cat[a].x,dyc=cat[b].y-cat[a].y;
            int64_t dc2=sq(dxc)+sq(dyc);
            if(dc2<100)continue;
            /* 15% scale tolerance to reject most pair hypotheses. */
            int64_t want=dc2*(int64_t)w*w, have=do2*(int64_t)fov_cdeg*fov_cdeg;
            if (have*100<want*72 || have*100>want*132) continue;
            /* Rotation + scale Q12: image = anchor + R*(sky-anchor). */
            int64_t A=(((int64_t)dxc*dxo+(int64_t)dyc*dyo)<<12)/dc2;
            int64_t B=(((int64_t)dxc*dyo-(int64_t)dyc*dxo)<<12)/dc2;
            int matches=0, err=0;
            uint64_t used=0;
            for(int o=0;o<nobs;o++) {
                int found=-1, mind=17;
                for(int k=0;k<ncat;k++) {
                    if(used & (1ULL<<k))continue;
                    int cx=cat[k].x-cat[a].x,cy=cat[k].y-cat[a].y;
                    int px=obs[i].x+(int)((A*cx-B*cy)>>12);
                    int py=obs[i].y+(int)((B*cx+A*cy)>>12);
                    int dx=px-obs[o].x,dy=py-obs[o].y;
                    int d=dx*dx+dy*dy;
                    if(d<mind){mind=d;found=k;}
                }
                if(found>=0) {used |= 1ULL<<found; matches++; err+=mind;}
            }
            if (matches>=4 &&
                 (matches>best || (matches==best && (!best_rms||err<best_rms)))) {
                int64_t denom=A*A+B*B;
                if(denom<=0) continue;
                /* Inverse transform from image midpoint to sky position.
                   Q12 coefficients => centidegrees output. */
                int64_t ux=(int64_t)(w/2-obs[i].x)<<12;
                int64_t uy=(int64_t)(h/2-obs[i].y)<<12;
                best_cx=(int64_t)cat[a].x+(A*ux+B*uy)/denom;
                best_cy=(int64_t)cat[a].y+(-B*ux+A*uy)/denom;
                best=matches;best_rms=err;
            }
        }
    }
    /* An arbitrary field can produce 5-6 accidental pair matches. Require
       enough independent confirmations, especially in crowded fields. */
    if (best < 8 || best*4 < nobs || best_rms > 4*best) return 0;
    res->matched=best;
    res->center_x_cdeg=(int32_t)best_cx;
    res->center_y_cdeg=(int32_t)best_cy;
    res->rms_x10=(best_rms*100/best); /* actually mean square pixels*100 */
    return best;
}
