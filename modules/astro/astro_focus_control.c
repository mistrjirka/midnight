#include "astro_focus_control.h"
#include "astro_core.h"
#include <string.h>

const char *astro_focus_status_text(enum astro_focus_status s)
{
    switch(s) {
        case ASTRO_FOCUS_OK: return "Focus complete";
        case ASTRO_FOCUS_NO_LENS: return "AF lens/LV unavailable";
        case ASTRO_FOCUS_NO_FRAME: return "LiveView frame unavailable";
        case ASTRO_FOCUS_NO_STAR: return "No confirmed star, motor not used";
        case ASTRO_FOCUS_STAR_LOST: return "Star lost, motor stopped";
        case ASTRO_FOCUS_NO_FEEDBACK: return "Lens position unavailable";
        case ASTRO_FOCUS_MOTOR_FAILED: return "Lens command failed; stopped";
        case ASTRO_FOCUS_NOT_MOVING: return "Motor unresponsive; stopped";
        case ASTRO_FOCUS_EDGE: return "Focus optimum at scan edge";
        case ASTRO_FOCUS_WEAK_CHANGE: return "No measurable focus change";
        default: return "Focus aborted";
    }
}
/* Every motor step is followed by confirmation of actual movement.
 * For lenses without reliable position feedback, stop rather than
 * sending further commands or claiming that focus succeeded. */
static enum astro_focus_status step_checked(const astro_focus_io *io,int dir,int speed,
                                             int *actual,astro_focus_result *r)
{
    int before,after;
    if (!io->ready(io->context)) return ASTRO_FOCUS_NO_LENS;
    if (!io->position(io->context,&before)) return ASTRO_FOCUS_NO_FEEDBACK;
    if (!io->step(io->context,dir,speed)) return ASTRO_FOCUS_MOTOR_FAILED;
    r->commands++;
    if (!io->ready(io->context)) return ASTRO_FOCUS_NO_LENS;
    if (!io->position(io->context,&after)) return ASTRO_FOCUS_NO_FEEDBACK;
    if (after == before) {
        io->sleep_ms(io->context,160);
        if (!io->ready(io->context)) return ASTRO_FOCUS_NO_LENS;
        if (!io->position(io->context,&after)) return ASTRO_FOCUS_NO_FEEDBACK;
        if (after == before) return ASTRO_FOCUS_NOT_MOVING;
    }
    *actual=after;
    return ASTRO_FOCUS_OK;
}
static enum astro_focus_status measure_locked(const astro_focus_io *io,
                                               uint8_t *pixels,int w,int h,
                                               int sx,int sy,int *metric)
{
    int x=sx,y=sy;
    if(!io->ready(io->context)) return ASTRO_FOCUS_NO_LENS;
    if(!io->frame(io->context,pixels,w,h))return ASTRO_FOCUS_NO_FRAME;
    *metric=astro_focus_metric_locked(pixels,w,h,w,&x,&y,7);
    if(*metric<0)return ASTRO_FOCUS_STAR_LOST;
    return ASTRO_FOCUS_OK;
}
int astro_run_autofocus(const astro_focus_io *io,uint8_t *pixels,int w,int h,
                       int radius,int speed,astro_focus_result *r)
{
    int x=w/2,y=h/2, first,second,pos=0,best_pos=0,best=0,bad=0,initial=0,actual=0;
    enum astro_focus_status err=ASTRO_FOCUS_ABORTED;
    if(!r) return ASTRO_FOCUS_ABORTED;
    memset(r,0,sizeof(*r));
    r->status=ASTRO_FOCUS_ABORTED;
    if(!io||!pixels||w<32||h<32||w>1024||h>1024 ||
       radius<1||radius>6||speed<1||speed>3||
       !io->ready||!io->position||!io->step||!io->frame||!io->sleep_ms)
        goto finished;
    if(!io->ready(io->context)){err=ASTRO_FOCUS_NO_LENS;goto finished;}
    if(!io->position(io->context,&initial)){err=ASTRO_FOCUS_NO_FEEDBACK;goto finished;}
    r->initial_position=initial;
    r->final_position=initial;
    if(!io->frame(io->context,pixels,w,h)){err=ASTRO_FOCUS_NO_FRAME;goto finished;}
    first=astro_focus_metric(pixels,w,h,w,&x,&y);
    if(first<0){err=ASTRO_FOCUS_NO_STAR;goto finished;}
    /* A second independent frame must contain the SAME bright star.
     * This rejects transient sensor noise and single-frame artifacts. */
    io->sleep_ms(io->context,100);
    err=measure_locked(io,pixels,w,h,x,y,&second);
    if(err != ASTRO_FOCUS_OK) {if(err==ASTRO_FOCUS_STAR_LOST)err=ASTRO_FOCUS_NO_STAR;goto finished;}
    int base=second;
    best=second;
    /* Sample each side of focus with exactly one verified motor step at a time. */
    for(int d=0;d<radius;d++) {
        err=step_checked(io,-1,speed,&actual,r);
        if(err!=ASTRO_FOCUS_OK)goto finished;
        pos--;
        r->final_position=actual;
    }
    for(int sample=-radius;sample<=radius;sample++) {
        if(sample>pos) {
            err=step_checked(io,+1,speed,&actual,r);
            if(err!=ASTRO_FOCUS_OK)goto finished;
            pos++;
            r->final_position=actual;
        }
        io->sleep_ms(io->context,200);
        int current;
        err=measure_locked(io,pixels,w,h,x,y,&current);
        if(err!=ASTRO_FOCUS_OK) {
            if(err==ASTRO_FOCUS_STAR_LOST)bad++;
            if(bad>1 || err!=ASTRO_FOCUS_STAR_LOST)goto finished;
            continue;
        }
        bad=0;
        r->samples++;
        if(current<best) {best=current;best_pos=pos;}
    }
    r->best_offset=best_pos;
    r->best_metric=best;
    /* Restore known baseline when measured contrast did not change.
     * Do not label an edge solution as autofocus success. */
    if(best_pos==-radius || best_pos==radius)
        err=ASTRO_FOCUS_EDGE;
    else if(base-best<2)
        err=ASTRO_FOCUS_WEAK_CHANGE;
    else
        err=ASTRO_FOCUS_OK;
    int target=(err==ASTRO_FOCUS_OK)?best_pos:0;
    while(pos!=target) {
        enum astro_focus_status moved=step_checked(io,pos<target?1:-1,speed,&actual,r);
        if(moved!=ASTRO_FOCUS_OK) {err=moved;goto finished;}
        pos+=(pos<target?1:-1);
        r->final_position=actual;
    }
finished:
    r->status=err;
    return err;
}
