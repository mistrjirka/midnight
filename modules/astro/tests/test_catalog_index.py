#!/usr/bin/env python3
"""2D sky tile lookup regression versus a full catalog scan.

Verifies index file integrity, angular wrap, polar regions and exact star
selection for many camera fields. Reports file-read reductions, not runtime.
"""
import math
import pathlib
import random
import struct
import sys

RA_BINS, DEC_BINS = 72, 36
N_TILES = RA_BINS * DEC_BINS
RECORD = struct.Struct("<IiHH")
HEADER = struct.Struct("<4sIII")
IDX_HEADER = struct.Struct("<4sIIII")
QTAB = (16384, 16322, 16135, 15825, 15396, 14849, 14189,
        13422, 12552, 11585, 10531, 9396, 8192, 6925, 5603,
        4240, 2845, 1428, 0)

def div_trunc(a, b):
    return (1 if a>=0 else -1)*(abs(a)//b)

def cos_q14(dec):
    a = abs(dec)
    if a >= 90:return 0
    return QTAB[a//5]+div_trunc((QTAB[a//5+1]-QTAB[a//5])*(a%5),5)

def filtered(star, ra_hint, dec_hint, cosq, limitx, limity):
    ra, dec, _, _ = star
    dra = ra-ra_hint
    if dra>1800000:dra-=3600000
    if dra < -1800000:dra+=3600000
    x=div_trunc(div_trunc(dra*cosq,16384),100)
    y=div_trunc(dec-dec_hint,100)
    return -limitx<=x<=limitx and -limity<=y<=limity

def sky_fields():
    base=[(5,0,35,640),(2,15,58,640),(6,0,-5,640),
          (23,56,25,640),(12,0,55,640),(0,0,-80,640),
          (23,59,0,640),(18,0,0,1030),(21,14,72,1030),
          (5,0,0,3000),(5,0,85,3000),(0,0,-85,3000)]
    rng=random.Random(20261108)
    base += [(rng.randrange(24),rng.randrange(60),rng.randrange(-85,86),
              rng.choice((200,400,640,1030,3000))) for _ in range(50)]
    return base

def main(path):
    data=path.read_bytes()
    signature,version,count,rs=HEADER.unpack_from(data)
    assert (signature,version,rs,len(data)) == (b"ASTR",1,12,16+count*12)
    ixdata=path.with_suffix(".IDX").read_bytes()
    signature,version,ic,nra,ndec=IDX_HEADER.unpack_from(ixdata)
    assert (signature,version,ic,nra,ndec)==(b"SIDX",2,count,RA_BINS,DEC_BINS)
    assert len(ixdata)==IDX_HEADER.size+4*(N_TILES+1)
    bounds=struct.unpack_from("<"+str(N_TILES+1)+"I",ixdata,IDX_HEADER.size)
    assert bounds[0]==0 and bounds[-1]==count
    assert all(bounds[i]<=bounds[i+1] for i in range(N_TILES))
    stars=list(RECORD.iter_unpack(data[16:]))
    assert len(stars)==count
    # Tile offsets point to exactly the expected records.
    for tile in range(N_TILES):
        for rec in stars[bounds[tile]:bounds[tile+1]]:
            ra,dec,_,_=rec
            assert min(DEC_BINS-1,(dec+900000)//50000)*RA_BINS+ra//50000 == tile

    ratios=[]
    for rah,ram,dec,fov in sky_fields():
        ra_hint=rah*150000+ram*2500
        dec_hint=dec*10000
        cq=cos_q14(dec)
        limitx=fov//2+360
        limity=fov//3+360
        ra_half=min(1800000,(limitx*100*16384+cq-1)//cq+1000) if cq else 1800000
        dec_half=limity*100+1000
        scanned=[]
        for db in range(DEC_BINS):
            dec_c=-900000+db*50000+25000
            if abs(dec_c-dec_hint)>dec_half+25000:continue
            for rb in range(RA_BINS):
                ra_c=rb*50000+25000
                delta=abs(ra_c-ra_hint)
                delta=min(delta,3600000-delta)
                if delta>ra_half+25000:continue
                tile=db*RA_BINS+rb
                scanned.extend(range(bounds[tile],bounds[tile+1]))
        indexed={j for j in scanned if filtered(stars[j],ra_hint,dec_hint,cq,limitx,limity)}
        full={j for j in range(count) if filtered(stars[j],ra_hint,dec_hint,cq,limitx,limity)}
        assert indexed==full,(rah,ram,dec,fov,len(indexed),len(full),list(full-indexed)[:5])
        ratio=count/max(1,len(scanned))
        ratios.append(ratio)
        if len(ratios)<=12:
            print(f"RA {rah:02}:{ram:02} Dec {dec:+03}, FOV {fov/100:g}deg: "
                  f"{len(scanned):5} read / {count} total, "
                  f"{ratio:5.1f}x less data, {len(full)} matching stars")
    ratios.sort()
    print(f"PASS {len(ratios)} fields including poles and RA wrap. "
          f"Read-volume reduction: min={ratios[0]:.1f}x "
          f"median={ratios[len(ratios)//2]:.1f}x max={ratios[-1]:.1f}x")
    print(f"RAM index: {len(ixdata)} bytes. Catalog: {len(data)} bytes on SD.")
if __name__=="__main__":
    main(pathlib.Path(sys.argv[1]))
