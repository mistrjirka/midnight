#!/usr/bin/env python3
"""Native C autofocus state-machine failure simulation.
Hardware calls are replaced with simulated focus motors and video frames.
"""
from __future__ import annotations
import ctypes as c
import math
import pathlib
import random
import subprocess

ROOT=pathlib.Path(__file__).resolve().parents[1]
SO=ROOT/"build"/"libastro_fail.so"
subprocess.run(["gcc","-std=c99","-O2","-shared","-fPIC","-Wall","-Wextra",
                "-o",str(SO),str(ROOT/"astro_core.c"),
                str(ROOT/"astro_focus_control.c")],check=True)
api=c.CDLL(str(SO))
W,H=360,240
READY=c.CFUNCTYPE(c.c_int,c.c_void_p)
POS=c.CFUNCTYPE(c.c_int,c.c_void_p,c.POINTER(c.c_int))
STEP=c.CFUNCTYPE(c.c_int,c.c_void_p,c.c_int,c.c_int)
FRAME=c.CFUNCTYPE(c.c_int,c.c_void_p,c.POINTER(c.c_uint8),c.c_int,c.c_int)
SLEEP=c.CFUNCTYPE(None,c.c_void_p,c.c_int)
class IO(c.Structure):
    _fields_=[("context",c.c_void_p),("ready",READY),("position",POS),
              ("step",STEP),("frame",FRAME),("sleep_ms",SLEEP)]
class Result(c.Structure):
    _fields_=[("status",c.c_int),("initial_position",c.c_int),
              ("final_position",c.c_int),("best_offset",c.c_int),
              ("best_metric",c.c_int),("commands",c.c_int),("samples",c.c_int)]
api.astro_run_autofocus.argtypes=[c.POINTER(IO),c.POINTER(c.c_uint8),
                                 c.c_int,c.c_int,c.c_int,c.c_int,c.POINTER(Result)]
api.astro_run_autofocus.restype=c.c_int
# Status constants from astro_focus_control.h:
OK,NO_LENS,NO_FRAME,NO_STAR,STAR_LOST,NO_FEEDBACK,MOTOR_FAILED,NOT_MOVING,EDGE,WEAK_CHANGE,ABORTED=range(11)

class Sim:
    def __init__(self,case):
        self.case=case
        self.pos=2
        self.commands=0
        self.frames=0
        self.keep=[]
        self.noise=random.Random(4)
        self.ready=READY(self.check_ready)
        self.position=POS(self.check_position)
        self.step=STEP(self.move)
        self.frame=FRAME(self.get_frame)
        self.sleep=SLEEP(self.do_sleep)
        self.keep=[self.ready,self.position,self.step,self.frame,self.sleep]
        self.io=IO(None,*self.keep)
    def check_ready(self,ctx):
        if self.case=="no_lens" or (self.case=="disconnect" and self.commands>=2):
            return 0
        return 1
    def check_position(self,ctx,p):
        if self.case=="no_feedback":
            return 0
        p[0]=2 if self.case=="stuck_position" else self.pos
        return 1
    def move(self,ctx,direction,speed):
        self.commands+=1
        if self.case=="nack":return 0
        if self.case!="stuck":
            self.pos+=direction
        return 1
    def do_sleep(self,ctx,ms):pass
    def get_frame(self,ctx,dst,w,h):
        self.frames+=1
        if self.case=="no_frame" or (self.case=="frame_lost" and self.commands>=2):
            return 0
        rng=random.Random(self.frames)
        img=bytearray([20]*(w*h))
        # Single-pixel hot pixels, with random cosmic ray and sensor noise.
        for k in range(500):
            at=rng.randrange(len(img))
            img[at]=min(255,img[at]+rng.randrange(1,5))
        for x,y in ((40,45),(91,72),(134,191)):
            img[y*w+x]=255
        if self.case not in ("no_star","hot_pixels"):
            # Optimal focus at motor position 0, start position 2.
            sigma=0.85+abs(self.pos)*0.45
            for y in range(h//2-9,h//2+10):
                for x in range(w//2-9,w//2+10):
                    dx=x-w//2;dy=y-h//2
                    v=int(210*math.exp(-(dx*dx+dy*dy)/(2*sigma*sigma)))
                    img[y*w+x]=min(255,20+v)
        c.memmove(dst,bytes(img),len(img))
        return 1

def test(case,expect,allow_extra=None):
    sim=Sim(case)
    buf=(c.c_uint8*(W*H))()
    result=Result()
    got=api.astro_run_autofocus(c.byref(sim.io),buf,W,H,3,1,c.byref(result))
    assert got in (expect,) + tuple(allow_extra or ()),(case,got,expect)
    if case in ("no_lens","no_feedback","no_frame","no_star","hot_pixels"):
        assert sim.commands==0,(case,sim.commands)
    if case in ("nack","stuck","stuck_position"):
        assert sim.commands==1,(case,sim.commands)
    if case=="success":
        assert got==OK and result.best_offset==-2,(got,result.best_offset,result.best_metric)
    print(f"{case:17} result={got:2} cmds={sim.commands:2} frames={sim.frames:2} final={sim.pos:2}")

if __name__=="__main__":
    for case,code in (("no_lens",NO_LENS),
                      ("no_feedback",NO_FEEDBACK),
                      ("no_frame",NO_FRAME),
                      ("no_star",NO_STAR),
                      ("hot_pixels",NO_STAR),
                      ("nack",MOTOR_FAILED),
                      ("stuck",NOT_MOVING),
                      ("stuck_position",NOT_MOVING),
                      ("disconnect",NO_LENS),
                      ("frame_lost",NO_FRAME),
                      ("success",OK)):
        test(case,code)
    print("PASS: 11 autofocus scenarios, including all missing-lens/AF/noise failures")
