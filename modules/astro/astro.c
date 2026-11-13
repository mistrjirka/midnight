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
#include "astro_wcs.h"
#include "astro_overlay.h"
#include "astro_draw.h"
#include <timer.h>
#include <powersave.h>

#define IM_W 360
#define IM_H 240
static volatile int gallery_prompt=0;
static volatile int gallery_results=0;
static volatile int gallery_result_pending=0;
static volatile int gallery_running=0;
static volatile int gallery_failed=0;
static uint32_t gallery_prompt_time=0;
static int solved_matched_stars=0;
static int result_from_gallery=0;
static uint32_t gallery_image_sig=0;
static CONFIG_INT("astro.test_image",test_image,0);
static volatile int file_preview=0,solve_cancel=0;
static uint32_t solve_started=0,solve_elapsed=0;
static sky_solution sky_result;
static const char *test_files[]={"ML/ASTRO/TEST.PGM","ML/ASTRO/TEST1.PGM", "ML/ASTRO/TEST2.PGM","ML/ASTRO/TEST3.PGM"};

/* Lens: 0 blind (no hint), 1 focal length reported by an EF lens,
 * 2 manual value for lenses without electronic contacts. */
static CONFIG_INT("astro.focal_src", focal_source, 0);
static CONFIG_INT("astro.focal_mm", manual_focal, 50);
/* Target picker: catalogue (astro_overlay.h IDs) and number. */
static CONFIG_INT("astro.tgt_cat", target_cat, ASTRO_CAT_M);
static CONFIG_INT("astro.tgt_num", target_num, 42);
static CONFIG_INT("astro.tgt_on", target_on, 1);
/* Overlay layers drawn over solved frames. */
static CONFIG_INT("astro.ov_stars", ov_stars, 1);
static CONFIG_INT("astro.ov_dso", ov_dso, 1);
static CONFIG_INT("astro.ov_const", ov_const, 1);
static CONFIG_INT("astro.ov_dsomag", ov_dso_mag, 9);
static astro_scene scene, shown_scene;
static int scene_valid=0;
static astro_target target;
static volatile int target_cat_loaded=0,target_num_loaded=0,lookup_busy=0;
static char target_info[48]="";
static char solve_focal_text[24]="";
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
static int observed_count=0;

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

static void draw_scene_gallery(void);
static void draw_gallery_results(void)
{
    if(!gallery_results || !is_pure_play_photo_mode())return;
    /* Canon playback bitmap canvas is 720x480; underlying JPEG is not
     * modified. Overlays come from the verified solution only. */
    BMP_LOCK(
        draw_scene_gallery();
        bmp_fill(COLOR_BLACK,8,6,345,102);
        bmp_printf(FONT(FONT_MED,COLOR_YELLOW,COLOR_BLACK),16,12,
                   "SKY FOUND (%d stars)",solved_matched_stars);
        bmp_printf(FONT(FONT_MED,COLOR_WHITE,COLOR_BLACK),16,43,
                   "RA: %s",solve_ra_text);
        bmp_printf(FONT(FONT_MED,COLOR_WHITE,COLOR_BLACK),16,72,
                   "Dec: %s",solve_dec_text);
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

static void draw_gallery_failure(void)
{
    if(!gallery_failed || !is_pure_play_photo_mode())return;
    BMP_LOCK(
        bmp_fill(COLOR_BLACK,14,365,692,88);
        bmp_printf(FONT(FONT_MED,COLOR_YELLOW,COLOR_BLACK),
                   22,371,"MIDNIGHT: COULD NOT SOLVE");
        bmp_printf(FONT(FONT_MED,COLOR_WHITE,COLOR_BLACK),
                   22,402,"%s",last_status);
        bmp_printf(FONT(FONT_SMALL,COLOR_WHITE,COLOR_BLACK),
                   22,430,"Q: dismiss | LEFT/RIGHT: other photo");
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
static uint32_t solve_budget_ms=45000;
static int sky_camera_progress(void *unused,unsigned quads,unsigned checked){
    snprintf(last_status,sizeof(last_status),"Sky patterns %d / checks %d",quads,checked);
    return !solve_cancel && ((uint32_t)get_ms_clock()-solve_started)<solve_budget_ms;
}
static int sensor_width_x10(void){return is_camera("6D","1.1.6")?358:223;}
/* Focal-length hint in mm, or 0 for a fully blind solve. */
static int hint_focal_mm(void){
    if(focal_source==1)return lens_info.lens_exists?(int)lens_info.focal_len:0;
    if(focal_source==2)return manual_focal;
    return 0;
}
/* Expected scale (Q16 px per millidegree) for the hint, +-30 %:
 * IM_W*f/sensor pixels per radian of tangent, times pi/180000. */
static void focal_window(sky_options *opt){
    int f=hint_focal_mm();
    memset(opt,0,sizeof(*opt));
    if(f<4)return;
    int64_t s=(int64_t)IM_W*f*65536*31416/((int64_t)sensor_width_x10()*18000*10000);
    opt->scale_min=(uint32_t)(s*10/13);opt->scale_max=(uint32_t)(s*13/10);
}
/* -3: index absent, -2: cancelled, -1: corrupt, 0: no match, 1: solved. */
static int solve_index(const char *prefix,int nobs,const sky_options *opt){
    uint32_t header[4];FILE *f=0,*pat=0,*fld=0;sky_dir_entry *dir=0;int result=-1;
    unsigned nkeys=0,npatterns=0,nfields=0;char path[32];
    snprintf(path,sizeof(path),"ML/ASTRO/%s.DIR",prefix);
    f=FIO_OpenFile(path,O_RDONLY | O_SYNC);
    if(!f){result=-3;goto done;}
    if(!read_exact(f,header,16) || memcmp(header,"SDIR",4) || header[1]!=1 ||
       !header[2] || header[2]>40000 || header[3]>20000000)goto done;
    nkeys=header[2];npatterns=header[3];
    dir=malloc((nkeys+1)*sizeof(*dir));
    if(!dir){strcpy(last_status,"Not enough RAM for sky directory");goto done;}
    if(!read_exact(f,dir,(nkeys+1)*sizeof(*dir)))goto done;
    FIO_CloseFile(f);f=0;
    snprintf(path,sizeof(path),"ML/ASTRO/%s.PAT",prefix);
    pat=FIO_OpenFile(path,O_RDONLY | O_SYNC);
    if(!pat || !read_exact(pat,header,16) || memcmp(header,"SPAT",4) || header[1]!=1 ||
       header[2]!=npatterns || header[3]!=16)goto done;
    snprintf(path,sizeof(path),"ML/ASTRO/%s.FLD",prefix);
    fld=FIO_OpenFile(path,O_RDONLY | O_SYNC);
    if(!fld || !read_exact(fld,header,16) || memcmp(header,"SFLD",4) || header[1]!=1 ||
       !header[2] || header[2]>1000000 || header[3]!=SKY_FIELD_BYTES)goto done;
    nfields=header[2];
    struct sky_file_reader pr={pat,16},fr={fld,16};
    result=astro_sky_solve(observed,nobs,IM_W,IM_H,dir,nkeys,npatterns,nfields,
                           sky_camera_read,&pr,sky_camera_read,&fr,
                           sky_camera_progress,0,opt,&sky_result);
done:
    if(f)FIO_CloseFile(f);
    if(pat)FIO_CloseFile(pat);
    if(fld)FIO_CloseFile(fld);
    if(dir)free(dir);
    return result;
}
static int solve_sky_on_camera(int nobs){
    sky_options opt;focal_window(&opt);
    int r=solve_index("SKY",nobs,&opt);
    if(r==-3)r=-1;
    /* Optional wide-lens index (make_sky_index.py --wide), only if installed. */
    if(r==0 && !solve_cancel){
        int wide=solve_index("SKYW",nobs,&opt);
        if(wide==1 || wide==-2)r=wide;
    }
    return r;
}

/* Overlay data files, opened only while a scene is built or a target looked up. */
struct overlay_files {struct sky_file_reader r[7];astro_sky_files f;};
static void open_overlay_files(struct overlay_files *o){
    static const char *const names[7]={"STARS.BIN","STARS.IDX","STARS.NAM",
        "DSO.BIN","DSO.KEY","DSO.NAM","CONST.BIN"};
    memset(o,0,sizeof(*o));
    astro_file *slots[7]={&o->f.stars,&o->f.stars_idx,&o->f.stars_nam,&o->f.dso,
                          &o->f.dso_key,&o->f.dso_nam,&o->f.constellations};
    char path[32];
    for(int i=0;i<7;i++){
        snprintf(path,sizeof(path),"ML/ASTRO/%s",names[i]);
        o->r[i].file=FIO_OpenFile(path,O_RDONLY | O_SYNC);
        o->r[i].pos=0;
        if(o->r[i].file){slots[i]->read=sky_camera_read;slots[i]->ctx=&o->r[i];}
    }
}
static void close_overlay_files(struct overlay_files *o){
    for(int i=0;i<7;i++)if(o->r[i].file)FIO_CloseFile(o->r[i].file);
}
/* Resolve the picked designation (SD reads: worker or lookup task only). */
static void load_target(void){
    int cat=target_cat,num=target_num;
    if(target_cat_loaded==cat && target_num_loaded==num)return;
    struct overlay_files o;open_overlay_files(&o);
    int r=astro_dso_lookup(&o.f,cat,num,&target);
    if(r==1){
        char common[24];
        astro_dso_common_name(&o.f,&target.obj,common,sizeof(common));
        int m=target.obj.mag10;
        if(common[0])snprintf(target_info,sizeof(target_info),"%s",common);
        else if(m!=ASTRO_MAG_UNKNOWN)
            snprintf(target_info,sizeof(target_info),"%s, mag %s%d.%d",astro_dso_type_name(target.obj.type),
                     m<0?"-":"",(m<0?-m:m)/10,(m<0?-m:m)%10);
        else snprintf(target_info,sizeof(target_info),"%s",astro_dso_type_name(target.obj.type));
    } else snprintf(target_info,sizeof(target_info),"%s",r==0?"Not in catalogue":"DSO.BIN/KEY missing");
    close_overlay_files(&o);
    target_cat_loaded=cat;target_num_loaded=num;
}
static void lookup_task(void *unused){load_target();lookup_busy=0;}
/* Stars, deep-sky outlines, constellations and the target for the solution. */
static int wait_target_lookup(void){
    uint32_t started=(uint32_t)get_ms_clock();
    while(lookup_busy && (uint32_t)(get_ms_clock()-started)<3000)
        msleep(20);
    return !lookup_busy;
}
static void build_overlay(void){
    astro_overlay_opts opt={ov_stars,ov_dso,ov_const,45,ov_dso_mag*10};
    if(!wait_target_lookup()){scene_valid=0;return;}
    load_target();
    struct overlay_files o;open_overlay_files(&o);
    astro_build_scene(&sky_result.wcs,IM_W,IM_H,&opt,(target_on && target.valid)?&target:0,&o.f,&scene);
    close_overlay_files(&o);
    scene_valid=1;
}
static void format_result(void){
    uint32_t ts=(sky_result.ra*24+500)/1000;       /* seconds of time */
    if(ts>=86400)ts-=86400;
    snprintf(solve_ra_text,sizeof(solve_ra_text),"%02dh%02dm%02ds",(int)(ts/3600),(int)(ts/60%60),(int)(ts%60));
    int32_t dec=sky_result.dec;
    uint32_t as=((uint32_t)(dec<0?-dec:dec)*36+50)/100;   /* arcseconds */
    snprintf(solve_dec_text,sizeof(solve_dec_text),"%c%02dd%02d'%02d\"",dec<0?'-':'+',
             (int)(as/3600),(int)(as/60%60),(int)(as%60));
    int fov=sky_result.fov_cdeg,fx10=astro_lens_focal_x10(sensor_width_x10(),fov);
    snprintf(solve_focal_text,sizeof(solve_focal_text),"%d mm, FOV %d.%d",(fx10+5)/10,fov/100,fov/10%10);
}
static void draw_matched_boxes(int num,int den,int ox,int oy){
    for(int i=0;i<observed_count;i++){
        if(!(sky_result.matched_mask&(1u<<i)))continue;
        int x=ox+observed[i].x*num/den,y=oy+observed[i].y*num/den;
        bmp_draw_rect(COLOR_BLACK,x-6,y-6,12,12);
        bmp_draw_rect(COLOR_YELLOW,x-5,y-5,10,10);
    }
}
static void target_status(char *out,int size){
    out[0]=0;
    if(!scene_valid || !scene.target)return;
    unsigned sep=scene.target_sep;
    if(scene.target==1)snprintf(out,size,"%s in view, %d.%d deg from centre",scene.target_label,sep/10000,sep/1000%10);
    else snprintf(out,size,"%s %d.%d deg away: follow the arrow",scene.target_label,sep/10000,sep/1000%10);
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
            if(scene_valid){
                astro_map m={5,3,60,30,60,30,659,429};
                astro_draw_scene(&scene,&m,0);
            }
            draw_matched_boxes(5,3,60,30);
            char status[64];target_status(status,sizeof(status));
            bmp_printf(FONT(FONT_MED,COLOR_YELLOW,COLOR_BLACK),12,2,"MIDNIGHT: SKY FOUND   %d stars",solved_matched_stars);
            bmp_printf(FONT(FONT_MED,COLOR_WHITE,COLOR_BLACK),12,432,"RA %s  Dec %s",solve_ra_text,solve_dec_text);
            if(status[0])bmp_printf(FONT(FONT_SMALL,COLOR_RED,COLOR_BLACK),430,438,"%s",status);
            bmp_printf(FONT(FONT_SMALL,COLOR_WHITE,COLOR_BLACK),12,461,"%s %d.%02d s  lens ~%s  Q: back",
                       hint_focal_mm()?"Hinted":"Blind",solve_elapsed/1000,(solve_elapsed%1000)/10,solve_focal_text);
        }
    }));
}
static void draw_scene_gallery(void){
    if(scene_valid){
        astro_map m={2,1,0,0,0,0,719,445};
        astro_draw_scene(&scene,&m,0);
    }
    draw_matched_boxes(2,1,0,0);
}
/* Capture, detect and solve one frame. Returns the solver status. */
static int solve_frame(int test_mode){
    if(!(test_mode?capture_test_pgm():capture_liveview()))return -4;
    strcpy(last_status,"Detecting stars...");
    astro_mask_streak(image,IM_W,IM_H,IM_W);
    int n=astro_find_stars_psf(image,IM_W,IM_H,IM_W,observed,ASTRO_MAX_STARS);
    observed_count=n;
    if(n<8){snprintf(last_status,sizeof(last_status),"%d stars; need at least 8",n);return -5;}
    return solve_sky_on_camera(n);
}
static void do_solve(int action){
    int test_mode=(action==3),gallery_mode=(action==5);
    solve_started=(uint32_t)get_ms_clock();solve_cancel=0;
    memset(&sky_result,0,sizeof(sky_result));
    scene_valid=0;
    if(!test_mode && !gallery_mode && lv_dispsize!=1){strcpy(last_status,"Use 1x LiveView");return;}
    if(gallery_mode)gallery_image_sig=gallery_photo_signature();
    /* Preserve the exact captured Live View frame. Detection masks trails
     * in-place, but the displayed result must show the original pixels,
     * not a later frame or the filtered working image. */
    uint8_t *snapshot=0;
    if(action==2){
        snapshot=malloc(IM_W*IM_H);
        if(!snapshot){strcpy(last_status,"Out of RAM for result preview");return;}
    }
    if(!(test_mode?capture_test_pgm():capture_liveview())){
        if(!test_mode)strcpy(last_status,"No preview image");
        if(snapshot)free(snapshot);
        return;
    }
    if(snapshot)memcpy(snapshot,image,IM_W*IM_H);
    strcpy(last_status,"Detecting stars...");
    int masked=astro_mask_streak(image,IM_W,IM_H,IM_W);
    int n=astro_find_stars_psf(image,IM_W,IM_H,IM_W,observed,ASTRO_MAX_STARS);
    observed_count=n;
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
        strcpy(last_status,status==-2?"Solve cancelled / time limit":status<0?"Missing/corrupt SKY index":"No verified sky match");return;
    }
    format_result();
    solved_matched_stars=sky_result.matched;
    build_overlay();
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
/* Live guide: re-solve Live View continuously and keep an arrow (or the
 * outline) of the target on screen until Q, a menu or playback. The 3:2
 * Live View image fills the 720x480 overlay on the 600D and 6D. */
static void guide_hud(const char *text,int color){
    BMP_LOCK(
        bmp_fill(COLOR_BLACK,0,446,720,30);
        bmp_printf(FONT(FONT_MED,color,COLOR_BLACK),10,449,"%s",text);
    );
}
static void do_guide(void){
    astro_map m={2,1,0,0,0,0,719,445};
    int drawn=0,frames=0,solved=0;
    char text[64];
    solve_cancel=0;
    if(lv_dispsize!=1){strcpy(last_status,"Use 1x LiveView");return;}
    if(!wait_target_lookup()){
        strcpy(last_status,"Target catalogue lookup timed out");return;
    }
    load_target();
    if(!target.valid){strcpy(last_status,"Pick a target first");return;}
    for(int i=0;i<30 && gui_menu_shown();i++)msleep(50);   /* menu closing */
    guide_hud("Guide: hold still...   Q: stop",COLOR_WHITE);
    solve_budget_ms=hint_focal_mm()?8000:20000;
    while(!solve_cancel && lv && !is_movie_mode() && !gui_menu_shown() && !is_play_or_qr_mode()){
        solve_started=(uint32_t)get_ms_clock();
        memset(&sky_result,0,sizeof(sky_result));
        int status=solve_frame(0);
        frames++;
        if(solve_cancel || !lv || gui_menu_shown())break;
        if(status==1){
            solved++;
            build_overlay();
            BMP_LOCK(
                if(drawn)astro_draw_scene(&shown_scene,&m,1);
                astro_draw_scene(&scene,&m,0);
            );
            shown_scene=scene;drawn=1;
            target_status(text,sizeof(text));
            int used=strlen(text);
            snprintf(text+used,sizeof(text)-used,"   Q: stop");
            guide_hud(text,scene.target==1?COLOR_GREEN1:COLOR_YELLOW);
        } else {
            snprintf(text,sizeof(text),"%s: hold still / raise LV gain   Q: stop",
                     status==-5?"Too few stars":"No sky match");
            guide_hud(text,COLOR_WHITE);
        }
        msleep(200);
    }
    if(drawn)BMP_LOCK(astro_draw_scene(&shown_scene,&m,1););
    BMP_LOCK(bmp_fill(COLOR_EMPTY,0,446,720,30););
    solve_budget_ms=45000;
    snprintf(last_status,sizeof(last_status),"Guide stopped: %d/%d frames solved",solved,frames);
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
    if(gallery_prompt || gallery_results || gallery_running || gallery_failed) {
        /* Canon may temporarily change its playback dialog while servicing
         * button events. Do not discard a pending Q/SET confirmation solely
         * because PlayMain is temporarily not the active dialog. */
        if(gallery_prompt && (uint32_t)(get_ms_clock()-gallery_prompt_time)>15000) {
            gallery_prompt=0;
            strcpy(last_status,"Gallery: confirmation timed out");
        }
        if(!is_pure_play_photo_mode()) {
            if(!is_play_or_qr_mode()) {
                gallery_results=gallery_result_pending=gallery_failed=0;
                gallery_running=0;
            }
        } else if(gallery_prompt) draw_gallery_prompt();
        else if(gallery_results) draw_gallery_results();
        else if(gallery_failed) draw_gallery_failure();
        else if(gallery_running) draw_gallery_progress();
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
    else if(action==6)do_guide();
    else if(action==2 || action==3 || action==5) {
        if(action==2) {
            /* Leave the ML menu before acquiring the actual sensor frame.
             * The menu itself may otherwise obscure the Live View display. */
            gui_stop_menu();
            msleep(300);
            strcpy(last_status,"Stabilizing: 3 seconds");
            for(int elapsed=0;elapsed<3000;elapsed+=100) {
                if(!lv) {
                    strcpy(last_status,"LiveView switched off during solve");
                    goto completed;
                }
                if(is_movie_mode()) {
                    strcpy(last_status,"Switch to photo mode for LiveView solve");
                    goto completed;
                }
                if(is_play_or_qr_mode()) {
                    strcpy(last_status,"Playback opened during LiveView solve");
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
        if(action==5 && is_pure_play_photo_mode()) {
            gallery_failed=1;
            draw_gallery_failure();
        } else {
            NotifyBox(4500,"Astro: %s",last_status);
        }
    }
    busy=0;
}
static void start_action(int action)
{
    if(busy || overlay_busy) {
        if(action==5) {
            strcpy(last_status,"Gallery: another action is busy");
            gallery_failed=1;
            draw_gallery_failure();
        }
        return;
    }
    if(!(is_camera("600D","1.0.2")||is_camera("6D","1.1.6"))) {
        NotifyBox(3000,"Astro: unsupported camera version");
        return;
    }
    busy=1;
    file_preview=0;
    requested_action=action;
    /* Make progress visible immediately when Solve is started in playback. */
    gallery_running=(action==5);
    if(action==2 || action==3 || action==5 || action==6) {
        solve_ra_text[0]=0;
        solve_dec_text[0]=0;
        result_from_gallery=0;
        gallery_results=0;
        gallery_failed=0;
        gallery_result_pending=0;
        scene_valid=0;
    }
    strcpy(last_status,"Running...");
    if(action==5 && is_pure_play_photo_mode())
        draw_gallery_progress();
    if(!task_create("astro_task",0x1d,0x4000,astro_worker,0)) {
        strcpy(last_status,"Could not start worker task");
        gallery_running=0;
        busy=0;
        if(action==5 && is_pure_play_photo_mode()) {
            gallery_failed=1;
            draw_gallery_failure();
        } else {
            NotifyBox(4500,"Astro: %s",last_status);
        }
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
static MENU_SELECT_FUNC(start_solve)
{
    if(!lv) {
        strcpy(last_status,"Turn on 1x photo LiveView before solving");
    } else if(is_movie_mode()) {
        strcpy(last_status,"Switch to photo LiveView before solving");
    } else if(is_play_or_qr_mode()) {
        strcpy(last_status,"Close photo playback before LiveView solve");
    } else if(lv_dispsize!=1) {
        strcpy(last_status,"Use 1x LiveView (not 5x or 10x)");
    } else {
        start_action(2);
        return;
    }
    NotifyBox(5500,"Astro: %s",last_status);
}

static MENU_SELECT_FUNC(start_test) { start_action(3); }
static MENU_SELECT_FUNC(start_measure) { start_action(4); }
static MENU_SELECT_FUNC(start_guide) { gui_stop_menu(); start_action(6); }

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
    MENU_SET_HELP("Right ascension of the frame centre (J2000), hours/minutes/seconds.");
}
static MENU_UPDATE_FUNC(astro_dec_result)
{
    MENU_SET_VALUE("%s",solve_dec_text[0]?solve_dec_text:"--");
    MENU_SET_HELP("Declination of the frame centre (J2000), degrees/arcminutes/arcseconds.");
}
static MENU_UPDATE_FUNC(astro_focal_result)
{
    MENU_SET_VALUE("%s",solve_focal_text[0]?solve_focal_text:"--");
    MENU_SET_HELP("Focal length measured from the solved field. Use it for manual lenses.");
}
/* Caret editing (as in the intervalometer) needs .unit=UNIT_DEC and no
 * .select. The digit count follows the selected catalogue's range. */
static MENU_UPDATE_FUNC(target_number_update)
{
    int cat=(target_cat>=1 && target_cat<=ASTRO_CAT_COUNT)?target_cat:ASTRO_CAT_M;
    entry->max=astro_catalogue_max[cat];
    if(target_num>entry->max)target_num=entry->max;
    if(target_num<1)target_num=1;
    char name[24];astro_designation(cat,target_num,name,sizeof(name));
    MENU_SET_RINFO("%s",name);
}
static MENU_UPDATE_FUNC(target_object_update)
{
    if(target_cat_loaded!=target_cat || target_num_loaded!=target_num){
        if(!lookup_busy && !busy){
            lookup_busy=1;
            if(!task_create("astro_lookup",0x1e,0x2000,lookup_task,0))lookup_busy=0;
        }
        MENU_SET_VALUE("...");
    } else MENU_SET_VALUE("%s",target_info);
    if(target.valid && target_cat_loaded==target_cat && target_num_loaded==target_num){
        int maj=target.obj.maj;
        MENU_SET_RINFO("%d'",(maj+5)/10);
    }
}
static MENU_UPDATE_FUNC(focal_source_update)
{
    if(focal_source==1){
        if(lens_info.lens_exists && lens_info.focal_len)MENU_SET_RINFO("%dmm",lens_info.focal_len);
        else MENU_SET_WARNING(MENU_WARN_NOT_WORKING,"Lens reports no focal length: use Manual.");
    } else if(focal_source==2)MENU_SET_RINFO("%dmm",manual_focal);
}
static MENU_UPDATE_FUNC(manual_focal_update)
{
    int fov=astro_lens_fov_cdeg(sensor_width_x10(),manual_focal);
    MENU_SET_RINFO("FOV %d.%d",fov/100,fov/10%10);
    if(focal_source!=2)MENU_SET_WARNING(MENU_WARN_ADVICE,"Used only when Focal length source is Manual.");
    else if(fov>6000)MENU_SET_WARNING(MENU_WARN_ADVICE,"Over 60 deg: install the optional SKYW wide index.");
}
static unsigned int astro_gallery_keypress(unsigned int key)
{
    /* Service an outstanding confirmation BEFORE checking the transient
     * Canon dialog state. On the 600D, PLAY -> Q can briefly leave
     * PlayMain during button handling. Releasing SET to Canon also enables
     * the unrelated SET+MainDial exposure adjustment shortcut. */
    if(gallery_prompt) {
        if((uint32_t)(get_ms_clock()-gallery_prompt_time)>15000) {
            gallery_prompt=0;
            strcpy(last_status,"Gallery: confirmation timed out");
        } else if(!gui_menu_shown()) {
            if(key==MODULE_KEY_PRESS_SET) {
                gallery_prompt=0;
                clrscr();
                start_action(5);
                return 0;
            }
            if(key==MODULE_KEY_Q) {
                gallery_prompt=0;
                strcpy(last_status,"Gallery: cancelled before solve");
                clrscr();
                return 0;
            }
            if(key==MODULE_KEY_PRESS_LEFT || key==MODULE_KEY_PRESS_RIGHT ||
               key==MODULE_KEY_PLAY || key==MODULE_KEY_MENU ||
               key==MODULE_KEY_TRASH) {
                gallery_prompt=0;
                clrscr();
            }
            /* Ignore unrelated press/release keys. They must not dismiss
             * the confirmation or leak the SET press to SET+MainDial. */
            return 1;
        }
    }
    if(!is_pure_play_photo_mode() || gui_menu_shown()) {
        if(gui_menu_shown() || !is_play_or_qr_mode())
            gallery_prompt=gallery_results=gallery_failed=0;
        return 1;
    }
    if(key==MODULE_KEY_Q) {
        if(gallery_results || gallery_failed) {
            gallery_results=gallery_failed=0;
            clrscr();
            return 0;
        }
        if(!busy) {
            gallery_prompt=1;
            gallery_prompt_time=(uint32_t)get_ms_clock();
            strcpy(last_status,"Gallery: waiting for SET");
            draw_gallery_prompt();
            return 0;
        }
    }
    if(gallery_results || gallery_failed) {
        if(key==MODULE_KEY_PRESS_LEFT || key==MODULE_KEY_PRESS_RIGHT ||
           key==MODULE_KEY_PLAY || key==MODULE_KEY_MENU ||
           key==MODULE_KEY_TRASH) {
            gallery_results=gallery_failed=0;
            clrscr();
        }
    }
    return 1;
}

static unsigned int astro_keypress(unsigned int key)
{
    if(file_preview){
        if(key==MODULE_KEY_Q || key==MODULE_KEY_PRESS_SET){
            file_preview=0;clrscr();gui_open_menu();return 0;
        }
        return 1;
    }
    if(busy && (requested_action==2 || requested_action==3 || requested_action==5 || requested_action==6) &&
       key==MODULE_KEY_Q){solve_cancel=1;return 0;}
    if(!astro_gallery_keypress(key))return 0;
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
            { .name="Target & guide", .select=menu_open_submenu,
              .help="Pick an M/NGC/IC/C/B/Sh2 object; LiveView arrows lead you to it.",
              .children=(struct menu_entry[]) {
                { .name="Catalogue", .priv=&target_cat, .min=1, .max=ASTRO_CAT_COUNT,
                  .choices=CHOICES("Messier (M)","NGC","IC","Caldwell (C)","Barnard (B)","Sharpless (Sh2)"),
                  .help="Catalogue of the target object." },
                { .name="Number", .priv=&target_num, .min=1, .max=110, .unit=UNIT_DEC,
                  .update=target_number_update,
                  .help="Edit like the intervalometer time: LEFT/RIGHT digit, UP/DOWN value.",
                  .help2="Digits follow the catalogue: M 1-110, NGC 1-7840, IC 1-5386." },
                { .name="Object", .update=target_object_update, .icon_type=IT_ACTION,
                  .help="Catalogue entry for this number, from ML/ASTRO/DSO.BIN." },
                { .name="Guide in LiveView", .select=start_guide, .icon_type=IT_ACTION,
                  .help="1x photo LiveView: solves repeatedly and points to the target. Q stops.",
                  .help2="Set the lens focal length under Midnight advanced for faster updates." },
                { .name="Mark target", .priv=&target_on, .max=1,
                  .help="Show the target, or an arrow to it, on every solved frame." },
                { .name="Star names", .priv=&ov_stars, .max=1,
                  .help="Label stars up to magnitude 4.5 (Greek Bayer letters)." },
                { .name="Deep-sky outlines", .priv=&ov_dso, .max=1,
                  .help="Ellipses for M, C, bright NGC/IC, and large Barnard/Sharpless objects." },
                { .name="Constellations", .priv=&ov_const, .max=1,
                  .help="Constellation figures and names." },
                { .name="DSO magnitude limit", .priv=&ov_dso_mag, .min=6, .max=14,
                  .help="NGC/IC objects brighter than this are outlined. M and C always are." },
                MENU_EOL
              } },
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
                { .name="Lens focal length", .select=menu_open_submenu,
                  .help="Optional solving hint. Blind solving needs no lens data.",
                  .children=(struct menu_entry[]) {
                    { .name="Focal length source",.priv=&focal_source,.max=2,
                      .choices=CHOICES("None (blind)","From lens","Manual"),
                      .update=focal_source_update,
                      .help="A hint restricts the image scale: much less work per solve.",
                      .help2="Manual: for lenses without electronic contacts." },
                    { .name="Manual focal length",.priv=&manual_focal,.min=4,.max=1200,.unit=UNIT_DEC,
                      .update=manual_focal_update,
                      .help="Millimetres. LEFT/RIGHT: digit, UP/DOWN: value.",
                      .help2="Last solve shows the measured focal length for comparison." },
                    MENU_EOL
                  } },
                { .name="Last solve", .select=menu_open_submenu,
                  .help="Read-only result of the last solve. No pointing coordinates are required.",
                  .children=(struct menu_entry[]) {
                    { .name="Last result",.update=astro_status,.icon_type=IT_ACTION },
                    { .name="RA",.update=astro_ra_result,.icon_type=IT_ACTION },
                    { .name="Dec",.update=astro_dec_result,.icon_type=IT_ACTION },
                    { .name="Measured lens",.update=astro_focal_result,.icon_type=IT_ACTION },
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
