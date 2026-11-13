"""The production Canon playback capture from a synthetic 720x480 UYVY image."""
import ctypes as C
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
SOURCE=(ROOT/"modules/astro/capture.c").read_text()
PRODUCTION=SOURCE[SOURCE.index("static struct vram_info *display_buffer(void)"):
                  SOURCE.index("const char *astro_test_file_name",SOURCE.index("int astro_capture_display"))]
STUBS=r"""
#include <stdint.h>
#include <stddef.h>
#define ASTRO_IMAGE_WIDTH 360
#define ASTRO_IMAGE_HEIGHT 240
#define MAX_BUFFER_WIDTH 4096
#define MAX_BUFFER_HEIGHT 2160
struct vram_info {uint8_t *vram; int width,pitch,height;};
static uint8_t fb[720*480*2],output[360*240];
static struct vram_info v = {fb,720,1440,480};
static int lv=0,play=1;
static int is_pure_play_photo_mode(void){return play;}
static struct vram_info *get_yuv422_vram(void){return &v;}
"""
WRAP=r"""
void reset(void){v.vram=fb;v.width=720;v.pitch=1440;v.height=480;lv=0;play=1;}
uint8_t* pixels(void){return fb;}
uint8_t* result(void){return output;}
int capture(void){return astro_capture_display(output);}
void settings(int key,int n){
 switch(key){
 case 0:v.width=n;break;
 case 1:v.height=n;break;
 case 2:v.pitch=n;break;
 case 3:lv=n;break;
 case 4:play=n;break;
 case 5:v.vram=n?fb:NULL;break;
 }
}
"""
def test_600d_playback_image():
    with tempfile.TemporaryDirectory() as t:
        file=Path(t)/"capture.c";file.write_text(STUBS+PRODUCTION+WRAP)
        so=Path(t)/"capture.so"
        subprocess.run(["gcc","-shared","-fPIC","-O2","-std=c99","-Wall","-Wextra","-Werror",
                        "-Wno-unused-function","-o",str(so),str(file)],check=True)
        lib=C.CDLL(str(so))
        lib.pixels.restype=C.POINTER(C.c_uint8)
        lib.result.restype=C.POINTER(C.c_uint8)
        lib.settings.argtypes=[C.c_int,C.c_int]
        lib.reset()
        file=(ROOT/"tests/data/TEST.PGM").read_bytes()
        assert file[:15]==b"P5\n360 240\n255\n"
        sample=file[15:]
        pixels=lib.pixels()
        for y in range(240):
            for x in range(360):
                value=sample[y*360+x]
                for dy in (0,1):
                    for dx in (0,1):
                        pixels[((2*y+dy)*720+(2*x+dx))*2+1]=value
        assert lib.capture()==1
        output=C.string_at(lib.result(),len(sample))
        assert output==sample,"captured preview differs from simulated Canon playback"
        # One noisy source pixel must not dominate the analysis pixel.
        pixels[1]=255
        pixels[3]=0
        pixels[1441]=0
        pixels[1443]=0
        assert lib.capture()==1
        assert lib.result()[0]==63
        lib.settings(2,100)
        assert lib.capture()==0
        lib.reset()
        lib.settings(4,0)
        assert lib.capture()==0
        lib.settings(3,1)
        assert lib.capture()==1
        lib.settings(5,0)
        assert lib.capture()==0
