#include "astro_sky.h"
#include <string.h>
#define ABS(x) ((x)<0?-(x):(x))
#define QN 24
#define PATTERN_CHUNK 256
#define MAX_QUADS 5000
/* Shared small verification fields replace sky-vote clustering. A false
 * quad never displaces a true match, and a verified solution exits early. */
typedef struct {uint32_t key,fine,field;uint8_t a,b,flip,pad;} sky_pattern;
typedef struct {int16_t x,y;uint16_t name,mag;} field_star;
/* pad: millidegrees per coordinate unit for wide fields (0 means 1). */
typedef struct {uint32_t ra;int32_t dec;uint16_t count,radius;uint32_t pad;
                field_star stars[SKY_VERIFY_STARS];} sky_field;
typedef char assert_pattern[(sizeof(sky_pattern)==16)?1:-1];
typedef char assert_field[(sizeof(sky_field)==SKY_FIELD_BYTES)?1:-1];
static sky_pattern patterns_buf[PATTERN_CHUNK];
/* Rank only the few descriptor matches for ONE image quad before touching
 * verification fields on SD. Unlike global voting this never discards a
 * candidate and never changes the geometric acceptance test. */
#define VERIFY_BATCH 1024
static sky_pattern verify_batch[VERIFY_BATCH];
static uint8_t verify_error[VERIFY_BATCH];
static unsigned verify_count=0;
static sky_field field_buf;
static uint32_t cached_field=0xffffffffu;
static int16_t pixel_x[SKY_VERIFY_STARS],pixel_y[SKY_VERIFY_STARS];
static int8_t association[ASTRO_MAX_STARS];
static int32_t matrix[4],translate[2];
static uint32_t scale_min=SKY_SCALE_MIN,scale_max=SKY_SCALE_MAX;
static uint32_t canonical(int *x,int *y,uint32_t *fin,int *role_a,int *role_b,int *parity) {
    int longest=-1,a=0,b=1;
    for(int i=0;i<4;i++)for(int j=i+1;j<4;j++) {
        int dx=x[j]-x[i],dy=y[j]-y[i],d=dx*dx+dy*dy;
        if(d>longest){longest=d;a=i;b=j;}
    }
    if(longest<400)return 0xffffffffu;
    int cd[2],q=0;
    for(int i=0;i<4;i++)if(i!=a&&i!=b)cd[q++]=i;
    uint32_t best=0xffffffffu, bestfine=0;
    int selected_a=a,selected_b=b,selected_flip=0;
    for(int sw=0;sw<2;sw++) {
        int aa=sw?b:a,bb=sw?a:b;
        int bx=x[bb]-x[aa],by=y[bb]-y[aa],d=bx*bx+by*by;
        int ux[2],vy[2],fx[2],fy[2];
        for(int i=0;i<2;i++) {
            int dx=x[cd[i]]-x[aa],dy=y[cd[i]]-y[aa];
            int64_t dot=(int64_t)dx*bx+(int64_t)dy*by;
            int64_t cross=(int64_t)dx*by-(int64_t)dy*bx;
            ux[i]=(int)((dot*QN+(dot>=0?d/2:-d/2))/d);
            vy[i]=(int)((cross*QN+(cross>=0?d/2:-d/2))/d);
            fx[i]=(int)((dot*255+(dot>=0?d/2:-d/2))/d);
            fy[i]=(int)(((cross+d)*255+d)/(2*d));
            if(fx[i]<0)fx[i]=0;
            if(fx[i]>255)fx[i]=255;
            if(fy[i]<0)fy[i]=0;
            if(fy[i]>255)fy[i]=255;
        }
        for(int sign=0;sign<2;sign++) {
            int x0=ux[0],x1=ux[1],y0=sign?-vy[0]:vy[0],
                y1=sign?-vy[1]:vy[1];
            int xx0=fx[0],xx1=fx[1],yy0=sign?255-fy[0]:fy[0],
                yy1=sign?255-fy[1]:fy[1];
            if(x0<0)x0=0;
            if(x0>QN)x0=QN;
            if(x1<0)x1=0;
            if(x1>QN)x1=QN;
            y0=(y0+QN+1)/2;y1=(y1+QN+1)/2;
            if(y0<0)y0=0;
            if(y0>QN)y0=QN;
            if(y1<0)y1=0;
            if(y1>QN)y1=QN;
            if(x0>x1 || (x0==x1&&y0>y1)){
                int t=x0;x0=x1;x1=t;t=y0;y0=y1;y1=t;
                t=xx0;xx0=xx1;xx1=t;t=yy0;yy0=yy1;yy1=t;
            }
            uint32_t code=((uint32_t)x0<<15)|((uint32_t)y0<<10)|
                          ((uint32_t)x1<<5)|(uint32_t)y1;
            if(code<best){
                best=code;
                selected_a=aa;selected_b=bb;selected_flip=sign;
                bestfine=((uint32_t)xx0<<24)|((uint32_t)yy0<<16)|
                         ((uint32_t)xx1<<8)|(uint32_t)yy1;
            }
        }
    }
    if(fin)*fin=bestfine;
    if(role_a)*role_a=selected_a;
    if(role_b)*role_b=selected_b;
    if(parity)*parity=selected_flip;
    return best;
}
static int popcount32(uint32_t x){int n=0;while(x){x&=x-1;n++;}return n;}
/* 1000*log10(k!) and 1000*log10(1+i/16): integer log-odds arithmetic. */
static const uint16_t log10_fact[ASTRO_MAX_STARS+1]={0,0,301,778,1380,2079,2857,
    3702,4606,5560,6560,7601,8680,9794,10940,12116,13321,14551,15806,17085,
    18386,19708,21051,22412,23793,25191,26606,28037,29484,30947,32424,33915,35420};
static const uint16_t log10_frac[16]={0,26,51,75,97,118,138,158,176,194,211,227,243,258,273,287};
static int log10_milli(uint32_t x){
    int e=0;
    if(!x)return -100000;
    while(x>=32){x>>=1;e++;}
    while(x<16){x<<=1;e--;}
    return 1204+e*301+log10_frac[x-16];
}
/* Four quad stars match by construction, so only the remaining stars are
 * independent evidence. Upper bound for the chance of k such coincidences:
 * C(n,k)*p^k, with p = (catalogue stars in frame)*(21 integer offsets within
 * the final 6 px^2 radius)/(frame area). Multiply by the number of
 * hypotheses checked so far and demand < 1e-6 (log-odds, Bonferroni). */
#define SKY_FALSE_ALARM_LOG10 (-6000)
static int significant(int n,int k,int w,int h,unsigned hypotheses){
    int inside=0;
    for(unsigned i=0;i<field_buf.count;i++)if(pixel_x[i]!=-30000)inside++;
    if(k<4 || n<k || n>ASTRO_MAX_STARS || !inside)return 0;
    int log_p=log10_milli((uint32_t)inside*21)-log10_milli((uint32_t)(w+20)*(h+20));
    int log_false=log10_fact[n]-log10_fact[k]-log10_fact[n-k]+k*log_p+
                  log10_milli(hypotheses?hypotheses:1);
    return log_false<=SKY_FALSE_ALARM_LOG10;
}
static int32_t star_x16[SKY_VERIFY_STARS],star_y16[SKY_VERIFY_STARS];
static int8_t star_ok[SKY_VERIFY_STARS];
/* Similarity fit (with the solution's mirror parity) from tangent-plane
 * millidegrees about the image centre to pixels. Same normal equations as
 * refine(), Q24 output for wide-lens precision. */
static int fit_wcs(const astro_star *obs,int n,int parity,astro_wcs *wcs){
    int64_t sx=0,sy=0,su=0,sv=0,ss=0,sa=0,sb=0;int used=0;
    for(int i=0;i<n;i++){
        int k=association[i];
        if(k<0 || !star_ok[k])continue;
        int64_t x=star_x16[k]/16,y=parity*(star_y16[k]/16),u=obs[i].x,v=obs[i].y;
        sx+=x;sy+=y;su+=u;sv+=v;ss+=x*x+y*y;sa+=x*u+y*v;sb+=x*v-y*u;used++;
    }
    int64_t den=used*ss-sx*sx-sy*sy;
    if(used<4 || den<100)return 0;
    int64_t a=(used*sa-sx*su-sy*sv)*16777216/den;
    int64_t b=(used*sb-sx*sv+sy*su)*16777216/den;
    if(!a && !b)return 0;
    wcs->m[0]=(int32_t)a;wcs->m[1]=(int32_t)(-b*parity);
    wcs->m[2]=(int32_t)b;wcs->m[3]=(int32_t)(a*parity);
    wcs->t[0]=(int32_t)((su*65536-(a*sx-b*sy)/256)/used);
    wcs->t[1]=(int32_t)((sv*65536-(b*sx+a*sy)/256)/used);
    return 1;
}
/* Runs once, after a solution has already been verified. The solver's
 * transform is anchored at the index field's tangent point, which can be
 * far from the image centre on wide lenses. Re-express every field star
 * about the image centre, refit, then re-associate once with the better
 * model. Acceptance is already decided; this only improves geometry. */
static void export_solution(const astro_star *obs,int n,int w,int h,int parity,sky_solution *out){
    astro_wcs field={field_buf.ra,field_buf.dec,{0,0,0,0},{0,0}},ident,*wcs=&out->wcs;
    int32_t unit=field_buf.pad?(int32_t)field_buf.pad:1;
    for(int i=0;i<4;i++)field.m[i]=matrix[i]*256/unit;
    field.t[0]=translate[0];field.t[1]=translate[1];
    *wcs=field;
    if(!astro_wcs_unproject(&field,w*8,h*8,&out->ra,&out->dec)){out->ra=field_buf.ra;out->dec=field_buf.dec;}
    /* Identity maps: "pixels" are tangent millidegrees. */
    ident.ra0=field_buf.ra;ident.dec0=field_buf.dec;
    ident.m[0]=ident.m[3]=16777216;ident.m[1]=ident.m[2]=0;ident.t[0]=ident.t[1]=0;
    astro_wcs centre=ident;centre.ra0=out->ra;centre.dec0=out->dec;
    for(unsigned k=0;k<field_buf.count;k++){
        uint32_t ra;int32_t dec;
        star_ok[k]=astro_wcs_unproject(&ident,field_buf.stars[k].x*unit*16,field_buf.stars[k].y*unit*16,&ra,&dec) &&
                   astro_wcs_project(&centre,ra,dec,&star_x16[k],&star_y16[k]);
    }
    astro_wcs refit={out->ra,out->dec,{0,0,0,0},{0,0}};
    if(fit_wcs(obs,n,parity,&refit)){
        *wcs=refit;
        /* One re-association pass across the whole frame. */
        int8_t previous[ASTRO_MAX_STARS];
        memcpy(previous,association,sizeof(previous));
        uint32_t used[3]={0,0,0};int matches=0,err=0;uint32_t mask=0;
        for(int o=0;o<n;o++){
            int best=-1,dist=7;
            for(unsigned k=0;k<field_buf.count;k++){
                int32_t x16,y16;
                if(!star_ok[k] || (used[k>>5]&(1u<<(k&31))))continue;
                int64_t px=(int64_t)refit.m[0]*star_x16[k]+(int64_t)refit.m[1]*star_y16[k]+(int64_t)refit.t[0]*4096;
                int64_t py=(int64_t)refit.m[2]*star_x16[k]+(int64_t)refit.m[3]*star_y16[k]+(int64_t)refit.t[1]*4096;
                x16=(int32_t)((px+(1<<23))>>24);y16=(int32_t)((py+(1<<23))>>24);
                int dx=(x16-obs[o].x*16+8)>>4,dy=(y16-obs[o].y*16+8)>>4;
                if(ABS(dx)>3 || ABS(dy)>3)continue;
                int e=dx*dx+dy*dy;
                if(e<dist){best=(int)k;dist=e;}
            }
            association[o]=(int8_t)best;
            if(best>=0){used[best>>5]|=1u<<(best&31);err+=dist;matches++;mask|=1u<<o;}
        }
        astro_wcs again=refit;
        if(matches>=out->matched && fit_wcs(obs,n,parity,&again)){
            *wcs=again;out->matched=matches;out->matched_mask=mask;
            out->mse100=matches?100*err/matches:0;
        } else memcpy(association,previous,sizeof(previous));
        astro_wcs_unproject(wcs,w*8,h*8,&out->ra,&out->dec);
    }
    out->fov_cdeg=astro_wcs_fov_cdeg(wcs,w,h);
}
static int associate(const astro_star *obs,int n,int w,int h,int tolerance,int *error,uint32_t *mask){
    unsigned nc=field_buf.count;
    for(unsigned k=0;k<nc;k++){
        int64_t x=(int64_t)matrix[0]*field_buf.stars[k].x+
                  (int64_t)matrix[1]*field_buf.stars[k].y+translate[0];
        int64_t y=(int64_t)matrix[2]*field_buf.stars[k].x+
                  (int64_t)matrix[3]*field_buf.stars[k].y+translate[1];
        x=(x+32768)>>16;y=(y+32768)>>16;
        pixel_x[k]=(int16_t)((x< -10 || x>w+10 || y< -10 || y>h+10)?-30000:x);
        pixel_y[k]=(int16_t)((x< -10 || x>w+10 || y< -10 || y>h+10)?-30000:y);
    }
    uint32_t used[3]={0,0,0};int matches=0;*error=0;*mask=0;
    for(int o=0;o<n;o++){
        int best=-1,dist=tolerance+1;
        for(unsigned k=0;k<nc;k++){
            if((used[k>>5]&(1u<<(k&31))) || pixel_x[k]==-30000)continue;
            int dx=pixel_x[k]-obs[o].x,dy=pixel_y[k]-obs[o].y;
            if(ABS(dx)>4 || ABS(dy)>4)continue;
            int e=dx*dx+dy*dy;
            if(e<dist){best=(int)k;dist=e;}
        }
        association[o]=(int8_t)best;
        if(best>=0){used[best>>5]|=1u<<(best&31);*error+=dist;matches++;*mask|=1u<<o;}
    }
    return matches;
}
static int refine(const astro_star *obs,int n,int parity){
    int64_t sx=0,sy=0,su=0,sv=0,ss=0,sa=0,sb=0;int used=0;
    for(int i=0;i<n;i++)if(association[i]>=0){
        field_star *c=&field_buf.stars[(unsigned)association[i]];
        int64_t x=c->x,y=c->y*parity,u=obs[i].x,v=obs[i].y;
        sx+=x;sy+=y;su+=u;sv+=v;ss+=x*x+y*y;sa+=x*u+y*v;sb+=x*v-y*u;used++;
    }
    int64_t den=used*ss-sx*sx-sy*sy;
    if(used<4 || den<100)return 0;
    int64_t a=(used*sa-sx*su-sy*sv)*65536/den;
    int64_t b=(used*sb-sx*sv+sy*su)*65536/den;
    if(ABS(a)>65536 || ABS(b)>65536)return 0;
    matrix[0]=(int32_t)a;matrix[1]=(int32_t)(-b*parity);
    matrix[2]=(int32_t)b;matrix[3]=(int32_t)(a*parity);
    translate[0]=(int32_t)((su*65536-a*sx+b*sy)/used);
    translate[1]=(int32_t)((sv*65536-b*sx-a*sy)/used);
    return 1;
}
static int verify_pattern(const sky_pattern *r,const astro_star *obs,int n,int w,int h,
                          int oa,int ob,int flip,uint32_t quad_mask,
                          unsigned nfields,sky_read rd,void *ctx,sky_solution *out){
    if(r->field>=nfields || r->a>=10 || r->b>=10 || r->a==r->b || r->flip>1)return 0;
    if(cached_field!=r->field){
        out->reads++;
        if(!rd(ctx,16+r->field*SKY_FIELD_BYTES,&field_buf,SKY_FIELD_BYTES))return -1;
        cached_field=r->field;
    }
    if(field_buf.count<10 || field_buf.count>SKY_VERIFY_STARS ||
       field_buf.ra>=3600000 || field_buf.dec< -900000 || field_buf.dec>900000 ||
       field_buf.pad>8)return -1;
    field_star *a=&field_buf.stars[r->a],*b=&field_buf.stars[r->b];
    int dxs=b->x-a->x,dys=b->y-a->y,dxo=obs[ob].x-obs[oa].x,dyo=obs[ob].y-obs[oa].y;
    int parity=flip==r->flip?1:-1;
    int64_t den=(int64_t)dxs*dxs+(int64_t)dys*dys;
    if(den<250000)return 0;
    matrix[0]=(int32_t)(((int64_t)dxo*dxs+(int64_t)parity*dyo*dys)*65536/den);
    matrix[1]=(int32_t)(((int64_t)dxo*dys-(int64_t)parity*dyo*dxs)*65536/den);
    matrix[2]=(int32_t)(((int64_t)dyo*dxs-(int64_t)parity*dxo*dys)*65536/den);
    matrix[3]=(int32_t)(((int64_t)dyo*dys+(int64_t)parity*dxo*dxs)*65536/den);
    uint32_t scale=astro_isqrt64((int64_t)matrix[0]*matrix[0]+(int64_t)matrix[2]*matrix[2]);
    uint32_t unit=field_buf.pad?field_buf.pad:1;
    if(scale<scale_min*unit || scale>scale_max*unit)return 0;
    out->checked++; /* hypotheses actually tested: the Bonferroni factor */
    translate[0]=obs[oa].x*65536-matrix[0]*a->x-matrix[1]*a->y;
    translate[1]=obs[oa].y*65536-matrix[2]*a->x-matrix[3]*a->y;
    int err=0;uint32_t mask=0;
    int matches=associate(obs,n,w,h,10,&err,&mask);
    if(matches<8 || popcount32(mask&~quad_mask)<4)return 0;
    if(!refine(obs,n,parity))return 0;
    matches=associate(obs,n,w,h,6,&err,&mask);
    if(matches<8 || popcount32(mask&~quad_mask)<4 || err>2*matches)return 0;
    if(!refine(obs,n,parity))return 0;
    matches=associate(obs,n,w,h,6,&err,&mask);
    if(matches<8 || popcount32(mask&~quad_mask)<4 || err>2*matches)return 0;
    if(!significant(n-popcount32(quad_mask),popcount32(mask&~quad_mask),w,h,out->checked))return 0;
    out->matched=matches;out->mse100=100*err/matches;out->matched_mask=mask;
    out->field=r->field;
    export_solution(obs,n,w,h,parity,out);
    memset(out->names,0,sizeof(out->names));
    for(int i=0;i<n;i++)if(association[i]>=0)out->names[i]=field_buf.stars[(unsigned)association[i]].name;
    return 1;
}
static int verify_candidates(const astro_star *obs,int n,int w,int h,
                             int oa,int ob,int flip,uint32_t mask,
                             unsigned nfields,sky_read read_fields,void *field_ctx,
                             sky_progress progress,void *progress_ctx,sky_solution *out){
    for(int error=0;error<=9;error++)for(unsigned i=0;i<verify_count;i++){
        if(verify_error[i]!=error)continue;
        if(progress && (out->checked%16==0) &&
           !progress(progress_ctx,out->quads,out->checked))return -2;
        int r=verify_pattern(&verify_batch[i],obs,n,w,h,oa,ob,flip,mask,
                             nfields,read_fields,field_ctx,out);
        if(r)return r;
    }
    verify_count=0;
    return 0;
}
static int lookup(const sky_dir_entry *dir,unsigned count,uint32_t key){
    unsigned lo=0,hi=count;
    while(lo<hi){unsigned m=lo+(hi-lo)/2;if(dir[m].key<key)lo=m+1;else hi=m;}
    return lo<count && dir[lo].key==key?(int)lo:-1;
}
int astro_sky_solve(const astro_star *obs,int n,int w,int h,
                    const sky_dir_entry *dir,unsigned nkeys,unsigned npatterns,
                    unsigned nfields,sky_read read_patterns,void *pattern_ctx,
                    sky_read read_fields,void *field_ctx,sky_progress progress,void *progress_ctx,
                    const sky_options *opt,sky_solution *out){
    if(!obs || !dir || !out || !read_patterns || !read_fields || n<8 || n>32 ||
       w<100 || w>4096 || h<80 || h>2160 || nkeys>65536 || !nkeys || !nfields)return -1;
    memset(out,0,sizeof(*out));cached_field=0xffffffffu;
    scale_min=SKY_SCALE_MIN;scale_max=SKY_SCALE_MAX;
    if(opt && opt->scale_min){
        if(opt->scale_min>opt->scale_max)return -1;
        scale_min=opt->scale_min<SKY_SCALE_MIN?SKY_SCALE_MIN:opt->scale_min;
        scale_max=opt->scale_max>SKY_SCALE_MAX?SKY_SCALE_MAX:opt->scale_max;
    }
    if(dir[nkeys].offset!=npatterns)return -1;
    for(unsigned i=0;i<nkeys;i++)if(dir[i].offset>dir[i+1].offset ||
        dir[i].key>=dir[i+1].key || dir[i+1].offset>npatterns)return -1;
    /* Increasing brightest-star rank avoids spending the entire budget
     * on quads containing one hot pixel or the first false detection. */
    for(int last=3;last<n;last++)
    for(int a=0;a<last-2;a++)for(int b=a+1;b<last-1;b++)for(int c=b+1;c<last;c++){
        int ids[4]={a,b,c,last};
        int x[4]={obs[a].x,obs[b].x,obs[c].x,obs[last].x};
        int y[4]={obs[a].y,obs[b].y,obs[c].y,obs[last].y};
        int longest=0,shortest=0x7fffffff;
        for(int i=0;i<4;i++)for(int j=i+1;j<4;j++){
            int dx=x[i]-x[j],dy=y[i]-y[j],d=dx*dx+dy*dy;
            if(d>longest)longest=d;
            if(d<shortest)shortest=d;
        }
        if(longest<(w/4)*(w/4) || shortest<64)continue;
        if(out->quads>=MAX_QUADS)return 0;
        out->quads++;
        if(progress && (out->quads%8==1) && !progress(progress_ctx,out->quads,out->checked))return -2;
        int ca=0,cb=0,flip=0;uint32_t fine=0;
        uint32_t code=canonical(x,y,&fine,&ca,&cb,&flip);
        if(code==0xffffffffu)continue;
        uint32_t qmask=(1u<<a)|(1u<<b)|(1u<<c)|(1u<<last);
        int values[4]={(code>>15)&31,(code>>10)&31,(code>>5)&31,code&31};
        uint32_t keys[81];int nk=0;
        for(int i=-1;i<=1;i++)for(int j=-1;j<=1;j++)for(int k=-1;k<=1;k++)for(int l=-1;l<=1;l++){
            int v[4]={values[0]+i,values[1]+j,values[2]+k,values[3]+l};
            int ok=1;
            for(int d=0;d<4;d++){
                if(v[d]<0 || v[d]>24){ok=0;break;}
                int fv=(fine>>(24-8*d))&255,error=v[d]*255-fv*24;
                if(ABS(error)>212){ok=0;break;}
            }
            if(!ok)continue;
            uint32_t key=((uint32_t)v[0]<<15)|((uint32_t)v[1]<<10)|((uint32_t)v[2]<<5)|(uint32_t)v[3];
            keys[nk++]=key;
        }
        verify_count=0;
        for(int ki=0;ki<nk;ki++){
            int entry=lookup(dir,nkeys,keys[ki]);
            if(entry<0)continue;
            uint32_t pos=dir[entry].offset,end=dir[entry+1].offset;
            while(pos<end){
                if(progress && (out->reads%8==0) && !progress(progress_ctx,out->quads,out->checked))return -2;
                uint32_t take=end-pos;if(take>PATTERN_CHUNK)take=PATTERN_CHUNK;
                out->reads++;
                if(!read_patterns(pattern_ctx,16+pos*16,patterns_buf,take*16))return -1;
                for(unsigned r=0;r<take;r++){
                    sky_pattern *pat=&patterns_buf[r];
                    if(pat->key!=keys[ki])return -1;
                    int error=0,valid=1;
                    for(int shift=0;shift<32;shift+=8){
                        int delta=(int)((fine>>shift)&255)-(int)((pat->fine>>shift)&255);
                        delta=ABS(delta);if(delta>3){valid=0;break;}error+=delta;
                    }
                    if(!valid || error>9)continue;
                    verify_batch[verify_count]=*pat;
                    verify_error[verify_count++]=(uint8_t)error;
                    if(verify_count==VERIFY_BATCH){
                        int verified=verify_candidates(obs,n,w,h,ids[ca],ids[cb],flip,qmask,
                            nfields,read_fields,field_ctx,progress,progress_ctx,out);
                        if(verified)return verified;
                    }
                }
                pos+=take;
            }
        }
        int verified=verify_candidates(obs,n,w,h,ids[ca],ids[cb],flip,qmask,
            nfields,read_fields,field_ctx,progress,progress_ctx,out);
        if(verified)return verified;
    }
    return 0;
}
