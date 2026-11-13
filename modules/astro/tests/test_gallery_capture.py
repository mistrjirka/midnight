#!/usr/bin/env python3
"""Exercise the real gallery YUV422 sampling code with a camera-sized buffer."""
import ctypes as C
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
source=(root/'astro.c').read_text()
start=source.index('static int capture_liveview(void)')
end=source.index('/* File reads may complete partially',start)
production=source[start:end]
stubs=r'''
#include <stdint.h>
#include <stddef.h>
#define IM_W 360
#define IM_H 240
static uint8_t* image;
static uint8_t framebuffer[720*480*2];
static int lv, play=1;
struct vram_info {uint8_t *vram; int width,pitch,height;};
static struct vram_info v={framebuffer,720,720*2,480};
static int is_pure_play_photo_mode(void){return play;}
static struct vram_info *get_yuv422_vram(void){return &v;}
'''
wrapper=r'''
void reset_capture(void){image=NULL; lv=0;play=1;v.vram=framebuffer;v.width=720;v.height=480;v.pitch=1440;}
void set_input(int x,int y,int brightness) {framebuffer[(y*v.width+x)*2+1]=brightness;}
int input_sample(int x,int y){return framebuffer[(y*v.width+x)*2+1];}
void enable_output(void){static uint8_t result[IM_W*IM_H];image=result;}
int pixel(int x,int y){return image[y*IM_W+x];}
int get_capture(void){return capture_liveview();}
uint32_t get_signature(void){return gallery_photo_signature();}
void set_play(int enabled){play=enabled;}
void set_vwidth(int width){v.width=width;}
void set_vpitch(int pitch){v.pitch=pitch;}
'''
with tempfile.TemporaryDirectory(prefix='midnight-yuv-') as temp:
    path=Path(temp)/'capture.c';path.write_text(stubs+production+wrapper)
    out=Path(temp)/'capture.so'
    subprocess.run(['gcc','-shared','-fPIC','-Os','-std=c99','-Wall','-Wextra','-Werror',str(path),'-o',str(out)],check=True)
    lib=C.CDLL(str(out))
    lib.set_input.argtypes=[C.c_int]*3
    lib.input_sample.argtypes=[C.c_int]*2;lib.input_sample.restype=C.c_int
    lib.pixel.argtypes=[C.c_int]*2;lib.pixel.restype=C.c_int
    lib.set_play.argtypes=[C.c_int];lib.set_vwidth.argtypes=[C.c_int];lib.set_vpitch.argtypes=[C.c_int]
    lib.get_signature.restype=C.c_uint32
    lib.reset_capture()
    assert not lib.get_capture() # no 86 KB target
    lib.enable_output()
    # Sample the known 360x240 grayscale pattern into 720x480 UYVY.
    data=(root.parents[1]/'.astro-data'/'TEST.PGM').read_bytes()
    assert data[:15]==b'P5\n360 240\n255\n'
    pixels=data[15:]
    for y in range(240):
        for x in range(360):
            # Grayscale image is upscaled 2x onto Canon UYVY playback pixels.
            value=pixels[y*360+x]
            lib.set_input(2*x,2*y,value)
    assert lib.get_capture()
    for x,y in [(0,0),(2,5),(32,59),(125,88),(192,120),(359,239)]:
        assert lib.pixel(x,y)==pixels[y*360+x],f'luma mismatch at {(x,y)}'
    sig=lib.get_signature()
    assert sig!=0 and lib.get_signature()==sig
    lib.set_input(2*13,2*14,pixels[14*360+13]^0x55)
    assert lib.get_signature()!=sig, 'changed playback image not detected'
    lib.set_play(0)
    assert lib.get_capture()==0 and lib.get_signature()==0
    lib.set_play(1)
    lib.set_vpitch(100)
    assert lib.get_capture()==0
    print('PASS: camera-size UYVY framebuffer -> 360x240 grayscale, image-change hash and invalid buffer guards')
