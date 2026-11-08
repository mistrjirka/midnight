#!/usr/bin/env python3
"""Native Linux end-to-end test using the SAME integer C code as camera.
Creates a true HYG-derived star field, detects stars, solves a rotated
field with a 1-3 degree pointing hint. No physical camera required.
"""
from __future__ import annotations
import ctypes as C
import math
import pathlib
import random
import struct
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
SHARED = ROOT / "build" / "libastro_native.so"
IMAGE_W, IMAGE_H = 360, 240
class Star(C.Structure):
    _fields_ = [("x",C.c_int16),("y",C.c_int16),("flux",C.c_uint16)]
class Catalog(C.Structure):
    _fields_ = [("x",C.c_int16),("y",C.c_int16),("mag10",C.c_uint16)]
class Sol(C.Structure):
    _fields_ = [("center_x_cdeg",C.c_int32),("center_y_cdeg",C.c_int32),
                ("matched",C.c_int),("rms_x10",C.c_int)]

def load():
    SHARED.parent.mkdir(exist_ok=True)
    subprocess.run(["gcc","-shared","-fPIC","-std=c99","-O2","-Wall",
                    "-Wextra","-o",str(SHARED),str(ROOT/"astro_core.c")],check=True)
    api=C.CDLL(str(SHARED))
    api.astro_find_stars.argtypes=[C.POINTER(C.c_uint8),C.c_int,C.c_int,
                                   C.c_int,C.POINTER(Star),C.c_int]
    api.astro_find_stars.restype=C.c_int
    api.astro_solve_hinted.argtypes=[C.POINTER(Star),C.c_int,C.POINTER(Catalog),
                                     C.c_int,C.c_int,C.c_int,C.c_int,C.POINTER(Sol)]
    api.astro_solve_hinted.restype=C.c_int
    return api

def catalog_for_hint(stars, rah, ram, dec, fov=640):
    cosq=math.cos(math.radians(dec))
    ra0=rah*15+ram/4
    projected=[]
    all_projected=[]
    lim_x=fov/2+360
    lim_y=fov/3+360
    for ra, de, m10, _ in stars:
        dra=ra/1e4-ra0
        if dra>180: dra-=360
        if dra< -180: dra+=360
        x=round(dra*cosq*100)
        y=round((de/1e4-dec)*100)
        if abs(x)<lim_x and abs(y)<lim_y:
            t=math.tan(math.radians(dec))
            x0,y0=x,y
            x=round(x0-x0*y0*t/5729.58)
            y=round(y0+x0*x0*t/(2*5729.58))
            item=(x,y,m10)
            all_projected.append(item)
    projected=sorted(all_projected,key=lambda x:x[2])[:64]
    return projected,all_projected

def test_scene(api, stars, rah,ram,dec,actual_offset_x,actual_offset_y,angle,save=None):
    random.seed(123)
    catalog, allstars=catalog_for_hint(stars,rah,ram,dec)
    cos=math.cos(math.radians(angle))
    sin=math.sin(math.radians(angle))
    scale=IMAGE_W/640
    image=bytearray(IMAGE_W*IMAGE_H)
    for i in range(len(image)):
        image[i]=max(0,round(15+random.gauss(0,1.3)))
    kept=0
    for x,y,m10 in allstars:
        mag=m10/10-5
        if mag>9:continue
        dx=(x-actual_offset_x)*scale
        dy=(y-actual_offset_y)*scale
        px=IMAGE_W/2+cos*dx-sin*dy
        py=IMAGE_H/2+sin*dx+cos*dy
        if px<5 or px>IMAGE_W-6 or py<5 or py>IMAGE_H-6:continue
        # Sensor gain is illustrative, not a camera sensitivity model.
        amplitude=min(220,260*10**(-0.21*(mag-5)))
        if amplitude<14:continue
        kept+=1
        for oy in range(-4,5):
            yy=round(py)+oy
            for ox in range(-4,5):
                xx=round(px)+ox
                val=amplitude*math.exp(-((xx-px)**2+(yy-py)**2)/(2*1.2**2))
                idx=yy*IMAGE_W+xx
                image[idx]=min(255,image[idx]+round(val))
    im=(C.c_uint8*len(image)).from_buffer(image)
    detected=(Star*32)()
    count=api.astro_find_stars(im,IMAGE_W,IMAGE_H,IMAGE_W,detected,32)
    cat=(Catalog*len(catalog))(*(Catalog(*c) for c in catalog))
    sol=Sol()
    matched=api.astro_solve_hinted(detected,count,cat,len(cat),IMAGE_W,IMAGE_H,640,C.byref(sol))
    ex=sol.center_x_cdeg-actual_offset_x
    ey=sol.center_y_cdeg-actual_offset_y
    print(f"hint={rah:02d}:{ram:02d} dec={dec:+3d} rot={angle:+3d} actual={actual_offset_x:+4d},{actual_offset_y:+4d}cdeg",
          f"visible={kept:2d} detected={count:2d} catalog={len(catalog):2d}",
          f"match={matched:2d} mse100={sol.rms_x10:4d} error={ex:+4d},{ey:+4d}cdeg")
    if save:
        with save.open("wb") as f:
            f.write(b"P5\n360 240\n255\n")
            f.write(image)
    assert matched>=8, "solver needs >=8 distinct star matches"
    assert abs(ex)<=15 and abs(ey)<=15, "solution wrong by more than 0.15 degree"

def main():
    if len(sys.argv)!=2:
        raise SystemExit("usage: test_solver.py .astro-data/STARS.BIN")
    path=pathlib.Path(sys.argv[1])
    raw=path.read_bytes()
    magic,version,num,size=struct.unpack_from("<4sIII",raw)
    assert (magic,version,size)==(b"ASTR",1,12)
    stars=list(struct.iter_unpack("<IiHH",raw[16:]))
    assert len(stars)==num
    api=load()
    test_scene(api,stars,5,0,35,100,60,28,save=path.parent/"TEST.PGM")
    test_scene(api,stars,2,15,58,-80,90,-17)
    test_scene(api,stars,6,0,-5,70,-40,61)
    test_scene(api,stars,23,56,25,90,-90,-65)
    test_scene(api,stars,12,0,55,-40,60,5)
    # A wrong solution is worse than no solution. Verify that noise-only
    # fields do not pass the inlier + residual quality checks.
    catalog, _ = catalog_for_hint(stars,5,0,35)
    cat=(Catalog*len(catalog))(*(Catalog(*item) for item in catalog))
    random.seed(10)
    for i in range(120):
        points=(Star*26)(*(Star(random.randint(2,358),
                                random.randint(2,237),1000-k*10)
                             for k in range(26)))
        sol=Sol()
        assert not api.astro_solve_hinted(points,26,cat,len(cat),
                                          IMAGE_W,IMAGE_H,640,C.byref(sol)), i
    print("PASS: 5 synthetic HYG fields solved and 120 random fields rejected")
if __name__ == "__main__":main()
