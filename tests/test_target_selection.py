"""No guidance is rendered until the user selects a target.

Compile the production target lookup and overlay code with in-memory catalogues.
"""
from __future__ import annotations

import ctypes as C
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SRC = (ROOT / "modules/astro/overlay.c").read_text()
START = SRC.index("static struct {")
END = SRC.index("void astro_target_status(")
LOOKUP_AND_OVERLAY = SRC[START:END]
STUBS = r"""
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#define ASTRO_NAME_BYTES 24
#define ASTRO_MAG_UNKNOWN 32767
#define ASTRO_CAT_NONE 0
#define ASTRO_CAT_M 1
#define ASTRO_ACTION_LOOKUP_TARGET 5
#define ASTRO_IMAGE_WIDTH 360
#define ASTRO_IMAGE_HEIGHT 240
#define STAR_LABEL_MAG10 45
typedef struct {int mag10,type,major_axis;} astro_dso;
typedef struct {int valid;astro_dso object;} astro_target;
typedef struct {int unused;} astro_sky_files;
typedef struct {astro_sky_files files;} astro_catalogue_files;
typedef struct {int dummy;} astro_scene;
typedef struct {struct {int wcs;} sky;} astro_solve_result;
typedef struct {int stars,dso,constellations,star_mag10,dso_mag10;} astro_overlay_opts;
static int astro_cfg_target_catalogue=0,astro_cfg_target_number=1,astro_cfg_mark_target=1;
static int astro_cfg_overlay_stars=1,astro_cfg_overlay_dso=1;
static int astro_cfg_overlay_constellations=1,astro_cfg_overlay_dso_mag=9;
static int opens,lookups,queues,draw_guidance;
static void astro_request(int action){if(action==ASTRO_ACTION_LOOKUP_TARGET)queues++;}
static void astro_catalogue_open(astro_catalogue_files *c){(void)c;opens++;}
static void astro_catalogue_close(astro_catalogue_files *c){(void)c;}
static int astro_dso_lookup(const astro_sky_files *f,int catalogue,int number,astro_target *out){
 (void)f;(void)catalogue;(void)number;lookups++;
 memset(out,0,sizeof(*out));out->valid=1;
 out->object.mag10=50;out->object.type=1;out->object.major_axis=20;
 return 1;
}
static void astro_dso_common_name(const astro_sky_files *f,const astro_dso *o,char *out,int size){
 (void)f;(void)o;(void)size;out[0]=0;
}
static const char *astro_dso_type_name(int kind){(void)kind;return "galaxy";}
static int astro_build_scene(const int *wcs,int width,int height,const astro_overlay_opts *opts,
                             const astro_target *target,const astro_sky_files *files,astro_scene *out){
 (void)wcs;(void)width;(void)height;(void)opts;(void)files;(void)out;
 draw_guidance=(target && target->valid);
 return 0;
}
#define DryosDebugMsg(a,b,c) ((void)0)
"""
WRAPPERS = r"""
void select_target(int catalogue,int number,int mark){
 astro_cfg_target_catalogue=catalogue;
 astro_cfg_target_number=number;
 astro_cfg_mark_target=mark;
}
int current(void){return astro_target_current();}
void render(void){
 astro_solve_result r={0};astro_scene s={0};
 astro_build_overlay(&r,&s);
}
int value(int what){
 return what==0?opens:what==1?lookups:what==2?draw_guidance:
        what==3?astro_target_get()->valid:queues;
}
const char *info(void){return astro_target_info();}
"""


def test_guidance_requires_selected_target():
    state = (ROOT / "modules/astro/state.c").read_text()
    menu = (ROOT / "modules/astro/menu.c").read_text()
    assert 'CONFIG_INT("astro.tgt_cat", astro_cfg_target_catalogue, ASTRO_CAT_NONE);' in state
    assert 'CONFIG_INT("astro.tgt_num", astro_cfg_target_number, 1);' in state
    assert '.choices = CHOICES("None", "Messier (M)"' in menu

    with tempfile.TemporaryDirectory(prefix="midnight-target-") as tmp:
        source = Path(tmp) / "target.c"
        source.write_text(STUBS + LOOKUP_AND_OVERLAY + WRAPPERS)
        library = Path(tmp) / "target.so"
        subprocess.run(
            ["gcc", "-shared", "-fPIC", "-O2", "-std=c99",
             "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
             "-o", str(library), str(source)], check=True
        )
        dll = C.CDLL(str(library))
        dll.select_target.argtypes = [C.c_int] * 3
        dll.value.argtypes = [C.c_int]
        dll.info.restype = C.c_char_p
        assert dll.current() == 1
        dll.render()
        assert [dll.value(i) for i in range(4)] == [1, 0, 0, 0]
        assert dll.info() == b"None"

        dll.select_target(1, 42, 1)
        assert dll.current() == 0
        dll.render()
        assert dll.value(1) == 1 and dll.value(2) == 1 and dll.value(3) == 1

        dll.select_target(0, 42, 1)
        assert dll.current() == 1
        dll.render()
        assert dll.value(1) == 1, "Clearing selection must not read the catalogue"
        assert dll.value(2) == dll.value(3) == 0, "Stale target still guides"
        assert dll.info() == b"None"

        dll.select_target(1, 31, 1)
        dll.render()
        assert dll.value(1) == 2 and dll.value(2) == 1
        dll.select_target(1, 31, 0)
        dll.render()
        assert dll.value(2) == 0, "Mark target Off should hide guidance"



def test_guide_menu_requires_target():
    menu_src = (ROOT / "modules/astro/menu_handlers.c").read_text()
    helper = menu_src[
        menu_src.index("static void start(astro_action action, int close_menu)"):
        menu_src.index("MENU_SELECT_FUNC(astro_menu_open_intervalometer)")
    ]
    callback = menu_src[
        menu_src.index("MENU_SELECT_FUNC(astro_menu_start_guide)"):
        menu_src.index("MENU_SELECT_FUNC(astro_menu_start_polar_first)")
    ]
    stubs = r"""
#include <stdio.h>
#include <stdarg.h>
#define ASTRO_CAT_NONE 0
#define ASTRO_ACTION_GUIDE 4
#define MENU_SELECT_FUNC(n) void n(void *priv, int delta)
typedef int astro_action;
static int astro_cfg_target_catalogue, queued, closed, notified;
static char status[80];
static int astro_request(int action){if(action==4){queued++;return 1;}return 0;}
static void astro_set_status(const char *fmt,...){
 va_list v;va_start(v,fmt);vsnprintf(status,sizeof(status),fmt,v);va_end(v);
}
static void NotifyBox(int ms,const char *fmt,...){(void)ms;(void)fmt;notified++;}
static void gui_stop_menu(void){closed++;}
"""
    wrappers = r"""
void choose(int catalog){astro_cfg_target_catalogue=catalog;}
void run(void){astro_menu_start_guide(0,0);}
int metric(int which){return which==0?queued:which==1?closed:notified;}
const char *reason(void){return status;}
"""
    with tempfile.TemporaryDirectory(prefix="midnight-guide-menu-") as tmp:
        source = Path(tmp) / "guide.c"
        source.write_text(stubs + helper + callback + wrappers)
        so = Path(tmp) / "guide.so"
        subprocess.run(
            ["gcc", "-shared", "-fPIC", "-O2", "-std=c99",
             "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
             "-o", str(so), str(source)], check=True
        )
        lib = C.CDLL(str(so))
        lib.choose.argtypes = [C.c_int]
        lib.metric.argtypes = [C.c_int]
        lib.reason.restype = C.c_char_p
        lib.choose(0)
        lib.run()
        assert [lib.metric(i) for i in range(3)] == [0, 0, 1]
        assert lib.reason() == b"Choose a target first"
        lib.choose(1)
        lib.run()
        assert [lib.metric(i) for i in range(3)] == [1, 1, 1]
