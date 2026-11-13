#include "astro_streak.h"
#include <string.h>

/* Bounded Hough voting + integer line regression. Candidate peaks are
 * eliminated ONLY if the detected line is both long and continuous.
 * Do not call from the 1-Hz focus callback; solving runs in a worker.
 * Hough transform is related to the method used by Siril/astrometry.net.
 * No floating point, external libraries or dynamic allocation required. */
#define H_THETA 48
#define H_RBINS 256
static uint16_t hough_acc[H_THETA*H_RBINS];
static const int16_t h_cos[H_THETA] = {
1024,1022,1015,1004,989,970,946,918,887,852,812,769,
724,675,623,568,512,453,392,329,265,200,134,67,
0,-67,-134,-200,-265,-329,-392,-453,-512,-568,-623,-675,
-724,-769,-812,-852,-887,-918,-946,-970,-989,-1004,-1015,-1022};
static const int16_t h_sin[H_THETA] = {
0,67,134,200,265,329,392,453,512,568,623,675,
724,769,812,852,887,918,946,970,989,1004,1015,1022,
1024,1022,1015,1004,989,970,946,918,887,852,812,769,
724,675,623,568,512,453,392,329,265,200,134,67};

static int absi(int x) {return x<0?-x:x;}
static int64_t absi64(int64_t x) {return x<0?-x:x;}
static int sky_background(const uint8_t *im,int w,int h,int stride) {
    uint16_t hist[256]={0};
    int count=0,sum=0;
    for(int y=2;y<h-2;y+=5)for(int x=2;x<w-2;x+=5) {
        hist[im[y*stride+x]]++;count++;
    }
    for(int v=0;v<256;v++) {
        sum+=hist[v];
        if(sum>=count/2)return v;
    }
    return 0;
}
struct streak {
    int valid,vertical,slope_q,intercept_q,minaxis,maxaxis;
};
static struct streak detect_streak(const uint8_t *im,int w,int h,int stride,int contrast) {
    struct streak no={0};
    if(!im || w<100 || h<80 || w>720 || h>480 || stride<w)return no;
    int bg=sky_background(im,w,h,stride);
    int thr=bg+contrast;
    if(thr>225)return no;
    memset(hough_acc,0,sizeof(hough_acc));
    for(int y=1;y<h-1;y+=2)for(int x=1;x<w-1;x+=2) {
        if(im[y*stride+x]<thr)continue;
        int cx=x-w/2,cy=y-h/2;
        for(int t=0;t<H_THETA;t++) {
            int rho=(cx*h_cos[t]+cy*h_sin[t])/1024;
            int bin=(rho+512)/4;
            if((unsigned)bin<H_RBINS && hough_acc[t*H_RBINS+bin]<65535)
                hough_acc[t*H_RBINS+bin]++;
        }
    }
    for(int attempt=0;attempt<6;attempt++) {
        int best=0,bt=0,br=0;
        for(int t=0;t<H_THETA;t++)for(int k=0;k<H_RBINS;k++) {
            int c=hough_acc[t*H_RBINS+k];
            if(c>best){best=c;bt=t;br=k;}
        }
        if(best<24)return no;
        hough_acc[bt*H_RBINS+br]=0;
        int64_t n=0,sx=0,sy=0,sxx=0,syy=0,sxy=0;
        int rho=br*4-512;
        for(int y=1;y<h-1;y+=2)for(int x=1;x<w-1;x+=2) {
            if(im[y*stride+x]<thr)continue;
            int rr=((x-w/2)*h_cos[bt]+(y-h/2)*h_sin[bt])/1024;
            if(absi(rr-rho)>10)continue;
            n++;sx+=x;sy+=y;sxx+=(int64_t)x*x;
            syy+=(int64_t)y*y;sxy+=(int64_t)x*y;
        }
        if(n<30)continue;
        int64_t denx=n*sxx-sx*sx,deny=n*syy-sy*sy;
        int vertical=deny>denx;
        int64_t den=vertical?deny:denx;
        if(den<10*n)continue;
        int64_t slope=1024*(n*sxy-sx*sy)/den;
        if(absi64(slope)>10240)continue;
        int64_t intercept=vertical ?
            (sx*1024-slope*sy)/n : (sy*1024-slope*sx)/n;
        int minaxis=vertical?h:w,maxaxis=0,segments=0;
        int length=vertical?h:w;
        for(int u=2;u<length-2;u+=4) {
            int v=(int)((slope*u+intercept)/1024);
            if(v<3 || v>=(vertical?w-3:h-3))continue;
            int supported=0;
            for(int delta=-2;delta<=2 && !supported;delta++) {
                int x=vertical?v+delta:u, y=vertical?u:v+delta;
                if(im[y*stride+x]>=thr)supported=1;
            }
            if(supported) {
                if(u<minaxis)minaxis=u;
                if(u>maxaxis)maxaxis=u;
            }
        }
        if(maxaxis-minaxis<100)continue;
        for(int u=minaxis;u<=maxaxis;u+=4) {
            int v=(int)((slope*u+intercept)/1024);
            if(v<3 || v>=(vertical?w-3:h-3))continue;
            int supported=0;
            for(int delta=-2;delta<=2 && !supported;delta++) {
                int x=vertical?v+delta:u, y=vertical?u:v+delta;
                if(im[y*stride+x]>=thr)supported=1;
            }
            segments+=supported;
        }
        if(segments*100<65*((maxaxis-minaxis)/4+1))continue;
        struct streak st={1,vertical,(int)slope,(int)intercept,minaxis,maxaxis};
        return st;
    }
    return no;
}
int astro_mask_streak(uint8_t *im,int w,int h,int stride) {
    struct streak st=detect_streak(im,w,h,stride,80);
    if(!st.valid)st=detect_streak(im,w,h,stride,40);
    if(!st.valid)st=detect_streak(im,w,h,stride,20);
    if(!st.valid)return 0;
    int bg=sky_background(im,w,h,stride);
    for(int y=0;y<h;y++)for(int x=0;x<w;x++) {
        int axis=st.vertical?y:x,pos=st.vertical?x:y;
        if(axis<st.minaxis-5 || axis>st.maxaxis+5)continue;
        int64_t dx=(int64_t)pos*1024-
                   ((int64_t)st.slope_q*axis+st.intercept_q);
        if(absi64(dx)<6*1024) im[y*stride+x]=(uint8_t)bg;
    }
    return 1;
}
