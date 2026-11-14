"""Runtime tests compile real camera registry, initialization, and raw-button policy.

The physical event codes remain in tests, not user-facing instructions.
"""
from __future__ import annotations

import ctypes as C
import json
from pathlib import Path
import subprocess
import tempfile

from scripts.cameras import load_cameras

ROOT = Path(__file__).resolve().parents[1]
MODULE = ROOT / "modules/astro"

INIT = (MODULE / "astro.c").read_text().split(
    "static unsigned int astro_init(void)", 1
)[1].split("static unsigned int astro_deinit(void)", 1)[0]
RAW = (MODULE / "star_focus.c").read_text().split(
    "unsigned int astro_focus_raw_key(unsigned int event_pointer)", 1
)[1]

STUBS = r"""
#include <stdint.h>
#include <string.h>
#include "camera.h"
#include "shortcut.h"
#define CBR_RET_ERROR 1u
#define ASTRO_ACTION_AUTOFOCUS 3
#define ASTRO_SHORTCUT_OFF 0
#define ASTRO_SHORTCUT_LIGHT 3
static int model,firmware_ok,worker_start_ok=1,starts,menus,requests,readout;
static int lv=1,astro_cfg_focus_shortcut=3;
struct event {int type,param;};
static const char *models[] = { MODEL_NAMES };
static const char *versions[] = { FW_NAMES };
int is_camera(const char *name,const char *firmware){
 if (!firmware_ok || model<1 || model>CAMERA_COUNT) return 0;
 return !strcmp(name,models[model]) && !strcmp(firmware,versions[model]);
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
void reset(int m,int firmware_valid,int shortcut){
 model=m; firmware_ok=firmware_valid; worker_start_ok=1;
 starts=menus=requests=readout=0;astro_cfg_focus_shortcut=shortcut;
}
unsigned init(void){return astro_init();}
int raw(int key){
 struct event e={0,key};
 return (int)astro_focus_raw_key((uintptr_t)&e);
}
int get(int which){
 return which==0?starts:which==1?menus:which==2?requests:
        which==3?readout:astro_cfg_focus_shortcut;
}
int sensor(void){return astro_sensor_width_x10();}
"""

def _build(out: Path) -> C.CDLL:
    cameras = load_cameras()
    names = ",".join(["0"] + [json.dumps(c.model) for c in cameras])
    versions = ",".join(["0"] + [json.dumps(c.firmware) for c in cameras])
    stubs = (STUBS.replace("MODEL_NAMES", names).replace("FW_NAMES", versions)
             .replace("CAMERA_COUNT", str(len(cameras))))
    source = out / "runtime.c"
    source.write_text(
        stubs + "static unsigned int astro_init(void)" + INIT
        + "unsigned int astro_focus_raw_key(uintptr_t event_pointer)" + RAW + WRAPPERS
    )
    (out / "module.h").write_text("int is_camera(const char*,const char*);\n")
    lib = out / "runtime.so"
    subprocess.run(
        ["gcc", "-shared", "-fPIC", "-O2", "-std=c99",
         "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
         "-Wno-unused-parameter", f"-I{MODULE}", f"-I{MODULE/'core'}",
         f"-I{out}", str(source), str(MODULE/"camera.c"),
         str(MODULE/"core/shortcut.c"), "-o", str(lib)], check=True
    )
    dll = C.CDLL(str(lib))
    dll.reset.argtypes = [C.c_int, C.c_int, C.c_int]
    dll.raw.argtypes = [C.c_int]
    dll.raw.restype = C.c_int
    dll.init.restype = C.c_uint
    dll.get.argtypes = [C.c_int]
    dll.sensor.restype = C.c_int
    return dll


def test_camera_profiles_support_and_raw_buttons():
    cameras = load_cameras()
    assert len(cameras) == 9
    assert [c.platform for c in cameras] == [
        "550D.109", "600D.102", "650D.104", "700D.115", "100D.101",
        "60D.111", "1100D.105", "70D.112", "6D.116",
    ]
    with tempfile.TemporaryDirectory(prefix="midnight-camera-runtime-") as d:
        lib = _build(Path(d))
        for index, camera in enumerate(cameras, 1):
            lib.reset(index, 1, 3)
            assert lib.init() == 0, camera.platform
            assert lib.sensor() == camera.sensor_width_x10
            assert lib.get(0) == 1 and lib.get(1) == 1
            assert lib.get(4) == (3 if camera.raw_focus_key >= 0 else 0)
            for key in (0, 4, 7, 8, 15, 29, 32, 36, 107, 111):
                expected = 0 if key == camera.raw_focus_key else 1
                assert lib.raw(key) == expected, (camera.platform, key)
            assert lib.get(2) == (1 if camera.raw_focus_key >= 0 else 0)
            # Explicit INFO and half-shutter modes are preserved during init.
            for shortcut in (1, 2):
                lib.reset(index, 1, shortcut)
                assert lib.init() == 0
                assert lib.get(4) == shortcut
            lib.reset(index, 0, 3)
            assert lib.init() == 1
            assert lib.sensor() == 0
            assert lib.get(0) == lib.get(1) == 0
        lib.reset(0, 1, 3)
        assert lib.init() == 1 and lib.sensor() == 0
