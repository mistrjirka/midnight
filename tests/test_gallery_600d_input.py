"""Real gallery key callback compiled against synthetic Canon key events."""
import ctypes as C
from pathlib import Path
import subprocess
import tempfile

SRC = (Path(__file__).resolve().parents[1] / "modules/astro/playback.c").read_text()
source = SRC[SRC.index("static void close_view(void)"):SRC.index("/* The SET press", SRC.index("static void close_view(void)"))]
source += SRC[SRC.index("unsigned int astro_playback_key(unsigned int key)"):]
MAIN = (Path(__file__).resolve().parents[1] / "modules/astro/astro.c").read_text()
source += MAIN[MAIN.index("static unsigned int astro_keypress(unsigned int key)"):
               MAIN.index("static unsigned int astro_keypress_raw(")]
stubs = r"""
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#define MODULE_KEY_Q 29
#define MODULE_KEY_PRESS_SET 9
#define MODULE_KEY_PRESS_LEFT 16
#define MODULE_KEY_PRESS_RIGHT 15
#define MODULE_KEY_PLAY 24
#define MODULE_KEY_MENU 22
#define MODULE_KEY_TRASH 25
#define ASTRO_ACTION_SOLVE_PLAYBACK 5
#define PROMPT_TIMEOUT_MS 15000
typedef enum { VIEW_IDLE, VIEW_PROMPT, VIEW_RUNNING, VIEW_RESULTS, VIEW_FAILURE } view_state;
static struct {volatile view_state state; uint32_t prompt_time_ms,run_time_ms;} pb;
static int pure_play=1,play_or_qr=1,menu=0,requests,accept_request=1,working,immediate_fail;
static int cleared,progress,prompts,failures,cancelled;
static int result_visible, result_hidden, menu_opened;
static int astro_result_visible(void){return result_visible;}
static void astro_result_hide(void){result_visible=0;result_hidden++;}
static void gui_open_menu(void){menu_opened++;}
static unsigned int astro_focus_key(unsigned int key){(void)key;return 1;}
static uint32_t now=100;
static char last_status[64];
static int is_pure_play_photo_mode(void){return pure_play;}
static int is_play_or_qr_mode(void){return play_or_qr;}
static int gui_menu_shown(void){return menu;}
static uint32_t get_ms_clock(void){return now;}
static int astro_busy(void){return working;}
static void astro_cancel(void){cancelled++;}
static int astro_request(int action){
    if(action!=ASTRO_ACTION_SOLVE_PLAYBACK || !accept_request)return 0;
    requests++;working=1;
    if(immediate_fail)pb.state=VIEW_FAILURE; /* worker finishes before call returns */
    return 1;
}
static void astro_set_status(const char* format,...){
    va_list ap;va_start(ap,format);vsnprintf(last_status,sizeof(last_status),format,ap);va_end(ap);
}
static const char* astro_status(void){return last_status;}
static void clrscr(void){cleared++;}
static void draw_progress(void){progress++;}
static void draw_prompt(void){prompts++;}
static void draw_failure(void){failures++;}
static void draw_results(void){}
static void fail_playback(const char* message){
    astro_set_status("%s",message);pb.state=VIEW_FAILURE;failures++;
}
"""
wrappers = r"""
void reset(void){
    memset(&pb,0,sizeof(pb));pure_play=play_or_qr=accept_request=1;
    menu=requests=working=cleared=progress=prompts=failures=cancelled=immediate_fail=0;
    result_visible=result_hidden=menu_opened=0;
    now=100;last_status[0]=0;
}
int press(int key){return astro_playback_key((unsigned int)key);}
int mainkeypress(int key){return astro_keypress((unsigned int)key);}
void tick(void){astro_playback_tick();}
void set(int id,int value){
    switch(id){
      case 0:pure_play=value;break;case 1:play_or_qr=value;break;
      case 2:menu=value;break;case 3:now=value;break;
      case 4:accept_request=value;break;case 5:working=value;break;
      case 6:pb.state=(view_state)value;break;
      case 7:immediate_fail=value;break;
      case 8:result_visible=value;break;
    }
}
int get(int id){
    switch(id){
      case 0:return pb.state;case 1:return requests;case 2:return cleared;
      case 3:return prompts;case 4:return progress;case 5:return failures;
      case 6:return cancelled;case 7:return result_hidden;
      case 8:return menu_opened;default:return working;
    }
}
const char* status(void){return last_status;}
"""
def test_gallery_q_set():
    with tempfile.TemporaryDirectory(prefix="midnight-gallery-") as t:
        file = Path(t)/"input.c"
        file.write_text(stubs+source+wrappers)
        so=Path(t)/"input.so"
        subprocess.run(["gcc","-shared","-fPIC","-O2","-std=c99","-Wall","-Wextra",
                        "-Werror","-Wno-unused-function","-o",str(so),str(file)],check=True)
        lib=C.CDLL(str(so))
        lib.press.argtypes=[C.c_int];lib.press.restype=C.c_int
        lib.mainkeypress.argtypes=[C.c_int];lib.mainkeypress.restype=C.c_int
        lib.set.argtypes=[C.c_int,C.c_int]
        lib.get.argtypes=[C.c_int];lib.get.restype=C.c_int
        lib.status.restype=C.c_char_p
        lib.reset()
        assert lib.press(29)==0 and lib.get(0)==1
        for key in (0,10,20,5,7,23,34):
            assert lib.press(key)==1 and lib.get(0)==1
        # PlayMain can temporarily stop being the active Canon dialog.
        lib.set(0,0);lib.tick();assert lib.get(0)==1
        assert lib.press(9)==0 and lib.get(1)==1 and lib.get(0)==2
        assert lib.press(9)==0 and lib.get(1)==1
        lib.set(0,1);lib.set(5,0);lib.set(3,1000);lib.tick()
        assert lib.get(0)==4 and lib.get(5)>=1
        lib.reset();lib.press(29);lib.press(29)
        assert lib.get(0)==0 and lib.get(1)==0
        lib.reset();lib.press(29);lib.set(4,0)
        assert lib.press(9)==0 and lib.get(0)==4 and lib.get(1)==0
        lib.reset();lib.press(29);lib.set(3,15101)
        assert lib.press(9)==1 and lib.get(0)==0 and lib.get(1)==0
        lib.reset();lib.press(29);lib.set(1,0);lib.set(0,0);lib.tick()
        assert lib.get(0)==0
        lib.reset();lib.press(29);lib.press(16)
        assert lib.get(0)==0
        lib.reset();lib.press(29);lib.press(9)
        assert lib.press(29)==0 and lib.get(6)==1
        lib.reset();lib.press(29);lib.set(7,1)
        assert lib.press(9)==0 and lib.get(0)==4
        assert lib.get(4)==0, "late progress repaint hid immediate failure"
        # The worker can still be busy just after posting a successful gallery
        # result. Q must hide it, not set the cancellation flag of old work.
        lib.reset();lib.set(6,3);lib.set(5,1)
        assert lib.mainkeypress(29)==0 and lib.get(0)==0
        assert lib.get(6)==0, "result Q was swallowed by generic cancel"
        # While a gallery request is genuinely running, Q still cancels.
        lib.reset();lib.set(6,2);lib.set(5,1)
        assert lib.mainkeypress(29)==0 and lib.get(6)==1
        # Between worker completion and the timer tick, Q closes progress.
        lib.reset();lib.set(6,2);lib.set(5,0)
        assert lib.mainkeypress(29)==0 and lib.get(0)==0
        # For non-gallery operations the generic cancellation path remains.
        lib.reset();lib.set(5,1)
        assert lib.mainkeypress(29)==0 and lib.get(6)==1
        # One-shot file solve result has highest priority of all.
        lib.reset();lib.set(8,1);lib.set(5,1)
        assert lib.mainkeypress(29)==0 and lib.get(6)==0
        assert lib.get(7)==1 and lib.get(8)==1
