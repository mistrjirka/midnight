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

#define IM_W 360
#define IM_H 240
#define CATALOG_MAGIC "ASTR"
#define CATALOG_VERSION 1
#define CATALOG_ENTRY_BYTES 12

static CONFIG_INT("astro.hint_ra_h", hint_rah, 5);
static CONFIG_INT("astro.hint_ra_m", hint_ram, 0);
static CONFIG_INT("astro.hint_dec", hint_dec, 35);
static CONFIG_INT("astro.fov10", fov10, 0);  /* 0 auto from lens focal length */
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
    if (!lv || !image) return 0;
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
    FILE *f=FIO_OpenFile("ML/ASTRO/TEST.PGM",O_RDONLY | O_SYNC);
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
                    for(k=n-1;k>0 && nearby[k-1].mag10>mag;k--)
                        nearby[k]=nearby[k-1];
                    nearby[k].x=x;nearby[k].y=y;nearby[k].mag10=mag;
                }
                pos+=take;
            }
        }
    }
    FIO_CloseFile(f);
    return n;
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
    return capture_liveview();
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
static int astro_effective_fov(void)
{
    if(fov10>0)return fov10*10;  /* user override */
    int sensor_x10=is_camera("6D","1.1.6")?358:223;
    return astro_fov_cdeg(sensor_x10,lens_info.focal_len);
}
static void do_solve(int test_mode)
{
    int n, nc, fov=test_mode && fov10==0 ? 640 : astro_effective_fov();
    astro_solution result={0};
    if (fov<100 || fov>3000) {strcpy(last_status,"Set frame FOV 1-30 deg");return;}
    if(!test_mode && lv_dispsize != 1) {
        strcpy(last_status,"Use 1x LV for full frame solving");
        return;
    }
    if (!(test_mode?capture_test_pgm():capture_liveview())) {
        if(!test_mode) strcpy(last_status,"No LiveView frame");
        return;
    }
    /* Suppress long bright satellite/aircraft tracks in our own
     * scratch image. This never alters Canon's captured frame. */
    int streak_masked=astro_mask_streak(image,IM_W,IM_H,IM_W);
    n=astro_find_stars_psf(image,IM_W,IM_H,IM_W,observed,ASTRO_MAX_STARS);
    if(n<8) {snprintf(last_status,sizeof(last_status),"Only %d stars, need >=8",n);return;}
    strcpy(last_status,"Matching stars...");
    nc=read_catalog(fov);
    if(nc==-2) {strcpy(last_status,"Missing/invalid STARS.IDX");return;}
    if(nc<0) {strcpy(last_status,"Missing/invalid STARS.BIN");return;}
    if(nc<8) {strcpy(last_status,"Not enough catalog stars");return;}
    if(!astro_solve_hinted(observed,n,nearby,nc,IM_W,IM_H,fov,&result)) {
        snprintf(last_status,sizeof(last_status),
                 streak_masked?"%d stars; streak masked; no match":
                               "%d stars; no match",n);
        return;
    }
    /* Center is in tangent-plane centidegrees; undo the quadratic
     * approximation before converting back to celestial coordinates. */
    int32_t ra=(hint_rah*15*10000+hint_ram*2500);
    int cosq=cos_dec_q14(hint_dec);
    if(cosq<128) {strcpy(last_status,"Too close to celestial pole");return;}
    int tanq=tan_dec_q14(hint_dec);
    /* Inverse to 2nd order: deltaDec = y - tan(dec)*x*x/(2*5729.58),
     * deltaRA*cos(dec) = x + tan(dec)*x*y/5729.58. */
    int64_t div=(int64_t)572958*16384;
    int y=result.center_y_cdeg,x=result.center_x_cdeg;
    int64_t ra_plane=x + ((int64_t)x*y*tanq*100)/div;
    int64_t dec_plane=y - ((int64_t)x*x*tanq*100)/(2*div);
    ra+=(int32_t)(ra_plane*100*16384/cosq);
    int32_t dec=(int32_t)hint_dec*10000+(int32_t)(dec_plane*100);
    while(ra<0)ra+=3600000;
    while(ra>=3600000)ra-=3600000;
    /* RA as hours/minutes/seconds, Dec as degrees/minutes/seconds.
     * Separate menu rows prevent clipped coordinates on 720px LCD. */
    uint32_t ra_seconds=(uint32_t)(((int64_t)ra*86400+1800000)/3600000)%86400;
    int32_t dec_abs=dec<0?-dec:dec;
    uint32_t dec_seconds=(uint32_t)(((int64_t)dec_abs*3600+5000)/10000);
    snprintf(solve_ra_text,sizeof(solve_ra_text),"%02d:%02d:%02d",
             (int)(ra_seconds/3600),(int)((ra_seconds%3600)/60),
             (int)(ra_seconds%60));
    snprintf(solve_dec_text,sizeof(solve_dec_text),"%s%02dd%02dm%02ds",
             dec<0?"-":"+",(int)(dec_seconds/3600),
             (int)((dec_seconds%3600)/60),(int)(dec_seconds%60));
    snprintf(last_status,sizeof(last_status),"Solved: %d stars",result.matched);
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
    else if(action==2 || action==3)do_solve(action==3);
    NotifyBox(5000,"Astro: %s",last_status);
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
    requested_action=action;
    if(action==2 || action==3) {
        solve_ra_text[0]=0;
        solve_dec_text[0]=0;
    }
    strcpy(last_status,"Running...");
    if(!task_create("astro_task",0x1d,0x4000,astro_worker,0)) {
        strcpy(last_status,"Could not start worker task");
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
static MENU_UPDATE_FUNC(astro_status)
{
    MENU_SET_VALUE("%s",last_status);
    MENU_SET_HELP("Status of the last astronomy action.");
}
static MENU_UPDATE_FUNC(astro_ra_result)
{
    MENU_SET_VALUE("%s",solve_ra_text[0]?solve_ra_text:"--");
    MENU_SET_HELP("Solved right ascension, hours:minutes:seconds.");
}
static MENU_UPDATE_FUNC(astro_dec_result)
{
    MENU_SET_VALUE("%s",solve_dec_text[0]?solve_dec_text:"--");
    MENU_SET_HELP("Solved declination, signed degrees/minutes/seconds.");
}
static unsigned int astro_keypress(unsigned int key)
{
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
        .help="Astrophotography home. Q/LEFT returns to the standard tabs.",
        .children=(struct menu_entry[]) {
            { .name="Intervalometer settings", .select=open_intervalometer,
              .help="Jump to ML's existing intervalometer and exposure interval.",
              .icon_type=IT_ACTION },
            { .name="Bulb Timer settings", .select=open_bulb_timer,
              .help="Jump to ML's existing bulb exposure timer.",
              .icon_type=IT_ACTION },
            { .name="Manual star focus", .priv=&focus_overlay,.min=0,.max=1,
              .help="LiveView sharpness score, no motor. Use a bright star at 5x/10x." },
            { .name="Measure star now", .select=start_measure,
              .help="Read-only one-shot sharpness check; never moves a lens.",
              .icon_type=IT_ACTION },
            { .name="Auto focus (no mask)", .select=start_focus,
              .help="Electronic lens only; uses star sharpness, NOT Bahtinov spikes.",
              .icon_type=IT_ACTION },
            { .name="Solve sky (hinted)", .select=start_solve,
              .help="Current solver still requires a rough RA/Dec hint. Blind solving under development.",
              .icon_type=IT_ACTION },
            { .name="Astronomy settings", .select=menu_open_submenu, .children=(struct menu_entry[]) {
                { .name="Solve test PGM", .select=start_test,
                  .help="Use the included synthetic image to test the solver.",
                  .icon_type=IT_ACTION },
                { .name="Pointing RA hour", .priv=&hint_rah, .min=0,.max=23 },
                { .name="Pointing RA minute", .priv=&hint_ram,.min=0,.max=59 },
                { .name="Pointing Dec deg", .priv=&hint_dec,.min=-85,.max=85 },
                { .name="Frame width (0.1 deg)",.priv=&fov10,.min=0,.max=300,
                  .help="0=auto if the lens reports focal length. Otherwise set manual 1-30 deg." },
                { .name="Motor step size",.priv=&focus_step,.min=1,.max=3 },
                { .name="Focus radius",.priv=&scan_radius,.min=1,.max=6 },
                { .name="Focus shortcut",.priv=&shortcut,.min=0,.max=3,
                  .choices=CHOICES("Off","Double half-press","INFO button","DISP/LIGHT"),
                  .help="Default ON: 600D DISP, 6D LCD light. Set Off to restore the original button. Photo LV only." },
                { .name="Last result",.update=astro_status,.icon_type=IT_ACTION },
                { .name="Solved RA",.update=astro_ra_result,.icon_type=IT_ACTION },
                { .name="Solved Dec",.update=astro_dec_result,.icon_type=IT_ACTION },
                MENU_EOL
            } },
            { .name="Full Magic Lantern", .select=open_normal_menus,
              .help="Leave the Midnight dashboard and show the original shooting menus.",
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
