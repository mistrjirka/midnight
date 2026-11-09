/*
 * Experimental Magic Lantern astro module for the 600D / 6D.
 * GPL-2.0-or-later. Never move a lens unless the user explicitly asks.
 *
 * This is a developer prototype, NOT hardware-validated firmware.
 * Uses LiveView YUV luminance; very faint stars may require an exposure
 * pipeline in a future release. Camera actions execute in a separate task.
 */
#include <module.h>
#include <dryos.h>
#include <bmp.h>
#include <menu.h>
#include <config.h>
#include <lens.h>
#include <focus.h>
#include <propvalues.h>
#include <fio-ml.h>
#include <vram.h>
#include <zebra.h>
#include <notify_box.h>
#include <gui-common.h>
#include <string.h>
#include "astro_core.h"
#include "astro_focus_control.h"
#include "astro_shortcut.h"
#include "astro_streak.h"
#include "astro_psf.h"
#include "astro_sky.h"
#include <timer.h>
#include <powersave.h>

#define IM_W 360
#define IM_H 240
#define CATALOG_MAGIC "ASTR"
#define CATALOG_VERSION 1
#define CATALOG_ENTRY_BYTES 12
#define ANNOTATION_MAX ASTRO_MAX_STARS
struct annotation {int16_t x,y;char name[24];};
static struct annotation matched_labels[ANNOTATION_MAX];
static int n_matched_labels=0;
static uint16_t nearby_names[ASTRO_MAX_CATALOG];
static volatile int gallery_prompt=0;
static volatile int gallery_results=0;
static volatile int gallery_result_pending=0;
static volatile int gallery_running=0;
static int solved_matched_stars=0;
static int result_from_gallery=0;
static uint32_t gallery_image_sig=0;
static CONFIG_INT("astro.test_image",test_image,0);
static volatile int file_preview=0,solve_cancel=0;
static uint32_t solve_started=0,solve_elapsed=0;
static sky_solution sky_result;
static const char *test_files[]={"ML/ASTRO/TEST.PGM","ML/ASTRO/TEST1.PGM", "ML/ASTRO/TEST2.PGM","ML/ASTRO/TEST3.PGM"};

static int hint_rah=5; /* selected internally by blind lookup */
static int hint_ram=0;
static int hint_dec=35;
static CONFIG_INT("astro.step", focus_step, 1); /* Canon focus motor speed 1..3 */
static CONFIG_INT("astro.scanradius", scan_radius, 3);
static CONFIG_INT("astro.shortcut", shortcut, 3); /* DISP (600D) / LCD light (6D) */
static CONFIG_INT("astro.overlay", focus_overlay, 0); /* opt-in manual focus overlay */
static volatile int busy = 0;
static volatile int overlay_busy = 0;
static volatile int overlay_score = -1;
static int overlay_x = IM_W/2, overlay_y = IM_H/2;
static int overlay_lost = 0;
static int requested_action = 0;
static char last_status[64] = "Ready";
static char solve_ra_text[24] = "";
static char solve_dec_text[24] = "";
static uint8_t *image = 0;
static astro_star observed[ASTRO_MAX_STARS];
static astro_catalog nearby[ASTRO_MAX_CATALOG];

/* On Canon's UYVY buffer the luma samples occupy byte 1 of each 2-byte
 * pixel. Read only inside the dimensions and pitch reported by ML. */
static int capture_liveview(void)
{
    struct vram_info *v;
    int x,y;
    if (!image || (!lv && !is_pure_play_photo_mode())) return 0;
    v=get_yuv422_vram();
    if(!v || !v->vram || v->width<IM_W || v->height<IM_H ||
        v->pitch<v->width*2 || v->width>4096 || v->height>2160)
        return 0;
    for (y=0;y<IM_H;y++) {
        int sy=y*v->height/IM_H;
        const uint8_t *src=v->vram+sy*v->pitch;
        for(x=0;x<IM_W;x++) {
            int sx=x*v->width/IM_W;
            image[y*IM_W+x]=src[sx*2+1];
        }
    }
    return 1;
}
/* Sample the Canon playback YUV image without storing or modifying the
 * photo. We use this to suppress annotations if the user changes photos
 * while the solver runs. Bitmap annotations are a separate overlay. */
static uint32_t gallery_photo_signature(void)
{
    if(!is_pure_play_photo_mode())return 0;
    struct vram_info *v=get_yuv422_vram();
    if(!v || !v->vram || v->pitch<v->width*2 ||
       v->width<IM_W || v->height<IM_H ||
       v->width>4096 || v->height>2160)return 0;
    uint32_t sig=2166136261U;
    for(int y=14;y<240;y+=13) {
        const uint8_t *row=v->vram+(y*v->height/240)*v->pitch;
        for(int x=13;x<360;x+=19) {
            int sx=x*v->width/360;
            sig^=row[sx*2+1];
            sig*=16777619U;
        }
    }
    return sig;
}

/* File reads may complete partially on Canon's SD interface. Ensure
 * that each requested block is consumed, or stop on a real I/O error. */
static int read_exact(FILE *f, void *dest, int count)
{
    uint8_t *p=(uint8_t*)dest;
    while (count>0) {
        int chunk=count>4096?4096:count;
        int got=FIO_ReadFile(f,p,chunk);
        if(got<=0 || got>chunk)return 0;
        p+=got; count-=got;
    }
    return 1;
}

/* For QEMU and desktop tests: one fixed-format P5 grayscale frame.
 * Strict header avoids any parsing of untrusted data on a camera. */
static int capture_test_pgm(void)
{
    char header[15];
    FILE *f=FIO_OpenFile(test_files[(unsigned)test_image<4?test_image:0],O_RDONLY | O_SYNC);
    static const char expected[]="P5\n360 240\n255\n";
    if(!f) {strcpy(last_status,"TEST.PGM not found");return 0;}
    if(!read_exact(f,header,15) || memcmp(header,expected,15)!=0) {
        strcpy(last_status,"TEST.PGM invalid header");
        FIO_CloseFile(f);return 0;
    }
    int ok=read_exact(f,image,IM_W*IM_H);
    FIO_CloseFile(f);
    if(!ok)strcpy(last_status,"TEST.PGM read failed");
    return ok;
}

/* cos(declination), in Q14. One 5-degree lookup/interpolation.
 * Catalogue plane is accurate enough within a few degrees of hint. */
static int cos_dec_q14(int deg)
{
    static const uint16_t lut[]={
        16384,16322,16135,15825,15396,14849,14189,13422,12552,11585,
        10531,9396,8192,6925,5603,4240,2845,1428,0
    };
    int a=deg<0?-deg:deg;
    if(a>=90)return 0;
    return lut[a/5]+((int)lut[a/5+1]-(int)lut[a/5])*(a%5)/5;
}
/* A plane with x=RA*cos(dec_hint), y=Dec is not flat enough across a
 * several-degree star field, especially near the celestial poles.
 * Apply a second-order tangent-plane approximation in centidegrees.
 * tan(dec) is Q14, and 5729.58=100*(180/pi). Avoid floating point. */
static int32_t tan_dec_q14(int dec)
{
    int cosq=cos_dec_q14(dec);
    if(cosq<1)return 0;
    int sinq=cos_dec_q14(90-(dec<0?-dec:dec));
    if(dec<0)sinq=-sinq;
    return (int32_t)(((int64_t)sinq*16384)/cosq);
}
static void tangent_project(int x,int y,int tanq,int *px,int *py)
{
    int64_t div=(int64_t)572958*16384;
    int64_t dx=((int64_t)x*y*tanq*100)/div;
    int64_t dy=((int64_t)x*x*tanq*100)/(2*div);
    *px=x-(int)dx;
    *py=y+(int)dy;
}
#define ASTRO_RA_BINS 72
#define ASTRO_DEC_BINS 36
#define ASTRO_TILE_COUNT (ASTRO_RA_BINS * ASTRO_DEC_BINS)
/* 5-degree, 2D direct-address index. One small table in RAM and one
 * contiguous binary catalog on SD. Each sky tile is a file segment,
 * not an individual FAT directory entry. */
static uint32_t tile_offsets[ASTRO_TILE_COUNT+1];

static int read_catalog(int fov_cdeg)
{
    char header[20];
    struct record { uint32_t ra; int32_t dec; uint16_t mag10; uint16_t pad; };
    struct record recs[128];
    uint32_t count,version,bytes,index_version,index_count,nra,ndec;
    int n=0,cosq=cos_dec_q14(hint_dec);
    int limit_x=fov_cdeg/2+360, limit_y=fov_cdeg/3+360;
    int32_t ra_hint=(hint_rah*15*10000 + hint_ram*2500);
    int32_t dec_hint=hint_dec*10000;
    int tanq=tan_dec_q14(hint_dec);

    FILE *idx=FIO_OpenFile("ML/ASTRO/STARS.IDX",O_RDONLY | O_SYNC);
    if(!idx)return -2;
    if(!read_exact(idx,header,20) || memcmp(header,"SIDX",4)) {
        FIO_CloseFile(idx);return -2;
    }
    memcpy(&index_version,header+4,4);
    memcpy(&index_count,header+8,4);
    memcpy(&nra,header+12,4);
    memcpy(&ndec,header+16,4);
    if(index_version!=2 || nra!=ASTRO_RA_BINS ||
       ndec!=ASTRO_DEC_BINS || index_count>200000 ||
       !read_exact(idx,tile_offsets,sizeof(tile_offsets))) {
        FIO_CloseFile(idx);return -2;
    }
    FIO_CloseFile(idx);
    if(tile_offsets[0] || tile_offsets[ASTRO_TILE_COUNT]!=index_count)
        return -2;
    for(int i=1;i<=ASTRO_TILE_COUNT;i++)
        if(tile_offsets[i]<tile_offsets[i-1] ||
           tile_offsets[i]>index_count)return -2;

    FILE *f=FIO_OpenFile("ML/ASTRO/STARS.BIN",O_RDONLY | O_SYNC);
    if(!f)return -1;
    if(!read_exact(f,header,16) || memcmp(header,CATALOG_MAGIC,4)) {
        FIO_CloseFile(f);return -1;
    }
    memcpy(&version,header+4,4);
    memcpy(&count,header+8,4);
    memcpy(&bytes,header+12,4);
    if(version!=CATALOG_VERSION || bytes!=CATALOG_ENTRY_BYTES ||
       count!=index_count) {
        FIO_CloseFile(f);return -1;
    }

    /* Pointing hint defines a rectangle in the tangent plane.
     * Conservatively include a 5-degree tile whenever it may overlap.
     * The margin avoids missing stars at bin boundaries. */
    int64_t ra_half=(cosq>0) ?
        ((int64_t)limit_x*100*16384+cosq-1)/cosq+1000 : 1800000;
    if(ra_half>1800000)ra_half=1800000;
    int dec_half=limit_y*100+1000;
    for(int decbin=0;decbin<ASTRO_DEC_BINS;decbin++) {
        int dec_center=-900000+decbin*50000+25000;
        int decdelta=dec_center-dec_hint;
        if(decdelta<0)decdelta=-decdelta;
        if(decdelta>dec_half+25000)continue;
        for(int rabin=0;rabin<ASTRO_RA_BINS;rabin++) {
            int tile=decbin*ASTRO_RA_BINS+rabin;
            int ra_center=rabin*50000+25000;
            int delta=ra_center-ra_hint;
            if(delta<0)delta=-delta;
            if(delta>1800000)delta=3600000-delta;
            if((int64_t)delta>ra_half+25000)continue;
            uint32_t pos=tile_offsets[tile];
            uint32_t end=tile_offsets[tile+1];
            if(pos==end)continue;
            int64_t byteoffset=16+(int64_t)pos*CATALOG_ENTRY_BYTES;
            if(FIO_SeekSkipFile(f,byteoffset,SEEK_SET)!=byteoffset) {
                FIO_CloseFile(f);return -1;
            }
            while(pos<end) {
                int take=end-pos<128?end-pos:128;
                if(!read_exact(f,recs,take*CATALOG_ENTRY_BYTES)) {
                    FIO_CloseFile(f);return -1;
                }
                for(int j=0;j<take;j++) {
                    int32_t dra=(int32_t)recs[j].ra-ra_hint;
                    int32_t ddec=recs[j].dec-dec_hint;
                    if(dra>1800000)dra-=3600000;
                    if(dra< -1800000)dra+=3600000;
                    int x=(int)((int64_t)dra*cosq/16384/100);
                    int y=(int)(ddec/100);
                    if(x< -limit_x || x>limit_x || y< -limit_y || y>limit_y)
                        continue;
                    /* Fix curved RA/Dec grid before matching image geometry.
                     * Filter the original coordinates so all tile overlap
                     * and catalog-selection tests remain conservative. */
                    tangent_project(x,y,tanq,&x,&y);
                    int mag=recs[j].mag10,k;
                    if(n==ASTRO_MAX_CATALOG && mag>=nearby[n-1].mag10)
                        continue;
                    if(n<ASTRO_MAX_CATALOG)n++;
                    for(k=n-1;k>0 && nearby[k-1].mag10>mag;k--) {
                        nearby[k]=nearby[k-1];
                        nearby_names[k]=nearby_names[k-1];
                    }
                    nearby[k].x=x;nearby[k].y=y;nearby[k].mag10=mag;
                    nearby_names[k]=recs[j].pad;
                }
                pos+=take;
            }
        }
    }
    FIO_CloseFile(f);
    return n;
}

/* Read matching names from a small indexed ASCII sidecar file.
 * Max 12 names and a few seeking calls per solve; no heap allocations. */
static void prepare_annotations(const astro_solution *sol,int nobs,int ncat)
{
    n_matched_labels=0;
    if(!sol || !image || sol->anchor_obs>=nobs || sol->anchor_cat>=ncat)
        return;
    FILE *f=FIO_OpenFile("ML/ASTRO/STARS.NAM",O_RDONLY | O_SYNC);
    if(!f)return;
    char head[16],label[24];
    uint32_t version=0,count=0,record_size=0;
    if(!read_exact(f,head,16) || memcmp(head,"SNAM",4))goto end;
    memcpy(&version,head+4,4);
    memcpy(&count,head+8,4);
    memcpy(&record_size,head+12,4);
    if(version!=1 || count>65536 || record_size!=24)goto end;
    uint64_t used=0;
    for(int o=0;o<nobs && n_matched_labels<ANNOTATION_MAX;o++) {
        int best=-1,mind=17;
        for(int k=0;k<ncat;k++) {
            if(used & (1ULL<<k))continue;
            int dx=nearby[k].x-nearby[sol->anchor_cat].x;
            int dy=nearby[k].y-nearby[sol->anchor_cat].y;
            int px=observed[sol->anchor_obs].x+
                (int)(((int64_t)sol->a_q12*dx-(int64_t)sol->b_q12*dy)>>12);
            int py=observed[sol->anchor_obs].y+
                (int)(((int64_t)sol->b_q12*dx+(int64_t)sol->a_q12*dy)>>12);
            int ex=px-observed[o].x,ey=py-observed[o].y;
            int d=ex*ex+ey*ey;
            if(d<mind){mind=d;best=k;}
        }
        if(best<0)continue;
        used|=1ULL<<best;
        if(!nearby_names[best] || nearby_names[best]>=count)continue;
        int64_t offset=16+(int64_t)nearby_names[best]*24;
        if(FIO_SeekSkipFile(f,offset,SEEK_SET)!=offset)break;
        if(!read_exact(f,label,24))break;
        label[23]=0;
        if(!label[0])continue;
        struct annotation *a=&matched_labels[n_matched_labels++];
        a->x=observed[o].x; a->y=observed[o].y;
        memcpy(a->name,label,24);
    }
end:
    FIO_CloseFile(f);
}

static void draw_gallery_results(void)
{
    if(!gallery_results || !is_pure_play_photo_mode())return;
    /* Canon playback bitmap canvas is 720x480; underlying JPEG is not
     * modified. Only the verified matches get a caption. */
    BMP_LOCK(
        bmp_fill(COLOR_BLACK,8,6,345,102);
        bmp_printf(FONT(FONT_MED,COLOR_YELLOW,COLOR_BLACK),16,12,
                   "SKY FOUND (%d stars)",solved_matched_stars);
        bmp_printf(FONT(FONT_MED,COLOR_WHITE,COLOR_BLACK),16,43,
                   "RA: %s",solve_ra_text);
        bmp_printf(FONT(FONT_MED,COLOR_WHITE,COLOR_BLACK),16,72,
                   "Dec: %s",solve_dec_text);
        for(int i=0;i<n_matched_labels;i++) {
            int x=matched_labels[i].x*2; int y=matched_labels[i].y*2;
            if(x<14||x>690||y<16||y>454)continue;
            bmp_draw_rect(COLOR_YELLOW,x-5,y-5,10,10);
            if(i<8) {
                int tx=x+9;int ty=y+5;
                if(tx>565)tx=x-126;
                if(ty>452)ty=y-14;
                bmp_printf(FONT(FONT_SMALL,COLOR_YELLOW,COLOR_BLACK),
                           tx,ty,"%s",matched_labels[i].name);
            }
        }
        bmp_printf(FONT(FONT_SMALL,COLOR_WHITE,COLOR_BLACK),12,450,
                   "Q: hide  |  LEFT/RIGHT: other photo");
    );
}

static void draw_gallery_progress(void)
{
    if(!gallery_running || !is_pure_play_photo_mode())return;
    BMP_LOCK(
       bmp_fill(COLOR_BLACK,12,407,695,43);
       bmp_printf(FONT(FONT_MED,COLOR_YELLOW,COLOR_BLACK),
                  20,414,"Finding sky: %s",last_status);
    );
}

static void draw_gallery_prompt(void)
{
    if(!gallery_prompt || !is_pure_play_photo_mode())return;
    BMP_LOCK(
       bmp_fill(COLOR_BLACK,40,174,640,122);
       bmp_printf(FONT(FONT_LARGE,COLOR_YELLOW,COLOR_BLACK),
                  56,183,"Solve this photo?");
       bmp_printf(FONT(FONT_MED,COLOR_WHITE,COLOR_BLACK),
                  56,226,"SET: solve    Q: cancel");
       bmp_printf(FONT(FONT_SMALL,COLOR_WHITE,COLOR_BLACK),
                  56,260,"No sky coordinates or lens settings needed");
    );
}

/* Lens IO is injected into the portable guarded focus controller.
 * On physical cameras the Canon/ML focus API is the only code that sends
 * focus commands, and its boolean ACK alone is not trusted. */
static int ml_focus_ready(void *unused)
{
    return lv && lens_info.lens_exists && !is_manual_focus() &&
           !is_continuous_af() && !is_movie_mode() &&
           lens_info.job_state == 0;
}
static int ml_focus_position(void *unused,int *out)
{
    if(!out || !ml_focus_ready(0))return 0;
    *out=lens_info.focus_pos;
    return 1;
}
static int ml_focus_step(void *unused,int dir,int speed)
{
    if(!ml_focus_ready(0) || dir==0 || speed<1 || speed>3)return 0;
    return lens_focus(1,dir*speed,1,120) && ml_focus_ready(0);
}
static int ml_focus_frame(void *unused,uint8_t *dst,int width,int height)
{
    if(!dst || dst!=image || width!=IM_W || height!=IM_H)return 0;
    return lv && capture_liveview();
}
static void ml_focus_sleep(void *unused,int delay)
{
    msleep(delay);
}
static void do_focus(void)
{
    astro_focus_io io={0};
    astro_focus_result r={0};
    io.ready=ml_focus_ready;
    io.position=ml_focus_position;
    io.step=ml_focus_step;
    io.frame=ml_focus_frame;
    io.sleep_ms=ml_focus_sleep;
    int status=astro_run_autofocus(&io,image,IM_W,IM_H,
                                  scan_radius,focus_step,&r);
    if(status==ASTRO_FOCUS_OK)
        snprintf(last_status,sizeof(last_status),"Focused: %d steps, r2=%d.%02d",
                 r.commands,r.best_metric/16,
                 (r.best_metric%16)*100/16);
    else
        snprintf(last_status,sizeof(last_status),"%s",
                 astro_focus_status_text((enum astro_focus_status)status));
}
/* Read only bounded ranges. Sequential reads avoid an unnecessary FAT
 * chain seek, which is disproportionately expensive on the camera. */
struct sky_file_reader {FILE *file;uint32_t pos;};
static int sky_camera_read(void *ctx,uint32_t at,void *dest,uint32_t bytes){
    struct sky_file_reader *r=ctx;
    if(!r || !r->file || bytes>8192)return 0;
    if(r->pos!=at){
        if(FIO_SeekSkipFile(r->file,at,SEEK_SET)!=(int64_t)at)return 0;
        r->pos=at;
    }
    if(!read_exact(r->file,dest,bytes))return 0;
    r->pos+=bytes;return 1;
}
static int sky_camera_progress(void *unused,unsigned quads,unsigned checked){
    snprintf(last_status,sizeof(last_status),"Sky patterns %d / checks %d",quads,checked);
    return !solve_cancel && ((uint32_t)get_ms_clock()-solve_started)<45000;
}
static int solve_sky_on_camera(int nobs){
    uint32_t header[4];FILE *f=0,*pat=0,*fld=0;sky_dir_entry *dir=0;int result=-1;
    unsigned nkeys=0,npatterns=0,nfields=0;
    f=FIO_OpenFile("ML/ASTRO/SKY.DIR",O_RDONLY | O_SYNC);
    if(!f)goto done;
    if(!read_exact(f,header,16) || memcmp(header,"SDIR",4) || header[1]!=1 ||
       !header[2] || header[2]>40000 || header[3]>20000000)goto done;
    nkeys=header[2];npatterns=header[3];
    dir=malloc((nkeys+1)*sizeof(*dir));
    if(!dir){strcpy(last_status,"Not enough RAM for sky directory");goto done;}
    if(!read_exact(f,dir,(nkeys+1)*sizeof(*dir)))goto done;
    FIO_CloseFile(f);f=0;
    pat=FIO_OpenFile("ML/ASTRO/SKY.PAT",O_RDONLY | O_SYNC);
    if(!pat || !read_exact(pat,header,16) || memcmp(header,"SPAT",4) || header[1]!=1 ||
       header[2]!=npatterns || header[3]!=16)goto done;
    fld=FIO_OpenFile("ML/ASTRO/SKY.FLD",O_RDONLY | O_SYNC);
    if(!fld || !read_exact(fld,header,16) || memcmp(header,"SFLD",4) || header[1]!=1 ||
       !header[2] || header[2]>1000000 || header[3]!=SKY_FIELD_BYTES)goto done;
    nfields=header[2];
    struct sky_file_reader pr={pat,16},fr={fld,16};
    result=astro_sky_solve(observed,nobs,IM_W,IM_H,dir,nkeys,npatterns,nfields,
                           sky_camera_read,&pr,sky_camera_read,&fr,
                           sky_camera_progress,0,&sky_result);
done:
    if(f)FIO_CloseFile(f);
    if(pat)FIO_CloseFile(pat);
    if(fld)FIO_CloseFile(fld);
    if(dir)free(dir);
    return result;
}
static void prepare_sky_annotations(int nobs){
    n_matched_labels=0;
    FILE *f=FIO_OpenFile("ML/ASTRO/STARS.NAM",O_RDONLY | O_SYNC);
    if(!f)return;
    uint32_t head[4];
    if(!read_exact(f,head,16) || memcmp(head,"SNAM",4) || head[1]!=1 ||
       head[2]>65536 || head[3]!=24){FIO_CloseFile(f);return;}
    for(int i=0;i<nobs && n_matched_labels<ANNOTATION_MAX;i++){
        if(!(sky_result.matched_mask&(1u<<i)))continue;
        unsigned name=sky_result.names[i];
        if(!name || name>=head[2])continue;
        int64_t off=16+(int64_t)name*24;
        if(FIO_SeekSkipFile(f,off,SEEK_SET)!=off)break;
        struct annotation *out=&matched_labels[n_matched_labels];
        if(!read_exact(f,out->name,24))break;
        out->name[23]=0;out->x=observed[i].x;out->y=observed[i].y;
        n_matched_labels++;
    }
    FIO_CloseFile(f);
}
static void draw_file_preview(void){
    if(!file_preview || !image || gui_menu_shown())return;
    uint16_t hist[256]={0};
    for(int i=0;i<IM_W*IM_H;i+=4)hist[image[i]]++;
    int count=0,black=0,white=255;
    for(int v=0;v<256;v++){count+=hist[v];if(count>IM_W*IM_H/4/100){black=v;break;}}
    count=0;
    for(int v=0;v<256;v++){count+=hist[v];if(count>IM_W*IM_H/4*998/1000){white=v;break;}}
    if(white<=black+10)white=black+10;
    uint8_t lut[256];
    for(int v=0;v<256;v++){
        int value=(v-black)*100/(white-black);if(value<0)value=0;if(value>100)value=100;
        lut[v]=COLOR_GRAY(value);
    }
    BMP_LOCK(({
        uint8_t *vram=bmp_vram();
        if(vram){
            bmp_fill(COLOR_BLACK,0,0,720,480);
            /* Actual unmodified input photo, stretched for LCD visibility.
             * Solver and focus routines never see this display stretch. */
            for(int y=0;y<400;y++){
                const uint8_t *row=image+(y*3/5)*IM_W;
                uint8_t *dst=vram+(y+30)*BMPPITCH+60;
                for(int x=0;x<600;x++)dst[x]=lut[row[x*3/5]];
            }
            bmp_printf(FONT(FONT_MED,COLOR_YELLOW,COLOR_BLACK),12,2,"MIDNIGHT: SKY FOUND   %d stars",solved_matched_stars);
            int boxes[8][4],nb=0;
            for(int i=0;i<n_matched_labels;i++){
                int x=60+matched_labels[i].x*5/3,y=30+matched_labels[i].y*5/3;
                bmp_draw_rect(COLOR_BLACK,x-6,y-6,12,12);
                bmp_draw_rect(COLOR_YELLOW,x-5,y-5,10,10);
            }
            for(int i=0;i<n_matched_labels;i++){
                int x=60+matched_labels[i].x*5/3,y=30+matched_labels[i].y*5/3;
                if(nb>=8)continue;
                int width=strlen(matched_labels[i].name)*10+4;
                int tx=0,ty=0,placed=0;
                for(int choice=0;choice<4;choice++){
                    tx=(choice&1)?x-width-8:x+9;
                    ty=(choice&2)?y-20:y+5;
                    if(tx<3 || tx+width>717 || ty<31 || ty+16>429)continue;
                    int overlap=0;
                    /* A later star box must not overwrite an earlier name. */
                    for(int j=0;j<n_matched_labels;j++){
                        int mx=60+matched_labels[j].x*5/3,my=30+matched_labels[j].y*5/3;
                        if(tx<mx+7 && tx+width>mx-7 && ty<my+7 && ty+16>my-7){overlap=1;break;}
                    }
                    for(int j=0;j<nb;j++)if(tx<boxes[j][2]+4 && tx+width>boxes[j][0]-4 &&
                        ty<boxes[j][3]+3 && ty+16>boxes[j][1]-3){overlap=1;break;}
                    if(!overlap){placed=1;break;}
                }
                if(placed){
                    bmp_printf(FONT(FONT_SMALL,COLOR_YELLOW,COLOR_BLACK),tx,ty,"%s",matched_labels[i].name);
                    boxes[nb][0]=tx;boxes[nb][1]=ty;boxes[nb][2]=tx+width;boxes[nb][3]=ty+16;nb++;
                }
            }
            bmp_printf(FONT(FONT_MED,COLOR_WHITE,COLOR_BLACK),12,432,"RA %s   Dec %s",solve_ra_text,solve_dec_text);
            bmp_printf(FONT(FONT_SMALL,COLOR_WHITE,COLOR_BLACK),12,461,"Blind solve  %d.%02d s   %d patterns   Q: back",solve_elapsed/1000,(solve_elapsed%1000)/10,sky_result.quads);
        }
    }));
}
static void do_solve(int action){
    int test_mode=(action==3),gallery_mode=(action==5);
    solve_started=(uint32_t)get_ms_clock();solve_cancel=0;
    memset(&sky_result,0,sizeof(sky_result));
    if(!test_mode && !gallery_mode && lv_dispsize!=1){strcpy(last_status,"Use 1x LiveView");return;}
    if(gallery_mode)gallery_image_sig=gallery_photo_signature();
    if(!(test_mode?capture_test_pgm():capture_liveview())){
        if(!test_mode)strcpy(last_status,"No preview image");
        return;
    }
    /* Preserve the exact captured Live View frame. Detection masks trails
     * in-place, but the displayed result must show the original pixels,
     * not a later frame or the filtered working image. */
    uint8_t *snapshot=0;
    if(action==2){
        snapshot=malloc(IM_W*IM_H);
        if(!snapshot){strcpy(last_status,"Out of RAM for result preview");return;}
        memcpy(snapshot,image,IM_W*IM_H);
    }
    strcpy(last_status,"Detecting stars...");
    int masked=astro_mask_streak(image,IM_W,IM_H,IM_W);
    int n=astro_find_stars_psf(image,IM_W,IM_H,IM_W,observed,ASTRO_MAX_STARS);
    if(n<8){
        if(snapshot)free(snapshot);
        snprintf(last_status,sizeof(last_status),"%d stars; need at least 8",n);
        return;
    }
    int status=solve_sky_on_camera(n);
    solve_elapsed=(uint32_t)get_ms_clock()-solve_started;
    DryosDebugMsg(0,15,"ASTRO result=%d stars=%d matched=%d ms=%d trail=%d",status,n,sky_result.matched,solve_elapsed,masked);
    DryosDebugMsg(0,15,"ASTRO ra=%d dec=%d quads=%d reads=%d checks=%d",sky_result.ra,sky_result.dec,sky_result.quads,sky_result.reads,sky_result.checked);
    if(status!=1){
        if(snapshot)free(snapshot);
        strcpy(last_status,status==-2?"Solve cancelled / 45s limit":status<0?"Missing/corrupt SKY index":"No verified sky match");return;
    }
    uint32_t ra=(sky_result.ra+50)/100;
    int32_t dec=sky_result.dec;unsigned ad=(unsigned)(dec<0?-dec:dec);ad=(ad+50)/100;
    snprintf(solve_ra_text,sizeof(solve_ra_text),"%d.%02d deg",ra/100,ra%100);
    snprintf(solve_dec_text,sizeof(solve_dec_text),"%s%d.%02d deg",dec<0?"-":"+",ad/100,ad%100);
    solved_matched_stars=sky_result.matched;
    prepare_sky_annotations(n);
    snprintf(last_status,sizeof(last_status),"Solved %d stars in %d.%02ds",sky_result.matched,solve_elapsed/1000,(solve_elapsed%1000)/10);
    if(gallery_mode){result_from_gallery=1;gallery_result_pending=1;}
    if(snapshot){
        memcpy(image,snapshot,IM_W*IM_H);
        free(snapshot);
        file_preview=1;
    }
    if(test_mode){
        /* Reload original pixels: the bright trail must remain visible in
         * the preview even though the solver excluded it internally. */
        if(capture_test_pgm())file_preview=1;
    }
}
static void do_measure(void)
{
    int sx=IM_W/2,sy=IM_H/2,score;
    if(!lv || !capture_liveview()) {
        strcpy(last_status,"No LiveView frame");
        return;
    }
    score=astro_focus_metric(image,IM_W,IM_H,IM_W,&sx,&sy);
    if(score<0)strcpy(last_status,"No bright star; try 5x/10x LV");
    else snprintf(last_status,sizeof(last_status),
                  "Star (%d,%d) sharpness %d.%02d",
                  sx,sy,score/16,(score%16)*100/16);
}
/* The 1-Hz clock callback must remain short and should never scan
 * 86 KB of pixels. Run the actual algorithm in a separate worker. */
static void astro_overlay_worker(void *unused)
{
    int sx=overlay_x,sy=overlay_y,score=-1;
    if(image && lv && capture_liveview()) {
        if(overlay_lost<3 && overlay_score>=0) {
            score=astro_focus_metric_locked(image,IM_W,IM_H,IM_W,
                                            &sx,&sy,12);
        } else {
            score=astro_focus_metric(image,IM_W,IM_H,IM_W,&sx,&sy);
        }
    }
    if(score>=0) {
        overlay_x=sx;overlay_y=sy;overlay_lost=0;
    } else {
        overlay_lost++;
    }
    overlay_score=score;
    overlay_busy=0;
}
static unsigned int astro_seconds(unsigned int unused)
{
    if(file_preview){display_on();bmp_on();draw_file_preview();return 0;}
    /* Gallery drawings stay visible over the selected photograph, while
     * Canon may redraw the bitmap when overlays or UI states change. */
    if(gallery_prompt || gallery_results || gallery_running) {
        if(!is_pure_play_photo_mode()) {
            gallery_prompt=gallery_results=gallery_result_pending=0;
            gallery_running=0;
        } else if(gallery_prompt) draw_gallery_prompt();
        else if(gallery_results) draw_gallery_results();
        else draw_gallery_progress();
    }
    if(!focus_overlay || !lv || is_movie_mode() || gui_menu_shown())return 0;
    if(overlay_score>=0)
        bmp_printf(FONT(FONT_MED,COLOR_WHITE,COLOR_BLACK),22,56,
                   "Star focus: %d.%02d (lower=better)  ",
                   overlay_score/16,(overlay_score%16)*100/16);
    else
        bmp_printf(FONT(FONT_MED,COLOR_WHITE,COLOR_BLACK),22,56,
                   "Star focus: no star (try 5x LV) ");
    if(!busy && !overlay_busy) {
        if(!image)image=malloc(IM_W*IM_H);
        if(image) {
            overlay_busy=1;
            if(!task_create("astro_lv_score",0x1e,0x2400,
                            astro_overlay_worker,0))
                overlay_busy=0;
        }
    }
    return 0;
}
static void astro_worker(void *ignored)
{
    int action=requested_action;
    if(!image)image=malloc(IM_W*IM_H);
    if(!image) {
        strcpy(last_status,"Out of RAM (86 KB needed)");
    } else if(action==1)do_focus();
    else if(action==4)do_measure();
    else if(action==2 || action==3 || action==5) {
        if(action==2) {
            /* 3-second vibration settling before reading LIVEVIEW pixels.
             * Do not delay gallery images; their exposure was already taken. */
            strcpy(last_status,"Stabilizing: 3 seconds");
            for(int elapsed=0;elapsed<3000;elapsed+=100) {
                if(!lv || is_movie_mode() || is_play_or_qr_mode()) {
                    strcpy(last_status,"LiveView changed; solve cancelled");
                    goto completed;
                }
                msleep(100);
            }
        }
        if(action==5 && !is_pure_play_photo_mode()) {
            strcpy(last_status,"Photo playback closed");
            goto completed;
        }
        do_solve(action);
    }
completed:
    gallery_running=0;
    if((action==2 || action==3) && file_preview){
        gui_stop_menu();msleep(350);display_on();bmp_on();clrscr();draw_file_preview();
        busy=0;return;
    }
    if(action==5 && gallery_result_pending && is_pure_play_photo_mode() &&
       gallery_image_sig && gallery_image_sig==gallery_photo_signature()) {
        gallery_result_pending=0;
        gallery_results=1;
        draw_gallery_results();
    } else {
        if(action==5 && gallery_result_pending) {
            gallery_result_pending=0;
            strcpy(last_status,"Photo changed; result hidden");
        }
        NotifyBox(4500,"Astro: %s",last_status);
    }
    busy=0;
}
static void start_action(int action)
{
    if(busy || overlay_busy)return;
    if(!(is_camera("600D","1.0.2")||is_camera("6D","1.1.6"))) {
        NotifyBox(3000,"Astro: unsupported camera version");
        return;
    }
    busy=1;
    file_preview=0;
    requested_action=action;
    /* Make progress visible immediately when Solve is started in playback. */
    gallery_running=(action==5);
    if(action==2 || action==3 || action==5) {
        solve_ra_text[0]=0;
        solve_dec_text[0]=0;
        result_from_gallery=0;
        gallery_results=0;
        gallery_result_pending=0;
        n_matched_labels=0;
    }
    strcpy(last_status,"Running...");
    if(!task_create("astro_task",0x1d,0x4000,astro_worker,0)) {
        strcpy(last_status,"Could not start worker task");
        gallery_running=0;
        busy=0;
    }
}
/* Jump to the original Magic Lantern controls. Never duplicate its
 * intervalometer/long-exposure config or replace the core shooting logic.
 * Q/back from these submenus returns to their native tabs. */
static MENU_SELECT_FUNC(open_intervalometer)
{
    select_menu_by_name("Intervalometer","Take a pic every");
    menu_redraw();
}
static MENU_SELECT_FUNC(open_bulb_timer)
{
    select_menu_by_name("Bulb Timer","Exposure duration");
    menu_redraw();
}
static MENU_SELECT_FUNC(open_normal_menus)
{
    select_menu_by_name("Shoot","Intervalometer");
    menu_redraw();
}
static MENU_SELECT_FUNC(start_focus) { start_action(1); }
static MENU_SELECT_FUNC(start_solve) { start_action(2); }
static MENU_SELECT_FUNC(start_test) { start_action(3); }
static MENU_SELECT_FUNC(start_measure) { start_action(4); }

/* Mirror the EXISTING photo-only ML gain. Never use the mode-dependent
 * set_display_gain_equiv(): in movie mode that changes recorded digital ISO.
 * These symbols are exported by both pinned 600D.102 and 6D.116 builds.
 * No second persistent value: changes in either menu stay synchronized. */
extern int get_digic_iso_gain_photo(void);
extern int get_digic_iso_gain_movie(void);
extern void display_gain_toggle(void *priv, int delta);
static int preview_gain_ev=0;
static int get_preview_gain_ev(void)
{
    int gain=get_digic_iso_gain_photo();
    int ev=0;
    while(ev<6 && gain>(1024<<ev))ev++;
    return ev;
}
static MENU_SELECT_FUNC(change_preview_gain)
{
    if(busy || overlay_busy){
        NotifyBox(2000,"Gain locked during focus / solve");
        return;
    }
    if(is_movie_mode()){
        NotifyBox(2000,"Live View gain: photo mode only");
        return;
    }
    /* A single step, bounded at Off/+6 EV, avoids wrapping maximum gain
     * to Off or Off straight to maximum with an accidental left press. */
    int ev=get_preview_gain_ev();
    int step=delta>0?1:delta<0?-1:0;
    if(!step || (step<0 && ev==0) || (step>0 && ev==6))return;
    display_gain_toggle(0,step); /* always the PHOTO gain, never movie gain */
    preview_gain_ev=get_preview_gain_ev();
    /* Exposure overlays are not accurate when display gain is active. */
    redraw();
    DryosDebugMsg(0,15,"ASTRO gain photo=%d movie=%d iso=%d",
                  get_digic_iso_gain_photo(),get_digic_iso_gain_movie(),lens_info.raw_iso);
}
static MENU_UPDATE_FUNC(preview_gain_status)
{
    preview_gain_ev=get_preview_gain_ev();
    if(preview_gain_ev)
        MENU_SET_VALUE("+%d EV (%dx)",preview_gain_ev,1<<preview_gain_ev);
    else
        MENU_SET_VALUE("OFF");
    MENU_SET_ENABLED(preview_gain_ev);
    if(busy || overlay_busy)
        MENU_SET_WARNING(MENU_WARN_NOT_WORKING,"Wait for focus / solving before changing gain.");
    else if(is_movie_mode())
        MENU_SET_WARNING(MENU_WARN_NOT_WORKING,"Photo preview only. Movie gain is not changed.");
    else if(!lv)
        MENU_SET_WARNING(MENU_WARN_ADVICE,"Open photo Live View to see the gain. Photos are unchanged.");
    else if(preview_gain_ev)
        MENU_SET_WARNING(MENU_WARN_ADVICE,"Preview boosted: exposure display is not an exposure guide.");
}
static MENU_UPDATE_FUNC(astro_status)
{
    MENU_SET_VALUE("%s",last_status);
    MENU_SET_HELP("Status of the last astronomy action.");
}
static MENU_UPDATE_FUNC(astro_ra_result)
{
    MENU_SET_VALUE("%s",solve_ra_text[0]?solve_ra_text:"--");
    MENU_SET_HELP("Right ascension (sky longitude), in decimal degrees.");
}
static MENU_UPDATE_FUNC(astro_dec_result)
{
    MENU_SET_VALUE("%s",solve_dec_text[0]?solve_dec_text:"--");
    MENU_SET_HELP("Declination (sky latitude), in decimal degrees.");
}
static unsigned int astro_keypress(unsigned int key)
{
    if(file_preview){
        if(key==MODULE_KEY_Q || key==MODULE_KEY_PRESS_SET){
            file_preview=0;clrscr();gui_open_menu();return 0;
        }
        return 1;
    }
    if(busy && (requested_action==2 || requested_action==3 || requested_action==5) && key==MODULE_KEY_Q){solve_cancel=1;return 0;}
    /* Photo gallery: Q opens a confirmation, SET runs the solver on
     * the full-screen playback YUV preview. No file rewriting, no motor.
     * Esc/navigation restores Canon key behavior. */
    if(is_pure_play_photo_mode() && !gui_menu_shown()) {
        if(gallery_prompt) {
            if(key==MODULE_KEY_PRESS_SET && !busy) {
                gallery_prompt=0;
                clrscr();
                start_action(5);
                return 0;
            }
            if(key==MODULE_KEY_Q) {
                gallery_prompt=0;
                clrscr();
                return 0;
            }
            gallery_prompt=0;
            clrscr();
        }
        if(key==MODULE_KEY_Q) {
            if(gallery_results) {
                gallery_results=0;
                clrscr();
                return 0;
            }
            if(!busy) {
                gallery_prompt=1;
                draw_gallery_prompt();
                return 0;
            }
        }
        if(gallery_results && key!=MODULE_KEY_INFO) {
            gallery_results=0;
            clrscr();
        }
    } else {
        gallery_prompt=0;
        gallery_results=0;
    }
    if(shortcut>=1 && shortcut<=2 && !busy && !overlay_busy &&
       lv && !is_movie_mode() && !gui_menu_shown() && display_idle()) {
        int activate=(shortcut==1 &&
             detect_double_click(key,MODULE_KEY_PRESS_HALFSHUTTER,
                                     MODULE_KEY_UNPRESS_HALFSHUTTER)) ||
                     (shortcut==2 && key==MODULE_KEY_INFO);
        if(activate) {
            start_action(1);
            return shortcut==2 ? 0 : 1; /* INFO consumed, shutter preserved */
        }
    }
    return 1; /* allow normal Canon handling */
}
/* Model-specific buttons not exposed by Magic Lantern's portable key API.
 * Only intercept them in PHOTO Live View, never in Canon menus or playback.
 * Without electronic AF, toggle the manual focus readout instead of
 * sending an unworkable lens-motor command. */
static unsigned int astro_keypress_raw(unsigned int event_ptr)
{
    if(shortcut!=3 || !event_ptr)return 1;
    struct event *event=(struct event *)(uintptr_t)event_ptr;
    if(event->type!=0)return 1;
    int cam=is_camera("600D","1.0.2")?ASTRO_CAMERA_600D:
            (is_camera("6D","1.1.6")?ASTRO_CAMERA_6D:0);
    int action=astro_shortcut_decide(
        cam,event->param,1,lv && !is_movie_mode() && display_idle(),
        gui_menu_shown(),busy || overlay_busy,ml_focus_ready(0));
    if(action==ASTRO_SHORTCUT_AUTOFOCUS)start_action(1);
    else if(action==ASTRO_SHORTCUT_TOGGLE_READOUT) {
        focus_overlay=!focus_overlay;
        overlay_score=-1; overlay_lost=0;
        NotifyBox(2000,"Star readout %s",focus_overlay?"ON":"OFF");
    }
    return action?0:1;
}
static struct menu_entry astro_menu[] = {
    {
        .name="Midnight",
        .help="Astronomy quick controls. Other ML features remain available below.",
        .children=(struct menu_entry[]) {
            { .name="Intervalometer settings", .select=open_intervalometer,
              .help="Open the interval, start trigger and shot-count controls.",
              .icon_type=IT_ACTION },
            { .name="Bulb Timer settings", .select=open_bulb_timer,
              .help="Open the timer for a single long exposure.",
              .icon_type=IT_ACTION },
            { .name="Live View gain", .priv=&preview_gain_ev,.min=0,.max=6,
              .select=change_preview_gain,.update=preview_gain_status,
              .help="Photo preview only. ISO and saved photographs are unchanged.",
              .help2="LEFT/RIGHT: Off to +6 EV (64x). Gain also amplifies noise.",
              .depends_on=DEP_PHOTO_MODE,.icon_type=IT_PERCENT_OFF },
            { .name="Manual star focus", .priv=&focus_overlay,.min=0,.max=1,
              .help="LiveView sharpness score, no motor. Use the same star at 5x/10x." },
            { .name="Auto focus (no mask)", .select=start_focus,
              .help="Electronic AF lens only. Uses star size, not Bahtinov spikes.",
              .icon_type=IT_ACTION },
            { .name="Solve LiveView (3s)", .select=start_solve,
              .help="Wait 3 seconds, solve without hints and open the annotated frame.",
              .icon_type=IT_ACTION },
            { .name="Midnight advanced", .select=menu_open_submenu,
              .help="Focus tuning, last coordinates and developer-only file tests.",
              .children=(struct menu_entry[]) {
                { .name="Star focus settings", .select=menu_open_submenu,
                  .help="Motor tuning and button assignments. Usually leave defaults.",
                  .children=(struct menu_entry[]) {
                    { .name="Measure star now",.select=start_measure,
                      .help="One focus reading without lens movement. The live score is on Midnight home.",
                      .icon_type=IT_ACTION },
                    { .name="Motor step size",.priv=&focus_step,.min=1,.max=3,
                      .help="Electronic focus step command. Start at 1; actual movement depends on lens." },
                    { .name="Focus radius",.priv=&scan_radius,.min=1,.max=6,
                      .help="Number of motor steps tested to each side, not an image or sky radius." },
                    { .name="Focus shortcut",.priv=&shortcut,.min=0,.max=3,
                      .choices=CHOICES("Off","Double half-press","INFO button","DISP/LIGHT"),
                      .help="600D DISP / 6D LCD-light in photo LV. Choose Off to keep the Canon button." },
                    MENU_EOL
                  } },
                { .name="Last solve", .select=menu_open_submenu,
                  .help="Read-only result of the last solve. No pointing coordinates are required.",
                  .children=(struct menu_entry[]) {
                    { .name="Last result",.update=astro_status,.icon_type=IT_ACTION },
                    { .name="RA (deg)",.update=astro_ra_result,.icon_type=IT_ACTION },
                    { .name="Dec (deg)",.update=astro_dec_result,.icon_type=IT_ACTION },
                    MENU_EOL
                  } },
                { .name="Developer tests", .select=menu_open_submenu,
                  .help="PGM test files, not the Canon photo album. Not needed for normal shooting.",
                  .children=(struct menu_entry[]) {
                    { .name="Solve image file", .select=start_test,
                      .help="Developer test: solve the selected 360x240 PGM from ML/ASTRO.",
                      .icon_type=IT_ACTION },
                    { .name="Image file",.priv=&test_image,.min=0,.max=3,
                      .choices=CHOICES("TEST.PGM","TEST1.PGM","TEST2.PGM","TEST3.PGM"),
                      .help="TEST.PGM is synthetic. TEST1/2/3.PGM must be supplied separately." },
                    MENU_EOL
                  } },
                MENU_EOL
              } },
            { .name="Full Magic Lantern", .select=open_normal_menus,
              .help="Other ML photo, video, audio and system tools. No settings reset.",
              .icon_type=IT_ACTION },
            MENU_EOL
        },
        .icon_type=IT_SUBMENU,
        .submenu_width=710,
    },
};
static unsigned int astro_init(void)
{
    if(!(is_camera("600D","1.0.2")||is_camera("6D","1.1.6")))return CBR_RET_ERROR;
    menu_add("Focus",astro_menu,COUNT(astro_menu));
    return 0;
}
static unsigned int astro_deinit(void)
{
    if(busy || overlay_busy)return CBR_RET_ERROR;
    menu_remove("Focus",astro_menu,COUNT(astro_menu));
    if(image) {free(image); image=0;}
    return 0;
}
MODULE_INFO_START()
    MODULE_INIT(astro_init)
    MODULE_DEINIT(astro_deinit)
MODULE_INFO_END()
MODULE_CBRS_START()
    MODULE_CBR(CBR_KEYPRESS,astro_keypress,0)
    MODULE_CBR(CBR_KEYPRESS_RAW,astro_keypress_raw,0)
    MODULE_CBR(CBR_SECONDS_CLOCK,astro_seconds,0)
MODULE_CBRS_END()
