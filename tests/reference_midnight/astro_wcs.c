/* Sky <-> image projection. GPL-2.0-or-later.
 * CORDIC trigonometry: accurate sky coordinates without a software
 * floating-point dependency on the ARM926 camera CPU. */
#include "astro_wcs.h"

#define Q30 1073741824LL
#define ABS64(x) ((x)<0?-(x):(x))
/* atan(2^-i) with a full circle = 2^32. */
static const int32_t angles[]={536870912,316933406,167458907,85004756,
    42667331,21354465,10679838,5340245,2670163,1335087,667544,333772,
    166886,83443,41722,20861,10430,5215,2608,1304,652,326,163,81};

uint32_t astro_isqrt64(uint64_t x){
    uint64_t result=0,bit=1ULL<<62;
    while(bit>x)bit>>=2;
    while(bit){if(x>=result+bit){x-=result+bit;result=(result>>1)+bit;}else result>>=1;bit>>=2;}
    return (uint32_t)result;
}
/* CORDIC rotation converges for |angle| <= ~99 degrees. */
static void sincos_core(int32_t angle,int32_t *s,int32_t *c){
    int64_t x=652032874,y=0,z=((int64_t)angle*4294967296LL)/ASTRO_FULL_CIRCLE;
    for(int i=0;i<24;i++){
        int64_t ox=x;
        if(z>=0){x-=y>>i;y+=ox>>i;z-=angles[i];}
        else {x+=y>>i;y-=ox>>i;z+=angles[i];}
    }
    *s=(int32_t)y;*c=(int32_t)x;
}
void astro_sincos(int32_t angle,int32_t *s,int32_t *c){
    angle%=ASTRO_FULL_CIRCLE;
    if(angle>=ASTRO_FULL_CIRCLE/2)angle-=ASTRO_FULL_CIRCLE;
    if(angle< -ASTRO_FULL_CIRCLE/2)angle+=ASTRO_FULL_CIRCLE;
    if(angle>ASTRO_FULL_CIRCLE/4){sincos_core(ASTRO_FULL_CIRCLE/2-angle,s,c);*c=-*c;}
    else if(angle< -ASTRO_FULL_CIRCLE/4){sincos_core(-ASTRO_FULL_CIRCLE/2-angle,s,c);*c=-*c;}
    else sincos_core(angle,s,c);
}
int32_t astro_atan2(int64_t y,int64_t x){
    int64_t z=0;
    /* Keep the CORDIC gain (1.65x) far from int64 overflow. */
    while(ABS64(x)>(1LL<<40) || ABS64(y)>(1LL<<40)){x>>=1;y>>=1;}
    /* ...and far from tiny values, where the y>>i steps lose all precision. */
    if(!x && !y)return 0;
    while(ABS64(x)<(1LL<<30) && ABS64(y)<(1LL<<30)){x<<=1;y<<=1;}
    if(x<0){z=y>=0?2147483648LL:-2147483648LL;x=-x;y=-y;}
    for(int i=0;i<24;i++){
        int64_t ox=x;
        if(y>0){x+=y>>i;y-=ox>>i;z+=angles[i];}
        else {x-=y>>i;y+=ox>>i;z-=angles[i];}
    }
    int32_t r=(int32_t)((z*ASTRO_FULL_CIRCLE)/4294967296LL);
    if(r<= -ASTRO_FULL_CIRCLE/2)r+=ASTRO_FULL_CIRCLE;
    return r;
}
int astro_wcs_project(const astro_wcs *w,uint32_t ra,int32_t dec,int32_t *x16,int32_t *y16){
    int32_t sd,cd,sd0,cd0,sa,ca;
    astro_sincos(dec,&sd,&cd);
    astro_sincos(w->dec0,&sd0,&cd0);
    astro_sincos((int32_t)ra-(int32_t)w->ra0,&sa,&ca);
    int64_t cdca=((int64_t)cd*ca)>>30;
    int64_t cosc=(((int64_t)sd0*sd)>>30)+(((int64_t)cd0*cdca)>>30);
    if(cosc<Q30/5)return 0;
    int64_t xi=((int64_t)cd*sa)>>30;
    int64_t eta=(((int64_t)cd0*sd)>>30)-(((int64_t)sd0*cdca)>>30);
    /* Tangent-plane 1/16 millidegree: 16*180000/pi = 916732.5 */
    int64_t xm=(xi*916732)/cosc,ym=(eta*916732)/cosc;
    int64_t px=(int64_t)w->m[0]*xm+(int64_t)w->m[1]*ym+(int64_t)w->t[0]*4096;
    int64_t py=(int64_t)w->m[2]*xm+(int64_t)w->m[3]*ym+(int64_t)w->t[1]*4096;
    px=(px+(1<<23))>>24;py=(py+(1<<23))>>24;
    if(px< -(1LL<<30) || px>(1LL<<30) || py< -(1LL<<30) || py>(1LL<<30))return 0;
    *x16=(int32_t)px;*y16=(int32_t)py;
    return 1;
}
int astro_wcs_unproject(const astro_wcs *w,int32_t x16,int32_t y16,uint32_t *ra,int32_t *dec){
    int64_t u=(int64_t)x16*4096-w->t[0];
    int64_t v=(int64_t)y16*4096-w->t[1];
    int64_t d=((int64_t)w->m[0]*w->m[3]-(int64_t)w->m[1]*w->m[2])/4096;
    if(!d)return 0;
    /* 1/16 millidegree tangent coordinates -> Q30 tangent (pi/180000/16). */
    int64_t xm=((int64_t)w->m[3]*u-(int64_t)w->m[1]*v)/d;
    int64_t ym=((int64_t)w->m[0]*v-(int64_t)w->m[2]*u)/d;
    int64_t xx=xm*18740330/16000,yy=ym*18740330/16000;
    if(ABS64(xx)>(1LL<<33) || ABS64(yy)>(1LL<<33))return 0;
    int32_t sd,cd;astro_sincos(w->dec0,&sd,&cd);
    int64_t horizontal=cd-((yy*sd)>>30),vertical=sd+((yy*cd)>>30);
    int64_t h2=horizontal>>2,x2=xx>>2;
    int64_t rho=astro_isqrt64((uint64_t)(h2*h2+x2*x2));
    int64_t r=(int64_t)w->ra0+astro_atan2(xx,horizontal);
    while(r<0)r+=ASTRO_FULL_CIRCLE;
    while(r>=ASTRO_FULL_CIRCLE)r-=ASTRO_FULL_CIRCLE;
    *ra=(uint32_t)r;*dec=astro_atan2(vertical>>2,rho);
    return 1;
}
int astro_wcs_axes(const astro_wcs *w,uint32_t ra,int32_t dec,int32_t north16[2],int32_t east16[2]){
    int32_t x0,y0,x1,y1,sign=1,dec1=dec+10000;
    if(dec1>900000){dec1=dec-10000;sign=-1;}
    if(!astro_wcs_project(w,ra,dec,&x0,&y0) || !astro_wcs_project(w,ra,dec1,&x1,&y1))return 0;
    north16[0]=sign*(x1-x0);north16[1]=sign*(y1-y0);
    /* The local map is a similarity: east is north turned by 90 degrees,
     * clockwise without a mirror image, anticlockwise with one. */
    int64_t det=(int64_t)w->m[0]*w->m[3]-(int64_t)w->m[1]*w->m[2];
    if(det>0){east16[0]=north16[1];east16[1]=-north16[0];}
    else {east16[0]=-north16[1];east16[1]=north16[0];}
    return 1;
}
/* Local east/north components (Q30) of the great circle from 1 to 2,
 * and cos(separation). */
static void local_offset(uint32_t ra1,int32_t dec1,uint32_t ra2,int32_t dec2,
                         int64_t *east,int64_t *north,int64_t *cosc){
    int32_t s1,c1,s2,c2,sa,ca;
    astro_sincos(dec1,&s1,&c1);
    astro_sincos(dec2,&s2,&c2);
    astro_sincos((int32_t)ra2-(int32_t)ra1,&sa,&ca);
    int64_t c2ca=((int64_t)c2*ca)>>30;
    *east=((int64_t)c2*sa)>>30;
    *north=(((int64_t)c1*s2)>>30)-(((int64_t)s1*c2ca)>>30);
    *cosc=(((int64_t)s1*s2)>>30)+(((int64_t)c1*c2ca)>>30);
}
uint32_t astro_separation(uint32_t ra1,int32_t dec1,uint32_t ra2,int32_t dec2){
    int64_t e,n,c;
    local_offset(ra1,dec1,ra2,dec2,&e,&n,&c);
    int64_t s=astro_isqrt64((uint64_t)(e*e+n*n));
    int32_t a=astro_atan2(s,c);
    return (uint32_t)(a<0?-a:a);
}
int astro_wcs_direction(const astro_wcs *w,uint32_t ra_from,int32_t dec_from,
                        uint32_t ra_to,int32_t dec_to,int32_t *dx,int32_t *dy){
    int32_t north[2],east[2];
    int64_t e,n,c;
    if(!astro_wcs_axes(w,ra_from,dec_from,north,east))return 0;
    local_offset(ra_from,dec_from,ra_to,dec_to,&e,&n,&c);
    e>>=14;n>>=14;
    int64_t vx=e*east[0]+n*north[0],vy=e*east[1]+n*north[1];
    while(ABS64(vx)>(1LL<<30) || ABS64(vy)>(1LL<<30)){vx>>=1;vy>>=1;}
    int64_t len=astro_isqrt64((uint64_t)(vx*vx+vy*vy));
    if(!len)return 0;
    *dx=(int32_t)(vx*16384/len);*dy=(int32_t)(vy*16384/len);
    return 1;
}
int astro_wcs_fov_cdeg(const astro_wcs *w,int width,int height){
    uint32_t ra1,ra2;int32_t d1,d2;
    if(!astro_wcs_unproject(w,0,height*8,&ra1,&d1) ||
       !astro_wcs_unproject(w,width*16,height*8,&ra2,&d2))return 0;
    return (int)((astro_separation(ra1,d1,ra2,d2)+50)/100);
}
int astro_lens_fov_cdeg(int sensor_width_x10,int focal_mm){
    if(sensor_width_x10<100 || sensor_width_x10>600 || focal_mm<4 || focal_mm>3000)return 0;
    return (2*astro_atan2(sensor_width_x10,(int64_t)focal_mm*20)+50)/100;
}
int astro_lens_focal_x10(int sensor_width_x10,int fov_cdeg){
    int32_t s,c;
    if(sensor_width_x10<100 || sensor_width_x10>600 || fov_cdeg<10 || fov_cdeg>17000)return 0;
    astro_sincos(fov_cdeg*50,&s,&c);
    if(s<=0)return 0;
    return (int)(((int64_t)sensor_width_x10*c+s)/(2*(int64_t)s));
}
