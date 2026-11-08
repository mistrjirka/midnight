#!/usr/bin/env python3
"""Regression for integer DoG plate-solving extraction.

Use the same portable C as the Canon ARM module. Do not require photographs
from the user in CI; the full HYG synthetic star field has fixed expected
coordinates. Real photograph regressions are run privately in QEMU.
"""
import ctypes as C
from pathlib import Path
import random
import struct
import subprocess
from test_solver import Star, Catalog, Sol, catalog_for_hint

ROOT=Path(__file__).resolve().parents[1]
ML=ROOT.parents[1]
W,H=360,240
SHARED=ROOT/'build'/'libastro_psf_test.so'
subprocess.run(['gcc','-std=c99','-O2','-Wall','-Wextra','-Werror',
                '-shared','-fPIC','-o',str(SHARED),
                str(ROOT/'astro_core.c'),str(ROOT/'astro_streak.c'),
                str(ROOT/'astro_psf.c')],check=True)
lib=C.CDLL(str(SHARED))
lib.astro_find_stars_psf.argtypes=[C.POINTER(C.c_uint8),C.c_int,C.c_int,C.c_int,C.POINTER(Star),C.c_int]
lib.astro_find_stars_psf.restype=C.c_int
lib.astro_mask_streak.argtypes=[C.POINTER(C.c_uint8),C.c_int,C.c_int,C.c_int]
lib.astro_mask_streak.restype=C.c_int
lib.astro_solve_hinted.argtypes=[C.POINTER(Star),C.c_int,C.POINTER(Catalog),C.c_int,C.c_int,C.c_int,C.c_int,C.POINTER(Sol)]
lib.astro_solve_hinted.restype=C.c_int

def detect(data,streak=True):
    array=(C.c_uint8*len(data)).from_buffer_copy(data)
    original=bytes(array)
    masked=lib.astro_mask_streak(array,W,H,W) if streak else 0
    out=(Star*32)()
    n=lib.astro_find_stars_psf(array,W,H,W,out,32)
    assert n>=0 and n<=32
    assert original==data
    return array,out,n,masked

def sky():
    data=(ML/'.astro-data'/'TEST.PGM').read_bytes()
    assert data.startswith(b'P5\n360 240\n255\n')
    assert len(data)==15+W*H
    return bytearray(data[15:])

def run_solve(data,cat):
    arr,obs,n,streak=detect(data)
    sol=Sol()
    found=lib.astro_solve_hinted(obs,n,cat,len(cat),
                                 W,H,640,C.byref(sol))
    return found,sol,n,streak

catalog_raw=(ML/'.astro-data'/'STARS.BIN').read_bytes()
stars=list(struct.iter_unpack('<IiHH',catalog_raw[16:]))
projected,_=catalog_for_hint(stars,5,0,35)
cat=(Catalog*len(projected))(*(Catalog(*p) for p in projected))
base=sky()
ret,sol,n,streak=run_solve(base,cat)
assert ret>=8,(ret,n,streak)
assert abs(sol.center_x_cdeg-100)<=15 and abs(sol.center_y_cdeg-60)<=15
assert streak==0,'Clean HYG field detected as a satellite trail'
print('PASS: clean HYG scene',ret,'matched stars, center',sol.center_x_cdeg,sol.center_y_cdeg)

# One long high-contrast trail crossing a realistic star field.
damaged=bytearray(base)
for x in range(20,331):
    y=155-x//3
    damaged[y*W+x]=255
    damaged[(y+1)*W+x]=255
ret,sol,n,streak=run_solve(damaged,cat)
assert streak==1,(ret,n)
assert ret>=8,(ret,n)
assert abs(sol.center_x_cdeg-100)<=15 and abs(sol.center_y_cdeg-60)<=15
print('PASS: HYG scene plus satellite trail',ret,'matched stars')

# Luminance noise and isolated hot pixels must not produce a sky fix.
rng=random.Random(20261108)
for mode in ('blank','salt','salt_with_streak'):
    data=bytearray([15]*(W*H))
    if mode!='blank':
        for i in range(W*H):
            data[i]=max(0,min(255,round(rng.gauss(15,6))))
        for _ in range(250):
            x=rng.randrange(10,W-10);y=rng.randrange(10,H-10)
            data[y*W+x]=255
    if mode=='salt_with_streak':
        for x in range(20,320):
            y=152-x//3
            data[y*W+x]=255
            data[(y+1)*W+x]=255
    ret,sol,n,streak=run_solve(data,cat)
    assert ret==0,(mode,ret,n)
    print('PASS:',mode,'rejected with',n,'candidate peaks')

# Five different real-catalog sky positions, rotations and pointing offsets.
# The HYG-derived scenes are created afresh and never stored in Git.
from test_solver import test_scene
lib.astro_find_stars.argtypes=[C.POINTER(C.c_uint8),C.c_int,C.c_int,
                               C.c_int,C.POINTER(Star),C.c_int]
lib.astro_find_stars.restype=C.c_int
fields=[
    (5,0,35,100,60,28),
    (2,15,58,-80,90,-17),
    (6,0,-5,70,-40,61),
    (23,56,25,90,-90,-65),
    (12,0,55,-40,60,5),
]
for rah,ram,dec,dx,dy,rotation in fields:
    tmp=ROOT/'build'/'test_psf_field.pgm'
    try:
        test_scene(lib,stars,rah,ram,dec,dx,dy,rotation,save=tmp)
        frame=bytearray(tmp.read_bytes()[15:])
        projected,_=catalog_for_hint(stars,rah,ram,dec)
        c=(Catalog*len(projected))(*(Catalog(*p) for p in projected))
        matched,solution,n,masked=run_solve(frame,c)
        assert matched>=8,(rah,dec,rotation,matched,n)
        assert abs(solution.center_x_cdeg-dx)<=15
        assert abs(solution.center_y_cdeg-dy)<=15
        assert masked==0,(rah,dec)
        print('PASS: PSF image',rah,ram,dec,'rotation',rotation,'matched',matched)
    finally:
        tmp.unlink(missing_ok=True)

print('PASS: plate-solving PSF extraction and streak isolation')
