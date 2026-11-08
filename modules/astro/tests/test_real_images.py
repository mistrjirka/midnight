#!/usr/bin/env python3
"""Real downloaded FITS, CR2 and Bahtinov frame smoke tests.

Requires numpy, pillow, astropy and rawpy. These files remain OUT of Git.
The frames were not captured by the Canon firmware. We normalize and
crop them to emulate an 8-bit LiveView input for the native C algorithms.
This is NOT a real-camera plate-solving validation.
"""
from pathlib import Path
import ctypes as C
import math
import sys
import numpy as np
from PIL import Image,ImageFilter
from astropy.io import fits
import rawpy
from test_solver import load, Star,IMAGE_W as W,IMAGE_H as H

DATA=Path(sys.argv[1] if len(sys.argv)>1 else ".astro-data/real")
api=load()
api.astro_focus_metric.argtypes=[C.POINTER(C.c_uint8),C.c_int,C.c_int,C.c_int,C.POINTER(C.c_int),C.POINTER(C.c_int)]
api.astro_focus_metric.restype=C.c_int
api.astro_focus_metric_locked.argtypes=[C.POINTER(C.c_uint8),C.c_int,C.c_int,C.c_int,C.POINTER(C.c_int),C.POINTER(C.c_int),C.c_int]
api.astro_focus_metric_locked.restype=C.c_int
api.astro_fov_cdeg.argtypes=[C.c_int,C.c_int]
api.astro_fov_cdeg.restype=C.c_int

def native(im,x=None,y=None):
    pix=np.asarray(im,dtype=np.uint8)
    assert pix.shape==(H,W),pix.shape
    buf=pix.ctypes.data_as(C.POINTER(C.c_uint8))
    stars=(Star*32)()
    count=api.astro_find_stars(buf,W,H,W,stars,32)
    sx=C.c_int(W//2 if x is None else x)
    sy=C.c_int(H//2 if y is None else y)
    if x is None:
        score=api.astro_focus_metric(buf,W,H,W,C.byref(sx),C.byref(sy))
    else:
        score=api.astro_focus_metric_locked(buf,W,H,W,C.byref(sx),C.byref(sy),8)
    assert 0<=count<=32 and -1<=score<=4000
    return count,score

def normalize(a):
    data=np.asarray(a,dtype=np.float32)
    lo=float(np.nanpercentile(data,35))
    hi=float(np.nanpercentile(data,99.8))
    return np.uint8(np.clip((data-lo)/max(1,hi-lo),0,1)*225+20)

for source in ("HorseHead.fits","M13_blue_0001.fits"):
    with fits.open(DATA/source) as hdus:
        data=normalize(hdus[0].data)
    a=data[:H,:W]
    count,score=native(a)
    print(source,"native C detections",count,"sharpness",score)
    assert count>1,source
    for shift in (12,24,50):
        # Test other crops, different sky brightness and star density.
        native(data[shift:shift+H,shift:shift+W])
    # Dust only suppresses light; test a circular dark shadow.
    yy,xx=np.ogrid[:H,:W]
    shade=(xx-100)**2+(yy-90)**2<32**2
    dusty=a.copy();dusty[shade]=np.maximum(dusty[shade]-50,0)
    native(dusty)

# The Horsehead image has an isolated, real star near (146,158).
# A locked star should score worse when deliberately blurred.
with fits.open(DATA/"HorseHead.fits") as f: a=normalize(f[0].data)
im=Image.fromarray(a[:H,:W])
scores=[]
for blur in (0,0.8,1.6,3):
    candidate=im if blur==0 else im.filter(ImageFilter.GaussianBlur(blur))
    _,score=native(candidate,146,158)
    scores.append(score)
assert all(b>a for a,b in zip(scores,scores[1:])),scores
print("Real Horsehead star blur response:",scores)

# A RAW camera photo has a completely different Bayer pattern and
# may not have astronomical stars. Test safe conversion and processing.
with rawpy.imread(str(DATA/"canon_eos_600d_sample.cr2")) as raw:
    a=raw.raw_image_visible
    assert a.shape[0]>H*2 and a.shape[1]>W*2
    lo,hi=np.percentile(a,[10,99])
    roi=a[1200:1200+H*2:2,1500:1500+W*2:2]
    preview=np.uint8(np.clip((roi-lo)/max(1,hi-lo),0,1)*225+20)
    count,score=native(preview)
    print("Real Canon 600D CR2 crop:",a.shape,"candidate peaks",count,"score",score)

# Real Bahtinov image is a visual mask example, not ground-truth
# numerical focus error. It must not crash our general star detector.
b=Image.open(DATA/"bahtinov_mask_example.jpg").convert("L").resize((W,H))
count,score=native(b)
print("Bahtinov mask example:",count,score,
      "(general sharpness; mask spike offset NOT implemented)")

for sensor in (223,358):
    for focal in (70,135,200,300,400):
        measured=api.astro_fov_cdeg(sensor,focal)/100
        reference=2*math.degrees(math.atan((sensor/10)/(2*focal)))
        assert abs(measured-reference)<max(0.25,reference*0.05), (sensor,focal,measured,reference)
print("PASS: real FITS/CR2/Bahtinov frames processed, dust shadows tolerated,")
print("      real Horsehead star blur monotonic, FOV scaling agrees with geometry")
