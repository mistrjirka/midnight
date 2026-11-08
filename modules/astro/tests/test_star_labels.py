#!/usr/bin/env python3
"""Regression: named-star IDs, catalog geometry, and matched-only annotations.

The source HYG database is compressed to STARS.NAM (fixed 24-byte labels),
with a 16-bit ID inside the original 12-byte catalog record. Never label
an observed pixel without a confirmed geometric catalog match.
"""
import ctypes as C
from pathlib import Path
import struct,sys
from test_solver import load,Star,Catalog,Sol,catalog_for_hint,IMAGE_W as W,IMAGE_H as H

ROOT=Path(__file__).resolve().parents[3]
catalog_path=Path(sys.argv[1])
raw=catalog_path.read_bytes()
magic,version,num,recbytes=struct.unpack_from("<4sIII",raw)
assert (magic,version,recbytes)==(b"ASTR",1,12)
stars=list(struct.iter_unpack("<IiHH",raw[16:]))
assert len(stars)==num
nam=catalog_path.with_suffix(".NAM").read_bytes()
tag,nversion,nlabels,nchars=struct.unpack_from("<4sIII",nam)
assert (tag,nversion,nchars)==(b"SNAM",1,24)
assert len(nam)==16+24*nlabels
assert 0<nlabels<65536
for ra,dec,mag,idx in stars:
    assert idx < nlabels
    if idx:
        chunk=nam[16+24*idx:16+24*(idx+1)]
        assert any(chunk)
        assert chunk[-1]==0
assert sum(star[3]!=0 for star in stars)>20000
print('PASS: labels in indexed catalog',nlabels,'entries')

api=load()
from ctypes import c_uint8
scene=(ROOT/'.astro-data/TEST.PGM').read_bytes()
assert scene.startswith(b'P5\n360 240\n255\n')
im=(c_uint8*(W*H)).from_buffer_copy(scene[15:])
out=(Star*32)()
n=api.astro_find_stars(im,W,H,W,out,32)
cat,allcat=catalog_for_hint(stars,5,0,35)
ca=(Catalog*len(cat))(*(Catalog(*item) for item in cat))
result=Sol()
match=api.astro_solve_hinted(out,n,ca,len(cat),W,H,640,C.byref(result))
assert match>=8 and 0<=result.anchor_obs<n and 0<=result.anchor_cat<len(cat)
assert result.a_q12 or result.b_q12
ai,ac=result.anchor_obs,result.anchor_cat
true_matches=0
for i in range(n):
    nearest=10**10
    for k in range(len(cat)):
        dx=cat[k][0]-cat[ac][0]
        dy=cat[k][1]-cat[ac][1]
        x=out[ai].x+((result.a_q12*dx-result.b_q12*dy)>>12)
        y=out[ai].y+((result.b_q12*dx+result.a_q12*dy)>>12)
        dist=(out[i].x-x)**2+(out[i].y-y)**2
        nearest=min(nearest,dist)
    if nearest<17:true_matches+=1
assert true_matches>=match,(true_matches,match)
print('PASS: returned orientation projects',true_matches,'real candidate star positions')
