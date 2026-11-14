"""Real 600D/700D/6D sensor selection and Live View controls, without hardware."""
import ctypes as C
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
MENU=(ROOT/"modules/astro/menu.c").read_text()
SETTLE=(ROOT/"modules/astro/solve_actions.c").read_text()
STATE=(ROOT/"modules/astro/state.c").read_text()
start=MENU[MENU.index("static MENU_SELECT_FUNC(start_solve_liveview)"):
           MENU.index("static MENU_SELECT_FUNC(start_solve_file)")]
settle=SETTLE[SETTLE.index("static int settle(void)"):SETTLE.index("static int capture(",SETTLE.index("static int settle(void)"))]
specs=STATE[STATE.index("int astro_sensor_width_x10(void)"):STATE.index("int astro_hint_focal_mm(void)")]
STUBS=r"""
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#define ASTRO_600D_SENSOR_WIDTH_X10 223
#define ASTRO_700D_SENSOR_WIDTH_X10 223
#define ASTRO_6D_SENSOR_WIDTH_X10 358
#define ASTRO_ACTION_SOLVE_LIVEVIEW 2
#define SETTLE_MS 3000
#define SETTLE_STEP_MS 100
#define MENU_SELECT_FUNC(n) void n(void *unused)
static int model,lv,movie,zoom,playback,requests,notifications,cancel;
static int change_after=-1,elapsed;
static char last_status[128];
static int is_camera(const char *name,const char *firmware){
 if(!strcmp(name,"600D")&&!strcmp(firmware,"1.0.2"))return model==1;
 if(!strcmp(name,"6D")&&!strcmp(firmware,"1.1.6"))return model==2;
 if(!strcmp(name,"700D")&&!strcmp(firmware,"1.1.5"))return model==3;
 return 0;
}
static int is_movie_mode(void){return movie;}
static int astro_liveview_ready(void){return lv&&!movie&&zoom==1;}
static int is_play_or_qr_mode(void){return playback;}
static int astro_cancel_requested(void){return cancel;}
#define lv_dispsize zoom
static void astro_set_status(const char *fmt,...){
 va_list args;va_start(args,fmt);vsnprintf(last_status,sizeof(last_status),fmt,args);va_end(args);
}
static const char *astro_status(void){return last_status;}
static void start(int action,int close_menu){(void)close_menu;if(action==2)requests++;}
static void NotifyBox(int duration,const char* fmt,...){(void)duration;(void)fmt;notifications++;}
static void astro_log(const char *fmt,...){(void)fmt;}
static void astro_log_progress(const char *fmt,...){(void)fmt;}
static void msleep(int time){
 elapsed+=time;
 if(change_after>=0&&elapsed>=change_after)lv=0;
}
"""
WRAPPERS=r"""
void reset(int m,int live,int video,int size,int play){
 model=m;lv=live;movie=video;zoom=size;playback=play;
 requests=notifications=cancel=elapsed=0;change_after=-1;last_status[0]=0;
}
int sensor(void){return astro_sensor_width_x10();}
void click(void){start_solve_liveview(NULL);}
int stabilized(void){return settle();}
void change_at(int ms){change_after=ms;}
void set_cancel(int x){cancel=x;}
int value(int k){return k==0?requests:k==1?notifications:elapsed;}
const char *status(void){return last_status;}
"""
def test_camera_liveview():
    with tempfile.TemporaryDirectory(prefix="midnight-lv-") as tmp:
        src=Path(tmp)/"lv.c";src.write_text(STUBS+specs+start+settle+WRAPPERS)
        out=Path(tmp)/"lv.so"
        subprocess.run(["gcc","-shared","-fPIC","-O2","-std=c99",
                        "-Wall","-Wextra","-Werror","-Wno-unused-function",
                        "-Wno-unused-parameter",str(src),"-o",str(out)],check=True)
        lib=C.CDLL(str(out))
        lib.reset.argtypes=[C.c_int]*5
        lib.sensor.restype=C.c_int
        lib.value.argtypes=[C.c_int];lib.value.restype=C.c_int
        lib.change_at.argtypes=[C.c_int];lib.set_cancel.argtypes=[C.c_int]
        lib.status.restype=C.c_char_p
        for model,width in [(0,0),(1,223),(2,358),(3,223)]:
            lib.reset(model,1,0,1,0)
            assert lib.sensor()==width
        for setup,message in [
            ((1,0,0,1,0),b"Turn on 1x photo Live View first"),
            ((1,1,1,1,0),b"Use photo mode, not movie mode"),
            ((1,1,0,5,0),b"Use 1x Live View, not 5x/10x"),
            ((1,1,0,1,1),b"Close playback before Live View solve"),
            ((3,1,0,10,0),b"Use 1x Live View, not 5x/10x"),
            ((3,0,0,1,0),b"Turn on 1x photo Live View first"),
        ]:
            lib.reset(*setup);lib.click()
            assert lib.value(0)==0 and lib.value(1)==1 and lib.status()==message
        for model in (1,2,3):
            lib.reset(model,1,0,1,0);lib.click()
            assert lib.value(0)==1 and lib.value(1)==0
            assert lib.stabilized()==1 and lib.value(2)==3000
        lib.reset(1,1,0,1,0);lib.change_at(100)
        assert lib.stabilized()==0 and b"switched off" in lib.status()
        lib.reset(1,1,0,1,0);lib.set_cancel(1)
        assert lib.stabilized()==0 and b"cancelled by Q" in lib.status()
