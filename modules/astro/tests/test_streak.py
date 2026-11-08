#!/usr/bin/env python3
"""No-motor regression for satellite/aircraft trail masking in the solver.

Uses the identical integer C implementation as the camera. Checks that
a line is removed and clean/random photographs are left alone.
"""
import ctypes as C
from pathlib import Path
import random
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
LIB=ROOT/"build"/"libastro_streak_test.so"
W,H=360,240
LIB.parent.mkdir(exist_ok=True)
subprocess.run(["gcc","-shared","-fPIC","-std=c99","-Wall","-Wextra",
                "-Werror","-O2","-o",str(LIB),
                str(ROOT/"astro_streak.c")],check=True)
mask=C.CDLL(str(LIB)).astro_mask_streak
mask.argtypes=[C.POINTER(C.c_uint8),C.c_int,C.c_int,C.c_int]
mask.restype=C.c_int
def apply(data,w=W,h=H):
    a=(C.c_uint8*len(data)).from_buffer_copy(data)
    found=mask(a,w,h,w)
    return found,bytes(a)
def pgm(path):
    raw=path.read_bytes()
    assert raw[:15]==b"P5\n360 240\n255\n"
    return raw[15:]
# Sparse stars should never look like a satellite/aircraft track.
clean=bytearray([12]*(W*H))
rng=random.Random(789)
for i in range(100):
    x=rng.randint(10,W-10);y=rng.randint(10,H-10)
    for dy in range(-2,3):
        for dx in range(-2,3):
            v=max(0,215-(dx*dx+dy*dy)*48)
            clean[(y+dy)*W+x+dx]=max(clean[(y+dy)*W+x+dx],v)
status,cleaned=apply(clean)
assert status==0 and cleaned==clean,"Clean sky incorrectly modified"
print("PASS: clean synthetic sky unchanged")
# Noise-only sky (including hot pixels) must be unchanged.
noise=bytearray(rng.randint(5,40) for _ in range(W*H))
for k in range(100):
    x=rng.randint(5,W-5);y=rng.randint(5,H-5)
    noise[y*W+x]=255
status,new=apply(noise)
assert status==0 and new==noise
print("PASS: noise/hot pixels unchanged")
# Synthetic sloping, horizontal and vertical 270px tracks.
for name in ("slope","horiz","vertical"):
    inp=bytearray(clean)
    xy=[]
    for t in range(25,300):
        if name=="slope":x,y=t,197-t//2
        elif name=="horiz":x,y=t,90
        else:x,y=165,t//2+10
        if 2<x<W-3 and 2<y<H-3:
            xy.append((x,y))
            inp[y*W+x]=235
            # 1px streak with enough samples in every 2x2 Hough block
            if y+1<H:inp[(y+1)*W+x]=235
    status,out=apply(inp)
    removed=sum(out[y*W+x]<70 for x,y in xy)
    assert status==1,(name,removed,len(xy))
    assert removed>len(xy)*0.6,(name,removed,len(xy))
    assert out != inp
    print("PASS:",name,"track pixels removed:",removed,"/",len(xy))
# Distribution test sample is a genuine synthetic star field without a streak.
if len(sys.argv)>1:
    data=pgm(Path(sys.argv[1]))
    status,out=apply(data)
    assert status==0 and out==data,"HYG field was incorrectly masked"
    print("PASS: test-field stars unchanged")
    # Confirm that the same genuine HYG scene still solves when one
    # long bright trail has been added and then rejected. No user photos
    # are distributed in CI.
    from test_solver import load,Star,Catalog,Sol,catalog_for_hint
    import struct
    import ctypes as C
    lib=load()
    raw=Path(sys.argv[1]).with_name("STARS.BIN").read_bytes()
    stars=list(struct.iter_unpack("<IiHH",raw[16:]))
    catalog,_=catalog_for_hint(stars,5,0,35)
    ca=(Catalog*len(catalog))(*(Catalog(*x) for x in catalog))
    damaged=bytearray(data)
    for x in range(20,331):
        y=155-x//3
        damaged[y*W+x]=255
        damaged[(y+1)*W+x]=255
    result,cleaned=apply(damaged)
    assert result==1,"synthetic HYG trail not detected"
    im=(C.c_uint8*len(cleaned)).from_buffer_copy(cleaned)
    found=(Star*32)()
    n=lib.astro_find_stars(im,W,H,W,found,32)
    sol=Sol()
    got=lib.astro_solve_hinted(found,n,ca,len(catalog),
                                W,H,640,C.byref(sol))
    assert got>=8,("HYG scene no longer solves after trail masking",got,n)
    print("PASS: HYG synthetic scene solves with a satellite trail removed:",
          got,"stars")
print("PASS: satellite streak filtering")
