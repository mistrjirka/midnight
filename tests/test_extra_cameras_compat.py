"""New-camera compatibility checks against the actual compiled dispatch code.

These tests do not claim new-camera firmware boot or touch UI are camera-validated.
"""
from __future__ import annotations

import ctypes as C
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MODULE = ROOT / "modules" / "astro"

INIT_SOURCE = (MODULE / "astro.c").read_text().split(
    "static unsigned int astro_init(void)", 1
)[1].split("static unsigned int astro_deinit(void)", 1)[0]
RAW_SOURCE = (MODULE / "star_focus.c").read_text().split(
    "unsigned int astro_focus_raw_key(unsigned int event_pointer)", 1
)[1]

STUBS = r"""
#include <stdint.h>
#include <string.h>
#include "shortcut.h"
#define CBR_RET_ERROR 1u
#define ASTRO_ACTION_AUTOFOCUS 3
#define ASTRO_SHORTCUT_OFF 0
#define ASTRO_SHORTCUT_LIGHT 3
static int model,firmware_ok,worker_start_ok=1,starts,menus,requests,readout;
static int lv=1;
static int astro_cfg_focus_shortcut=3;
struct event {int type,param;};
static int is_camera(const char *name,const char *firmware){
 if (!firmware_ok) return 0;
 if (model==1 && !strcmp(name,"600D") && !strcmp(firmware,"1.0.2")) return 1;
 if (model==2 && !strcmp(name,"6D") && !strcmp(firmware,"1.1.6")) return 1;
 if (model==3 && !strcmp(name,"700D") && !strcmp(firmware,"1.1.5")) return 1;
 if (model==4 && !strcmp(name,"650D") && !strcmp(firmware,"1.0.4")) return 1;
 if (model==5 && !strcmp(name,"100D") && !strcmp(firmware,"1.0.1")) return 1;
 if (model==6 && !strcmp(name,"60D") && !strcmp(firmware,"1.1.1")) return 1;
 if (model==7 && !strcmp(name,"1100D") && !strcmp(firmware,"1.0.5")) return 1;
 if (model==8 && !strcmp(name,"70D") && !strcmp(firmware,"1.1.2")) return 1;
 return 0;
}
static int astro_worker_start(void){starts++;return worker_start_ok;}
static void astro_menu_add(void){menus++;}
static int is_movie_mode(void){return 0;}
static int display_idle(void){return 1;}
static int gui_menu_shown(void){return 0;}
static int astro_busy(void){return 0;}
static int astro_focus_lens_ready(void){return 1;}
static void astro_request(int action){if(action==ASTRO_ACTION_AUTOFOCUS)requests++;}
static void toggle_readout(void){readout++;}
"""
WRAPPERS = r"""
void reset(int m,int firmware_valid){
 model=m;firmware_ok=firmware_valid;worker_start_ok=1;
 starts=menus=requests=readout=0;astro_cfg_focus_shortcut=3;
}
unsigned init(void){return astro_init();}
int raw(int key){
 struct event e={0,key};
 return (int)astro_focus_raw_key((uintptr_t)&e);
}
int get(int which){return which==0?starts:which==1?menus:which==2?requests:which==4?astro_cfg_focus_shortcut:readout;}
"""

def _build(out: Path) -> C.CDLL:
    source = out / "extra_cameras.c"
    source.write_text(
        STUBS + "static unsigned int astro_init(void)" + INIT_SOURCE
        + "unsigned int astro_focus_raw_key(uintptr_t event_pointer)"
        + RAW_SOURCE + WRAPPERS
    )
    lib = out / "libextra_cameras.so"
    subprocess.run(
        ["gcc", "-std=c99", "-O2", "-shared", "-fPIC", "-Wall", "-Wextra",
         "-Werror", "-Wno-unused-function", "-Wno-unused-parameter",
         f"-I{MODULE / 'core'}", "-o", str(lib), str(source),
         str(MODULE / "core" / "shortcut.c")],
        check=True,
    )
    dll = C.CDLL(str(lib))
    dll.reset.argtypes = [C.c_int, C.c_int]
    dll.raw.argtypes = [C.c_int]
    dll.raw.restype = C.c_int
    dll.init.restype = C.c_uint
    dll.get.argtypes = [C.c_int]
    return dll

def test_camera_firmware_gate_and_special_button():
    with tempfile.TemporaryDirectory(prefix="midnight-extra-") as d:
        lib = _build(Path(d))
        for model in range(1, 9):
            lib.reset(model, 1)
            assert lib.init() == 0
            assert lib.get(0) == 1 and lib.get(1) == 1
            assert lib.get(4) == (3 if model in (1, 2) else 0)
        for model in range(0, 9):
            lib.reset(model, 0)
            assert lib.init() == 1, "unsupported firmware must refuse to load"
            assert lib.get(0) == 0 and lib.get(1) == 0
        lib.reset(0, 1)
        assert lib.init() == 1

        # All newly supported bodies must pass arbitrary raw events.
        # Keep physical focus and touch inputs untouched by default.
        for model in (3, 4, 5, 6, 7, 8):
            lib.reset(model, 1)
            for key in (0x20, 0x08, 0x0F, 0x1D, 0x24, 0x6B, 0x72, 0x6F, 0x76, 4, 7):
                assert lib.raw(key) == 1, (model, key)
            assert lib.get(2) == 0 and lib.get(3) == 0

        # Existing supported buttons must keep their exact behavior.
        lib.reset(1, 1)
        assert lib.raw(0x20) == 1
        assert lib.raw(0x08) == 0 and lib.get(2) == 1

        lib.reset(2, 1)
        assert lib.raw(0x08) == 1
        assert lib.raw(0x20) == 0 and lib.get(2) == 1
