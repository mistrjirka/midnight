#!/usr/bin/env python3
"""Focus-metric regression test for the camera's native C algorithm."""
from __future__ import annotations
import ctypes as C
import math
from test_solver import load,IMAGE_H,IMAGE_W
api=load()
api.astro_focus_metric.argtypes=[C.POINTER(C.c_uint8),C.c_int,C.c_int,C.c_int,
                                 C.POINTER(C.c_int),C.POINTER(C.c_int)]
api.astro_focus_metric.restype=C.c_int
scores=[]
for sigma in (0.8,1.2,1.8,2.6):
    pixels=bytearray([20]*(IMAGE_W*IMAGE_H))
    for dy in range(-10,11):
        for dx in range(-10,11):
            x=IMAGE_W//2+dx
            y=IMAGE_H//2+dy
            v=220*math.exp(-(dx*dx+dy*dy)/(2*sigma*sigma))
            pixels[y*IMAGE_W+x]=min(255,20+round(v))
    buf=(C.c_uint8*len(pixels)).from_buffer(pixels)
    x=C.c_int(IMAGE_W//2)
    y=C.c_int(IMAGE_H//2)
    score=api.astro_focus_metric(buf,IMAGE_W,IMAGE_H,IMAGE_W,C.byref(x),C.byref(y))
    scores.append(score)
    print(f"sigma={sigma} score={score} star=({x.value},{y.value})")
assert all(a<b for a,b in zip(scores,scores[1:])),scores
flat=bytearray([20]*(IMAGE_W*IMAGE_H))
buf=(C.c_uint8*len(flat)).from_buffer(flat)
assert api.astro_focus_metric(buf,IMAGE_W,IMAGE_H,IMAGE_W,C.byref(x),C.byref(y))==-1
print("PASS: focus metric favors the sharpest star and rejects blank frames")
