#!/usr/bin/env python3
"""Exercise the production gain callback with injected Canon/ML state.

This is a control-policy regression, not a sensor/brightness or camera-speed
measurement. The actual menu/value behavior is also tested in 600D QEMU.
"""
import ctypes as C
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
src = (ROOT / 'astro.c').read_text()
start = src.index('extern int get_digic_iso_gain_photo(void);')
end = src.index('static MENU_UPDATE_FUNC(preview_gain_status)', start)
production = src[start:end]
stubs = r'''
#include <stddef.h>
#include <stdarg.h>
static int photo_gain, movie_gain, movie_mode, busy, overlay_busy, writes;
static struct {int raw_iso;} lens_info;
#define MENU_SELECT_FUNC(n) void n(void *priv, int delta)
static int is_movie_mode(void){return movie_mode;}
static void NotifyBox(int ms,const char *message,...){(void)ms;(void)message;}
static void redraw(void){}
static void DryosDebugMsg(int a,int b,const char *f,...){(void)a;(void)b;(void)f;}
int get_digic_iso_gain_photo(void){return photo_gain;}
int get_digic_iso_gain_movie(void){return movie_gain;}
void display_gain_toggle(void *priv,int delta){
    (void)priv; writes++;
    photo_gain=delta>0 ? photo_gain*2 : photo_gain/2;
}
'''
wrappers = r'''
void reset_state(int photo,int movie,int iso,int mode,int working,int overlay){
    photo_gain=photo;movie_gain=movie;lens_info.raw_iso=iso;
    movie_mode=mode;busy=working;overlay_busy=overlay;writes=0;
}
int step(int delta){change_preview_gain(0,delta);return photo_gain;}
int state(int which){
    switch(which){case 0:return writes;case 1:return movie_gain;
                 case 2:return lens_info.raw_iso;default:return get_preview_gain_ev();}
}
'''
with tempfile.TemporaryDirectory(prefix='midnight-gain-') as tmp:
    code=Path(tmp)/'gain.c';code.write_text(stubs+production+wrappers)
    so=Path(tmp)/'gain.so'
    subprocess.run(['gcc','-shared','-fPIC','-Os','-std=c99','-Wall','-Wextra',
                    '-Werror','-Wno-unused-parameter','-Wl,-z,defs',str(code),'-o',str(so)],check=True)
    lib=C.CDLL(str(so));lib.reset_state.argtypes=[C.c_int]*6
    lib.step.argtypes=[C.c_int];lib.step.restype=C.c_int
    lib.state.argtypes=[C.c_int];lib.state.restype=C.c_int
    lib.reset_state(1024,2048,104,0,0,0)
    assert lib.step(-1)==1024 and lib.state(0)==0
    for ev in range(1,7):
        assert lib.step(1)==1024<<ev and lib.state(3)==ev
        assert lib.state(1)==2048 and lib.state(2)==104
    assert lib.step(1)==65536 and lib.state(0)==6
    for ev in range(5,-1,-1):assert lib.step(-1)==1024<<ev
    assert lib.step(0)==1024 and lib.step(-1)==1024 and lib.state(0)==12
    for mode,work,overlay in [(1,0,0),(0,1,0),(0,0,1)]:
        lib.reset_state(4096,2048,104,mode,work,overlay)
        assert lib.step(1)==4096 and lib.state(0)==0
        assert lib.state(1)==2048 and lib.state(2)==104
    lib.reset_state(4096,2048,104,0,0,0)
    assert lib.state(3)==2 and lib.step(9)==8192 and lib.state(0)==1
print('PASS: gain limits, one-stop stepping, shared state, movie/busy guards; ISO/movie gain unchanged')
