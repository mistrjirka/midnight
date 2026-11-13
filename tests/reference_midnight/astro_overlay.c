/* Sky overlay scene builder. GPL-2.0-or-later. See astro_overlay.h. */
#include "astro_overlay.h"
#include <string.h>

#define RA_BINS 72
#define DEC_BINS 36
#define TILES (RA_BINS*DEC_BINS)
#define TILE_SIZE 50000              /* 5 degrees in 1e-4 degree */
#define DSO_HEADER (24+(TILES+1)*4)
#define MAX_STAR_LABELS 40
#define CHUNK 64

const uint16_t astro_catalogue_max[ASTRO_CAT_COUNT+1]={0,110,7840,5386,109,370,313};

static uint32_t tile_offsets[TILES+1];
static uint16_t tile_list[TILES];
static uint8_t chunk[CHUNK*24];

static int rd(const astro_file *f,uint32_t at,void *dst,uint32_t n){
    return f->read && f->read(f->ctx,at,dst,n);
}
static uint32_t get32(const uint8_t *p){return p[0]|p[1]<<8|p[2]<<16|(uint32_t)p[3]<<24;}
static uint16_t get16(const uint8_t *p){return (uint16_t)(p[0]|p[1]<<8);}

void astro_designation(int cat,int num,char *out,int size){
    static const char *const prefix[]={"?","M","NGC ","IC ","C ","B ","Sh2-"};
    const char *p=(cat>=1 && cat<=ASTRO_CAT_COUNT)?prefix[cat]:prefix[0];
    char digits[8];int n=0;
    unsigned v=(unsigned)num;
    do{digits[n++]=(char)('0'+v%10);v/=10;}while(v && n<7);
    int i=0;
    while(*p && i<size-1)out[i++]=*p++;
    while(n && i<size-1)out[i++]=digits[--n];
    if(size>0)out[i]=0;
}
const char *astro_dso_type_name(int type){
    static const char *const names[]={"object","galaxy","open cluster","globular",
        "planetary neb.","nebula","reflection neb.","dark nebula","SN remnant",
        "cluster+nebula","asterism","star"};
    return (type>=0 && type<=ASTRO_DSO_STAR)?names[type]:names[0];
}
static void decode_dso(const uint8_t *p,astro_dso *o){
    o->ra=get32(p);o->dec=(int32_t)get32(p+4);
    o->maj=get16(p+8);o->min=get16(p+10);
    o->pa=(int16_t)get16(p+12);o->mag10=(int16_t)get16(p+14);
    o->type=p[16];o->cat=p[17];o->num=get16(p+18);o->name=get16(p+20);o->reserved=0;
}
static int dso_count(const astro_sky_files *f){
    uint8_t h[24];
    if(!rd(&f->dso,0,h,24) || memcmp(h,"SDSO",4) || get32(h+4)!=1 || get32(h+12)!=24 ||
       get32(h+16)!=RA_BINS || get32(h+20)!=DEC_BINS || get32(h+8)>200000)return -1;
    return (int)get32(h+8);
}
int astro_dso_lookup(const astro_sky_files *f,int cat,int num,astro_target *out){
    uint8_t h[16],k[8];
    memset(out,0,sizeof(*out));
    int count=dso_count(f);
    if(count<0 || !rd(&f->dso_key,0,h,16) || memcmp(h,"SKEY",4) || get32(h+4)!=1 ||
       get32(h+12)!=8 || get32(h+8)>200000)return -1;
    uint32_t want=(uint32_t)cat<<16|(uint16_t)num,lo=0,hi=get32(h+8);
    while(lo<hi){                      /* <= 15 reads of 8 bytes */
        uint32_t mid=lo+(hi-lo)/2;
        if(!rd(&f->dso_key,16+mid*8,k,8))return -1;
        uint32_t key=(uint32_t)k[0]<<16|get16(k+2);
        if(key==want){
            uint32_t rec=get32(k+4);
            if(rec>=(uint32_t)count || !rd(&f->dso,DSO_HEADER+rec*24,chunk,24))return -1;
            decode_dso(chunk,&out->obj);
            if(out->obj.ra>=ASTRO_FULL_CIRCLE || out->obj.dec< -900000 || out->obj.dec>900000)return -1;
            astro_designation(cat,num,out->label,sizeof(out->label));
            out->valid=1;
            return 1;
        }
        if(key<want)lo=mid+1;else hi=mid;
    }
    return 0;
}
void astro_dso_common_name(const astro_sky_files *f,const astro_dso *o,char *out,int size){
    uint8_t h[16];char name[24];
    if(size>0)out[0]=0;
    if(!o->name || size<1 || !rd(&f->dso_nam,0,h,16) || memcmp(h,"SNAM",4) ||
       get32(h+12)!=24 || o->name>=get32(h+8) || !rd(&f->dso_nam,16+(uint32_t)o->name*24,name,24))return;
    name[23]=0;
    strncpy(out,name,(size_t)size-1);out[size-1]=0;
}

/* ---- scene construction ---- */
static astro_scene *sc;
static const astro_wcs *cw;
static int in_frame(int32_t x16,int32_t y16,int32_t margin16){
    return x16>=-margin16 && y16>=-margin16 &&
           x16<sc->width*16+margin16 && y16<sc->height*16+margin16;
}
static void add_label(int32_t x16,int32_t y16,int kind,int priority,const char *text){
    astro_label *l;
    if(!text[0])return;
    if(sc->nlabels<ASTRO_SCENE_LABELS)l=&sc->labels[sc->nlabels++];
    else {
        l=&sc->labels[ASTRO_SCENE_LABELS-1];        /* replace the least important */
        if(l->priority<=priority)return;
    }
    l->x=(int16_t)x16;l->y=(int16_t)y16;l->kind=(uint8_t)kind;l->priority=(uint8_t)priority;
    strncpy(l->text,text,sizeof(l->text)-1);l->text[sizeof(l->text)-1]=0;
    /* Keep sorted by priority (stable insertion). */
    for(int i=(int)(l-sc->labels);i>0 && sc->labels[i-1].priority>sc->labels[i].priority;i--){
        astro_label t=sc->labels[i];sc->labels[i]=sc->labels[i-1];sc->labels[i-1]=t;
    }
}
/* Cohen-Sutherland clip to the frame, Q4. */
static int outcode(int64_t x,int64_t y){
    return (x<0)|((x>=sc->width*16)<<1)|((y<0)<<2)|((y>=sc->height*16)<<3);
}
static void add_line(int64_t x1,int64_t y1,int64_t x2,int64_t y2){
    int c1=outcode(x1,y1),c2=outcode(x2,y2);
    int64_t xmax=sc->width*16-1,ymax=sc->height*16-1;
    for(int guard=0;guard<8;guard++){
        if(!(c1|c2))break;
        if(c1&c2)return;
        int c=c1?c1:c2;int64_t x,y;
        if(c&8){x=x1+(x2-x1)*(ymax-y1)/(y2-y1);y=ymax;}
        else if(c&4){x=x1+(x2-x1)*(0-y1)/(y2-y1);y=0;}
        else if(c&2){y=y1+(y2-y1)*(xmax-x1)/(x2-x1);x=xmax;}
        else {y=y1+(y2-y1)*(0-x1)/(x2-x1);x=0;}
        if(c==c1){x1=x;y1=y;c1=outcode(x1,y1);}else{x2=x;y2=y;c2=outcode(x2,y2);}
    }
    if((c1|c2) || sc->nlines>=ASTRO_SCENE_LINES)return;
    astro_line *l=&sc->lines[sc->nlines++];
    l->x1=(int16_t)x1;l->y1=(int16_t)y1;l->x2=(int16_t)x2;l->y2=(int16_t)y2;
}
static int16_t clamp16(int64_t v){return (int16_t)(v>32000?32000:v< -32000?-32000:v);}
/* Semi-axis vectors (Q4) of an object's outline at its own position. */
static int ellipse_axes(const astro_dso *o,astro_ellipse *e){
    int32_t north[2],east[2],s,c;
    if(!astro_wcs_axes(cw,o->ra,o->dec,north,east))return 0;
    astro_sincos((o->pa>=0?o->pa:0)*10000,&s,&c);
    int64_t ux=((int64_t)north[0]*c+(int64_t)east[0]*s)>>30,uy=((int64_t)north[1]*c+(int64_t)east[1]*s)>>30;
    int64_t vx=((int64_t)east[0]*c-(int64_t)north[0]*s)>>30,vy=((int64_t)east[1]*c-(int64_t)north[1]*s)>>30;
    int minor=o->min?o->min:o->maj;
    e->ax=clamp16(ux*o->maj/1200);e->ay=clamp16(uy*o->maj/1200);
    e->bx=clamp16(vx*minor/1200);e->by=clamp16(vy*minor/1200);
    return 1;
}
/* Collect the 5-degree tiles that may overlap a sky circle. */
static int frame_tiles(uint32_t ra,int32_t dec,uint32_t radius){
    int n=0;
    int32_t lo=dec-(int32_t)radius,hi=dec+(int32_t)radius;
    if(lo< -900000)lo=-900000;
    if(hi>900000)hi=900000;
    int32_t worst=(lo<0?-lo:lo)>(hi<0?-hi:hi)?(lo<0?-lo:lo):(hi<0?-hi:hi);
    int64_t half=ASTRO_FULL_CIRCLE;
    if(worst<890000){
        int32_t s,c;astro_sincos(worst,&s,&c);
        half=((int64_t)radius<<30)/c+TILE_SIZE;
    }
    int all=half>=ASTRO_FULL_CIRCLE/2;
    for(int db=(lo+900000)/TILE_SIZE;db<=(hi+900000)/TILE_SIZE && db<DEC_BINS;db++)
        for(int rb=0;rb<RA_BINS;rb++){
            if(!all){
                int32_t d=rb*TILE_SIZE+TILE_SIZE/2-(int32_t)ra;
                if(d<0)d=-d;
                if(d>ASTRO_FULL_CIRCLE/2)d=ASTRO_FULL_CIRCLE-d;
                if(d>half+TILE_SIZE/2)continue;
            }
            tile_list[n++]=(uint16_t)(db*RA_BINS+rb);
        }
    return n;
}
static int load_offsets(const astro_file *f,uint32_t at,uint32_t count){
    if(!rd(f,at,tile_offsets,sizeof(tile_offsets)))return 0;
    if(tile_offsets[0] || tile_offsets[TILES]!=count)return 0;
    for(int i=0;i<TILES;i++)if(tile_offsets[i]>tile_offsets[i+1])return 0;
    return 1;
}

struct star_pick {int16_t x,y;uint16_t mag,label;};
static struct star_pick picks[MAX_STAR_LABELS];
static int add_stars(const astro_sky_files *f,const astro_overlay_opts *o,int ntiles){
    uint8_t h[20];int n=0;
    if(!f->stars.read || !f->stars_idx.read || !f->stars_nam.read)return 0;
    if(!rd(&f->stars_idx,0,h,20) || memcmp(h,"SIDX",4) || get32(h+4)!=2 ||
       get32(h+12)!=RA_BINS || get32(h+16)!=DEC_BINS)return -1;
    uint32_t count=get32(h+8);
    if(count>400000 || !load_offsets(&f->stars_idx,20,count))return -1;
    if(!rd(&f->stars,0,h,16) || memcmp(h,"ASTR",4) || get32(h+8)!=count || get32(h+12)!=12)return -1;
    int limit=o->star_mag10+50;
    for(int t=0;t<ntiles;t++){
        uint32_t pos=tile_offsets[tile_list[t]],end=tile_offsets[tile_list[t]+1];
        while(pos<end){
            uint32_t take=end-pos>CHUNK?CHUNK:end-pos;
            if(!rd(&f->stars,16+pos*12,chunk,take*12))return -1;
            for(uint32_t i=0;i<take;i++){
                const uint8_t *r=chunk+i*12;
                int mag=get16(r+8),label=get16(r+10);
                if(!label || mag>limit || (n==MAX_STAR_LABELS && mag>=picks[n-1].mag))continue;
                int32_t x,y;
                if(!astro_wcs_project(cw,get32(r),(int32_t)get32(r+4),&x,&y) || !in_frame(x,y,0))continue;
                int k=n<MAX_STAR_LABELS?n++:n-1;
                for(;k>0 && picks[k-1].mag>mag;k--)picks[k]=picks[k-1];
                picks[k].x=(int16_t)x;picks[k].y=(int16_t)y;picks[k].mag=(uint16_t)mag;picks[k].label=(uint16_t)label;
            }
            pos+=take;
        }
    }
    if(!rd(&f->stars_nam,0,h,16) || memcmp(h,"SNAM",4) || get32(h+12)!=24)return -1;
    uint32_t nlabels=get32(h+8);
    for(int i=0;i<n;i++){
        char text[24];
        if(picks[i].label>=nlabels || !rd(&f->stars_nam,16+(uint32_t)picks[i].label*24,text,24))return -1;
        text[23]=0;
        if(!memcmp(text,"HIP ",4))continue;     /* catalogue numbers are clutter */
        add_label(picks[i].x,picks[i].y,ASTRO_LABEL_STAR,picks[i].mag<70?20:50,text);
    }
    return 0;
}

struct dso_pick {int rank;astro_dso o;astro_ellipse e;};
static struct dso_pick dpicks[ASTRO_SCENE_ELLIPSES];
static int dso_rank(const astro_dso *o,const astro_overlay_opts *opt){
    if(o->cat==ASTRO_CAT_M)return 100;
    if(o->cat==ASTRO_CAT_C)return 200;
    if(o->mag10!=ASTRO_MAG_UNKNOWN && o->mag10<=opt->dso_mag10)return 1000+o->mag10;
    if(o->cat==ASTRO_CAT_SH2 || o->cat==ASTRO_CAT_B)return 3000-(o->maj>2000?2000:o->maj);
    return -1;
}
static int add_dso(const astro_sky_files *f,const astro_overlay_opts *opt,const astro_target *target,int ntiles){
    int n=0;
    if(!f->dso.read)return 0;
    int count=dso_count(f);
    if(count<0 || !load_offsets(&f->dso,24,(uint32_t)count))return -1;
    for(int t=0;t<ntiles;t++){
        uint32_t pos=tile_offsets[tile_list[t]],end=tile_offsets[tile_list[t]+1];
        while(pos<end){
            uint32_t take=end-pos>32?32:end-pos;
            if(!rd(&f->dso,DSO_HEADER+pos*24,chunk,take*24))return -1;
            for(uint32_t i=0;i<take;i++){
                astro_dso o;decode_dso(chunk+i*24,&o);
                int rank=dso_rank(&o,opt);
                if(rank<0 || (n==ASTRO_SCENE_ELLIPSES && rank>=dpicks[n-1].rank))continue;
                if(target && target->valid && o.ra==target->obj.ra && o.dec==target->obj.dec)continue;
                int32_t x,y;astro_ellipse e;
                if(!astro_wcs_project(cw,o.ra,o.dec,&x,&y) || !ellipse_axes(&o,&e))continue;
                int32_t reach=(e.ax<0?-e.ax:e.ax)+(e.ay<0?-e.ay:e.ay);
                if(!in_frame(x,y,reach))continue;
                /* Faint-catalogue outlines only when they are visibly large. */
                if(rank>=2000 && reach<4*16)continue;
                e.x=(int16_t)x;e.y=(int16_t)y;e.kind=ASTRO_LABEL_DSO;e.pad=0;
                int k=n<ASTRO_SCENE_ELLIPSES?n++:n-1;
                for(;k>0 && dpicks[k-1].rank>rank;k--)dpicks[k]=dpicks[k-1];
                dpicks[k].rank=rank;dpicks[k].o=o;dpicks[k].e=e;
            }
            pos+=take;
        }
    }
    for(int i=0;i<n && sc->nellipses<ASTRO_SCENE_ELLIPSES;i++){
        char text[24];
        sc->ellipses[sc->nellipses++]=dpicks[i].e;
        astro_designation(dpicks[i].o.cat,dpicks[i].o.num,text,sizeof(text));
        if(in_frame(dpicks[i].e.x,dpicks[i].e.y,0))
            add_label(dpicks[i].e.x,dpicks[i].e.y,ASTRO_LABEL_DSO,dpicks[i].rank<1000?10:40,text);
    }
    return 0;
}
static int add_constellations(const astro_sky_files *f){
    uint8_t h[16];
    if(!f->constellations.read)return 0;
    if(!rd(&f->constellations,0,h,16) || memcmp(h,"SCON",4) || get32(h+4)!=1 ||
       get32(h+8)>4000 || get32(h+12)>200)return -1;
    uint32_t nseg=get32(h+8),nlab=get32(h+12);
    for(uint32_t pos=0;pos<nseg;pos+=48){
        uint32_t take=nseg-pos>48?48:nseg-pos;
        if(!rd(&f->constellations,16+pos*16,chunk,take*16))return -1;
        for(uint32_t i=0;i<take;i++){
            const uint8_t *s=chunk+i*16;int32_t x1,y1,x2,y2;
            if(astro_wcs_project(cw,get32(s),(int32_t)get32(s+4),&x1,&y1) &&
               astro_wcs_project(cw,get32(s+8),(int32_t)get32(s+12),&x2,&y2))
                add_line(x1,y1,x2,y2);
        }
    }
    for(uint32_t pos=0;pos<nlab;pos+=16){
        uint32_t take=nlab-pos>16?16:nlab-pos;
        if(!rd(&f->constellations,16+nseg*16+pos*32,chunk,take*32))return -1;
        for(uint32_t i=0;i<take;i++){
            const uint8_t *l=chunk+i*32;int32_t x,y;char name[21];
            memcpy(name,l+12,20);name[20]=0;
            if(astro_wcs_project(cw,get32(l),(int32_t)get32(l+4),&x,&y) && in_frame(x,y,0))
                add_label(x,y,ASTRO_LABEL_CONSTELLATION,30,name);
        }
    }
    return 0;
}
static void add_target(const astro_target *t){
    int32_t x,y;astro_ellipse e;
    memcpy(sc->target_label,t->label,sizeof(sc->target_label));
    sc->target_label[sizeof(sc->target_label)-1]=0;
    if(astro_wcs_project(cw,t->obj.ra,t->obj.dec,&x,&y) && in_frame(x,y,0)){
        sc->target=1;sc->target_x=(int16_t)x;sc->target_y=(int16_t)y;
        if(ellipse_axes(&t->obj,&e) && sc->nellipses<ASTRO_SCENE_ELLIPSES){
            e.x=(int16_t)x;e.y=(int16_t)y;e.kind=ASTRO_LABEL_TARGET;e.pad=0;
            sc->ellipses[sc->nellipses++]=e;
        }
        add_label(x,y,ASTRO_LABEL_TARGET,0,t->label);
    } else sc->target=2;
}
int astro_build_scene(const astro_wcs *wcs,int width,int height,const astro_overlay_opts *opts,
                      const astro_target *target,const astro_sky_files *files,astro_scene *scene){
    uint32_t ra,ra2;int32_t dec,dec2;
    memset(scene,0,sizeof(*scene));
    sc=scene;cw=wcs;scene->width=width;scene->height=height;
    if(!astro_wcs_unproject(wcs,width*8,height*8,&ra,&dec))return -1;
    /* Radius of the frame: the farthest corner, plus a margin. */
    uint32_t radius=0;
    for(int corner=0;corner<4;corner++){
        if(!astro_wcs_unproject(wcs,(corner&1)*width*16,(corner>>1)*height*16,&ra2,&dec2))return -1;
        uint32_t s=astro_separation(ra,dec,ra2,dec2);
        if(s>radius)radius=s;
    }
    radius+=10000;
    int status=0;
    if(target && target->valid){
        add_target(target);
        int32_t dx,dy;
        scene->target_sep=astro_separation(ra,dec,target->obj.ra,target->obj.dec);
        if(astro_wcs_direction(wcs,ra,dec,target->obj.ra,target->obj.dec,&dx,&dy)){
            scene->target_dx=(int16_t)dx;scene->target_dy=(int16_t)dy;
        }
    }
    int ntiles=frame_tiles(ra,dec,radius);
    if(opts->dso && add_dso(files,opts,target,ntiles)<0)status=-1;
    if(opts->constellations && add_constellations(files)<0)status=-1;
    if(opts->stars && add_stars(files,opts,ntiles)<0)status=-1;
    return status;
}
