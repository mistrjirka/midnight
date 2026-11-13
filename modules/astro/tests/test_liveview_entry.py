#!/usr/bin/env python3
"""Regression: Live View preflight identifies the actual failure before solving."""
import ctypes as C
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
src=(root/"astro.c").read_text()
a=src.index("static MENU_SELECT_FUNC(start_solve)")
b=src.index("static MENU_SELECT_FUNC(start_test)",a)
production=src[a:b]
stubs=r"""
#include <stdio.h>
#include <string.h>
static int lv, movie, playback, lv_dispsize, launches, notified;
static char last_status[64];
#define MENU_SELECT_FUNC(n) void n(void *unused)
static int is_movie_mode(void){return movie;}
static int is_play_or_qr_mode(void){return playback;}
static void start_action(int action){if(action==2)launches++;}
static void NotifyBox(int delay, const char *fmt, ...){(void)delay;(void)fmt;notified++;}
"""
wrapper=r"""
void reset(int live,int mov,int play,int zoom){
    lv=live;movie=mov;playback=play;lv_dispsize=zoom;
    launches=notified=0;last_status[0]=0;
}
void run(void){start_solve(NULL);}
int value(int code){return code==0?launches:notified;}
const char *message(void){return last_status;}
"""
with tempfile.TemporaryDirectory(prefix="midnight-liveview-") as tmp:
    path=Path(tmp)/"liveview.c";path.write_text(stubs+production+wrapper)
    libfile=Path(tmp)/"liveview.so"
    subprocess.run(["gcc","-shared","-fPIC","-std=c99","-Os","-Wall","-Wextra",
                    "-Werror","-Wno-unused-function","-Wno-unused-parameter",str(path),"-o",str(libfile)],check=True)
    lib=C.CDLL(str(libfile))
    lib.reset.argtypes=[C.c_int]*4
    lib.value.argtypes=[C.c_int];lib.value.restype=C.c_int
    lib.message.restype=C.c_char_p
    cases=[
        ((0,0,0,1),b"Turn on 1x photo LiveView before solving"),
        ((1,1,0,1),b"Switch to photo LiveView before solving"),
        ((1,0,1,1),b"Close photo playback before LiveView solve"),
        ((1,0,0,5),b"Use 1x LiveView (not 5x or 10x)"),
    ]
    for state,message in cases:
        lib.reset(*state);lib.run()
        assert lib.value(0)==0 and lib.value(1)==1,(state,lib.value(0),lib.value(1))
        assert lib.message()==message,(state,lib.message())
    lib.reset(1,0,0,1);lib.run()
    assert lib.value(0)==1 and lib.value(1)==0
    print("PASS: LiveView inactive, movie, playback, zoom and valid-action state")
