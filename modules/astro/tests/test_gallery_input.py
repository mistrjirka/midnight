#!/usr/bin/env python3
"""Regression test for Magic Lantern gallery Q -> SET with intervening events."""
import ctypes as C
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
source=(root/'astro.c').read_text()
start=source.index('static unsigned int astro_gallery_keypress(unsigned int key)')
end=source.index('static unsigned int astro_keypress(unsigned int key)',start)
production=source[start:end]
stubs=r'''
#include <string.h>
#include <stdint.h>
static int gallery_prompt, gallery_results, gallery_failed, busy;
static uint32_t gallery_prompt_time, clock_ms;
static int in_play, in_menu, in_play_or_qr, launched, clears, prompts, last_action;
static char last_status[64];
#define MODULE_KEY_PRESS_SET 9
#define MODULE_KEY_UNPRESS_SET 10
#define MODULE_KEY_PRESS_LEFT 16
#define MODULE_KEY_PRESS_RIGHT 15
#define MODULE_KEY_PLAY 24
#define MODULE_KEY_MENU 22
#define MODULE_KEY_TRASH 25
#define MODULE_KEY_Q 29
static int is_pure_play_photo_mode(void){return in_play;}
static int gui_menu_shown(void){return in_menu;}
static int is_play_or_qr_mode(void){return in_play_or_qr;}
static uint32_t get_ms_clock(void){return clock_ms;}
static void clrscr(void){clears++;}
static void draw_gallery_prompt(void){prompts++;}
static void start_action(int action){last_action=action;launched++;strcpy(last_status,"Running...");busy=1;}
'''
wrapper=r'''
void reset_all(void){gallery_prompt=gallery_results=gallery_failed=busy=0;
in_play=1;in_menu=0;in_play_or_qr=1;clock_ms=100;gallery_prompt_time=0;launched=clears=prompts=last_action=0;
strcpy(last_status,"Ready");}
unsigned int step(unsigned int key){return astro_gallery_keypress(key);}
int state(int index){switch(index) {
case 0:return gallery_prompt;case 1:return gallery_results;
case 2:return gallery_failed;case 3:return busy;
case 4:return launched;case 5:return prompts;
case 6:return clears;default:return last_action;}}
const char* status(void){return last_status;}
void set_state(int key,int val){switch(key){
case 0:in_play=val;break;case 1:in_menu=val;break;case 4:in_play_or_qr=val;break;case 5:clock_ms=val;break;
case 2:gallery_results=val;break;case 3:gallery_failed=val;break;}}
'''
with tempfile.TemporaryDirectory(prefix='midnight-gallery-') as tmp:
    src=Path(tmp)/'gallery.c';src.write_text(stubs+production+wrapper)
    so=Path(tmp)/'gallery.so'
    subprocess.run(['gcc','-shared','-fPIC','-Os','-std=c99','-Wall',
                    '-Wextra','-Werror','-Wno-unused-function',str(src),'-o',str(so)],check=True)
    lib=C.CDLL(str(so))
    lib.step.argtypes=[C.c_uint];lib.step.restype=C.c_uint
    lib.state.argtypes=[C.c_int];lib.state.restype=C.c_int
    lib.set_state.argtypes=[C.c_int,C.c_int]
    lib.status.restype=C.c_char_p
    lib.reset_all()
    assert lib.step(29)==0 and lib.state(0)==1 and lib.state(5)==1
    assert lib.status()==b'Gallery: waiting for SET'
    # Playback can temporarily switch Canon's dialog away from PlayMain.
    # A pending SET press must still be consumed before SET+MainDial sees it.
    lib.set_state(0,0)
    assert lib.step(0)==1 and lib.state(0)==1
    lib.set_state(0,1)
    for key in (0,10,20,5,7,23,34):
        assert lib.step(key)==1 and lib.state(0)==1, f'prompt lost on {key}'
    assert lib.step(9)==0 and lib.state(4)==1 and lib.state(7)==5
    assert lib.state(0)==0 and lib.status()==b'Running...'
    lib.reset_all()
    assert lib.step(29)==0
    lib.set_state(0,0)
    assert lib.step(9)==0 and lib.state(4)==1, 'SET leaked during playback-dialog transition'
    lib.reset_all()
    assert lib.step(29)==0
    lib.set_state(5,15101)
    assert lib.step(9)==1 and lib.state(4)==0 and lib.state(0)==0, 'Expired prompt consumed SET'
    lib.reset_all()
    assert lib.step(29)==0
    assert lib.step(29)==0 and lib.state(0)==0 and lib.state(4)==0
    assert lib.status()==b'Gallery: cancelled before solve'
    lib.reset_all()
    assert lib.step(29)==0
    assert lib.step(16)==1 and lib.state(0)==0
    lib.reset_all()
    lib.set_state(2,1)
    assert lib.step(29)==0 and lib.state(1)==0
    lib.set_state(3,1)
    assert lib.step(29)==0 and lib.state(2)==0
    lib.reset_all()
    lib.set_state(0,0)
    assert lib.step(29)==1 and lib.state(0)==0
    lib.reset_all()
    lib.set_state(1,1)
    assert lib.step(29)==1 and lib.state(0)==0
    print('PASS: Q/SET launches exactly once; key releases, unknown events do not dismiss; Q cancels; navigation and errors handled')
