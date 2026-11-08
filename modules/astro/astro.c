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
static CONFIG_INT("astro.shortcut", shortcut, 0); /* OFF by default */
static CONFIG_INT("astro.overlay", focus_overlay, 0); /* opt-in manual focus overlay */
static volatile int busy = 0;
static volatile int overlay_busy = 0;
static volatile int overlay_score = -1;
static int overlay_x = IM_W/2, overlay_y = IM_H/2;
static int overlay_lost = 0;
static int requested_action = 0;
static char last_status[64] = "Ready";
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
static void path_from_card(char *out, size_t size, const char *rel)
{
    const struct card_info *card=get_ml_card();
    char drive=(card && card->drive_letter) ? card->drive_letter[0] : 'B';
    snprintf(out,size,"%c:/ML/ASTRO/%s",drive,rel);
}

/* For QEMU and desktop tests: one fixed-format P5 grayscale frame.
 * Strict header avoids any parsing of untrusted data on a camera. */
static int capture_test_pgm(void)
{
    char path[FIO_MAX_PATH_LENGTH];
    char header[15];
    FILE *f;
    static const char expected[]="P5\n360 240\n255\n";
    path_from_card(path,sizeof(path),"TEST.PGM");
    f=FIO_OpenFile(path,O_RDONLY);
    if(!f)return 0;
    int ok=FIO_ReadFile(f,header,15)==15 && memcmp(header,expected,15)==0 &&
           FIO_ReadFile(f,image,IM_W*IM_H)==IM_W*IM_H;
    FIO_CloseFile(f);
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
/* Binary catalogue, sourced from HYG (see tools/make_catalog.py).
 * All data is on SD. Keep only 64 brightest stars near the hint.
 * File access uses buffered sequential reads and static scratch space. */
static int read_catalog(int fov_cdeg)
{
    char path[FIO_MAX_PATH_LENGTH], header[16];
    struct record { uint32_t ra; int32_t dec; uint16_t mag10; uint16_t pad; };
    struct record recs[48];  /* 576-byte stack scratch; no heap allocation */
    FILE *f;
    uint32_t count,version,bytes;
    int n=0,cosq=cos_dec_q14(hint_dec), limit_x=fov_cdeg/2+360;
    int limit_y=fov_cdeg/3+360;
    int32_t ra_hint=(hint_rah*15*10000 + hint_ram*2500);
    int32_t dec_hint=hint_dec*10000;
    path_from_card(path,sizeof(path),"STARS.BIN");
    f=FIO_OpenFile(path,O_RDONLY);
    if(!f)return -1;
    if(FIO_ReadFile(f,header,16)!=16 || memcmp(header,CATALOG_MAGIC,4)) {
        FIO_CloseFile(f); return -1;
    }
    memcpy(&version,header+4,4);
    memcpy(&count,header+8,4);
    memcpy(&bytes,header+12,4);
    if(version!=CATALOG_VERSION || bytes!=CATALOG_ENTRY_BYTES || count>200000) {
        FIO_CloseFile(f); return -1;
    }
    for(uint32_t offset=0;offset<count;) {
        uint32_t take=(count-offset < 48)? count-offset:48;
        if(FIO_ReadFile(f,recs,take*12)!=(int)(take*12)) {n=-1;break;}
        for(uint32_t j=0;j<take;j++) {
            int32_t dra=(int32_t)recs[j].ra-ra_hint;
            int32_t ddec=recs[j].dec-dec_hint;
            if(dra>1800000)dra-=3600000;
            if(dra< -1800000)dra+=3600000;
            int x=(int)((int64_t)dra*cosq/16384/100);
            int y=(int)(ddec/100);
            if(x < -limit_x || x > limit_x || y < -limit_y || y>limit_y)continue;
            int mag=recs[j].mag10, k;
            /* maintain ascending magnitude ordering */
            if(n==ASTRO_MAX_CATALOG && mag>=nearby[n-1].mag10)continue;
            if(n<ASTRO_MAX_CATALOG)n++;
            for(k=n-1;k>0 && nearby[k-1].mag10>mag;k--)nearby[k]=nearby[k-1];
            nearby[k].x=x;
            nearby[k].y=y;
            nearby[k].mag10=mag;
        }
        offset+=take;
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
        strcpy(last_status,test_mode?"Missing/invalid TEST.PGM":"No LiveView frame");
        return;
    }
    n=astro_find_stars(image,IM_W,IM_H,IM_W,observed,ASTRO_MAX_STARS);
    if(n<8) {snprintf(last_status,sizeof(last_status),"Only %d stars, need >=8",n);return;}
    nc=read_catalog(fov);
    if(nc<0) {strcpy(last_status,"Missing/invalid STARS.BIN");return;}
    if(nc<8) {strcpy(last_status,"Not enough catalog stars");return;}
    if(!astro_solve_hinted(observed,n,nearby,nc,IM_W,IM_H,fov,&result)) {
        snprintf(last_status,sizeof(last_status),"%d stars, no match. Check hint",n);
        return;
    }
    /* The result is in centidegrees relative to the hint position. */
    int32_t ra=(hint_rah*15*10000+hint_ram*2500);
    int cosq=cos_dec_q14(hint_dec);
    if(cosq<128) {strcpy(last_status,"Too close to celestial pole");return;}
    ra+=(int32_t)((int64_t)result.center_x_cdeg*100*16384/cosq);
    int32_t dec=(int32_t)hint_dec*10000+result.center_y_cdeg*100;
    while(ra<0)ra+=3600000;
    while(ra>=3600000)ra-=3600000;
    snprintf(last_status,sizeof(last_status),
             "%d stars RA %02d:%02d Dec %c%d.%d",
             result.matched,ra/150000,(ra%150000)/2500,
             dec<0?'-':'+',(dec<0?-dec:dec)/10000,
             (dec<0?-dec:dec)%10000/1000);
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
    strcpy(last_status,"Running...");
    if(!task_create("astro_task",0x1d,0x4000,astro_worker,0)) {
        strcpy(last_status,"Could not start worker task");
        busy=0;
    }
}
static MENU_SELECT_FUNC(start_focus) { start_action(1); }
static MENU_SELECT_FUNC(start_solve) { start_action(2); }
static MENU_SELECT_FUNC(start_test) { start_action(3); }
static MENU_SELECT_FUNC(start_measure) { start_action(4); }
static MENU_UPDATE_FUNC(astro_status)
{
    MENU_SET_VALUE("%s",last_status);
    MENU_SET_HELP("Astro proof of concept; not tested on physical cameras.");
}
static unsigned int astro_keypress(unsigned int key)
{
    if(shortcut && !busy && lv && !is_movie_mode() && display_idle()) {
        int activate=(shortcut==1 &&
             detect_double_click(key,MODULE_KEY_PRESS_HALFSHUTTER,
                                     MODULE_KEY_UNPRESS_HALFSHUTTER)) ||
                     (shortcut==2 && key==MODULE_KEY_INFO);
        if(activate) {
            start_action(1);
            return shortcut==2 ? 0 : 1; /* consume INFO, keep shutter */
        }
    }
    return 1; /* allow normal Canon handling */
}
static struct menu_entry astro_menu[] = {
    {
        .name="Astro tools",
        .help="Experimental star autofocus and hinted plate solving.",
        .children=(struct menu_entry[]) {
            { .name="Measure star only", .select=start_measure,
              .help="Read-only star detection and sharpness, no motor movement.",
              .icon_type=IT_ACTION },
            { .name="Auto focus star", .select=start_focus,
              .help="5x/10x LiveView, bright centered star, lens AF switch ON.",
              .icon_type=IT_ACTION },
            { .name="Solve LiveView", .select=start_solve,
              .help="Use 1x LiveView; enter approximate RA/Dec and FOV first.",
              .icon_type=IT_ACTION },
            { .name="Solve test PGM", .select=start_test,
              .help="Load B:/ML/ASTRO/TEST.PGM and STARS.BIN.",
              .icon_type=IT_ACTION },
            { .name="Pointing RA hour", .priv=&hint_rah, .min=0,.max=23 },
            { .name="Pointing RA minute", .priv=&hint_ram,.min=0,.max=59 },
            { .name="Pointing Dec deg", .priv=&hint_dec,.min=-85,.max=85 },
            { .name="Frame width (0.1 deg)",.priv=&fov10,.min=0,.max=300,
              .help="0 = auto from lens metadata. Otherwise set manual 1-30 deg." },
            { .name="Manual focus readout",.priv=&focus_overlay,.min=0,.max=1,
              .help="Measure one star each second. No AF motor required." },
            { .name="Motor step size",.priv=&focus_step,.min=1,.max=3 },
            { .name="Focus radius",.priv=&scan_radius,.min=1,.max=6 },
            { .name="Focus shortcut",.priv=&shortcut,.min=0,.max=2,
              .choices=CHOICES("Off","Double half-press","INFO button"),
              .help="OFF by default. Disable conflicting shortcut features." },
            { .name="Last result",.update=astro_status,.icon_type=IT_ACTION },
            MENU_EOL
        },
        .icon_type=IT_SUBMENU
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
    MODULE_CBR(CBR_SECONDS_CLOCK,astro_seconds,0)
MODULE_CBRS_END()
