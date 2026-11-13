#!/usr/bin/env python3
"""Build an all-sky multiscale quad atlas and shared verification fields.

Only HYG catalog positions/magnitudes are inputs. No photographs, solved
coordinates, camera pointing, or image-specific hints are used in this build.
Patterns use the 10 brightest stars in overlapping circular sky fields.
Each shared field stores up to 96 catalog stars for independent verification.
"""
from pathlib import Path
from itertools import combinations
import struct, math, time, multiprocessing as mp, argparse
import numpy as np
from scipy.spatial import cKDTree
ROOT=Path(__file__).resolve().parents[3]
DT=np.dtype([('key','<u4'),('fine','<u4'),('field','<u4'),('a','u1'),('b','u1'),('flip','u1'),('pad','u1')])
COMB=np.array(list(combinations(range(10),4)),np.int32)
EDGES=np.array(list(combinations(range(4),2)),np.int32)
NVERIFY=96
FIELD_BYTES=16+NVERIFY*8
G=None

def encode(points):
    """Vectorized canonical quad fingerprint, plus canonical endpoint roles."""
    q=points[COMB]
    edges=q[:,EDGES[:,1]]-q[:,EDGES[:,0]]
    d=np.sum(edges*edges,axis=2)
    m=d.argmax(axis=1);a=EDGES[m,0];b=EDGES[m,1]
    valid=d.max(axis=1)>1e-8
    ix=np.arange(len(q));others=np.array([[k for k in range(4) if k not in (aa,bb)] for aa,bb in zip(a,b)])
    best=np.full(len(q),2**32-1,np.uint32);fine=np.zeros(len(q),np.uint32)
    ba=a.copy();bb=b.copy();bf=np.zeros(len(q),np.uint8)
    for swap in range(2):
        aa,ab=(a,b) if not swap else (b,a)
        origin=q[ix,aa];v=q[ix,ab]-origin;denom=np.maximum((v*v).sum(1),1e-18)
        c=q[ix[:,None],others]-origin[:,None,:]
        ux=np.clip((c[:,:,0]*v[:,None,0]+c[:,:,1]*v[:,None,1])/denom[:,None],0,1)
        vy=np.clip((c[:,:,0]*v[:,None,1]-c[:,:,1]*v[:,None,0])/denom[:,None],-1,1)
        for flip in range(2):
            vflip=vy*(-1 if flip else 1)
            cx=np.rint(ux*24).astype(np.uint32);cy=np.rint((vflip+1)*12).astype(np.uint32)
            fx=np.rint(ux*255).astype(np.uint32);fy=np.rint((vflip+1)*127.5).astype(np.uint32)
            reverse=(cx[:,0]>cx[:,1])|((cx[:,0]==cx[:,1])&(cy[:,0]>cy[:,1]))
            k=np.where(reverse,1,0);l=1-k
            key=(cx[ix,k]<<15)|(cy[ix,k]<<10)|(cx[ix,l]<<5)|cy[ix,l]
            f=(fx[ix,k]<<24)|(fy[ix,k]<<16)|(fx[ix,l]<<8)|fy[ix,l]
            choose=key<best
            best[choose]=key[choose];fine[choose]=f[choose]
            ba[choose]=aa[choose];bb[choose]=ab[choose];bf[choose]=flip
    return best,fine,ba,bb,bf,valid

# Radii (degrees) of the overlapping sky fields. The optional --wide index
# (SKYW.*) adds 19 and 27 degree fields for fields of view beyond ~60 degrees;
# they use 4-millidegree coordinate units (stored in the field header pad).
RADII=(2.3,3.5,5.5,8.5,13.0)
RADII_WIDE=(19.0,27.0)
def field_unit(radius):
    return 1 if radius<=13 else 4

def worker(task):
    start,fields=task
    src,xyz,tree,mags=G
    records=[];blocks=[]
    for local,(ra0,dec0,radius) in enumerate(fields):
        fid=start+local
        ra,dec=np.deg2rad([ra0,dec0]);o=np.array([math.cos(dec)*math.cos(ra),math.cos(dec)*math.sin(ra),math.sin(dec)])
        east=np.array([-math.sin(ra),math.cos(ra),0.]);north=np.cross(o,east)
        ids=np.asarray(tree.query_ball_point(o,2*math.sin(math.radians(radius)/2)),dtype=int)
        ids=sorted(ids,key=lambda j:(int(mags[j]),int(j)))[:10]
        if len(ids)<10:
            blocks.append(bytes(FIELD_BYTES));continue
        # Verification covers the observed frame beyond the pattern region.
        # Wide-lens levels store coarser units so 55-degree fields fit int16.
        unit=field_unit(radius)
        reach=min(30,radius*2.1) if unit==1 else min(55,radius*2.1)
        allids=np.asarray(tree.query_ball_point(o,2*math.sin(math.radians(reach)/2)),dtype=int)
        others=sorted((j for j in allids if j not in ids),key=lambda j:(int(mags[j]),int(j)))[:NVERIFY-10]
        selected=np.array(ids+others,dtype=int)
        denom=xyz[selected]@o
        pos=np.column_stack((xyz[selected]@east,xyz[selected]@north))/denom[:,None]
        pos*=180000/math.pi/unit
        # Fixed signed units of `unit` millidegrees (header pad field).
        inside=np.all(np.abs(pos)<32760,axis=1)
        selected=selected[inside];pos=pos[inside]
        if len(selected)<10:
            blocks.append(bytes(FIELD_BYTES));continue
        cats=np.zeros(NVERIFY,dtype=[('x','<i2'),('y','<i2'),('name','<u2'),('mag','<u2')])
        cats['x'][:len(pos)]=np.rint(pos[:,0]).astype(np.int16)
        cats['y'][:len(pos)]=np.rint(pos[:,1]).astype(np.int16)
        cats['name'][:len(pos)]=src['name'][selected];cats['mag'][:len(pos)]=mags[selected]
        header=struct.pack('<IiHHI',round(ra0*10000)%3600000,round(dec0*10000),len(selected),round(radius*100),unit if unit>1 else 0)
        blocks.append(header+cats.tobytes())
        # Encode the quantized coordinates actually stored on the SD card.
        p=np.column_stack((cats['x'][:10],cats['y'][:10])).astype(float)
        key,fine,a,b,flip,valid=encode(p)
        span2=np.sum((p[COMB[np.arange(len(COMB)),a]]-p[COMB[np.arange(len(COMB)),b]])**2,axis=1)
        # Tiny patterns amplify integer centroid error and are poor identifiers.
        valid &= span2>=500**2
        r=np.empty(np.count_nonzero(valid),DT)
        r['key']=key[valid];r['fine']=fine[valid];r['field']=fid
        r['a']=COMB[np.arange(len(COMB)),a][valid];r['b']=COMB[np.arange(len(COMB)),b][valid]
        r['flip']=flip[valid];r['pad']=0
        records.append(r)
    return np.concatenate(records) if records else np.empty(0,DT),b''.join(blocks),start

def generate(radii=RADII,prefix='SKY'):
    start=time.monotonic()
    src=np.frombuffer((ROOT/'.astro-data/STARS.BIN').read_bytes()[16:],dtype=[('ra','<u4'),('dec','<i4'),('mag','<u2'),('name','<u2')])
    ra=np.deg2rad(src['ra']/10000);dec=np.deg2rad(src['dec']/10000)
    xyz=np.column_stack((np.cos(dec)*np.cos(ra),np.cos(dec)*np.sin(ra),np.sin(dec)))
    global G
    G=(src,xyz,cKDTree(xyz),src['mag'])
    fields=[]
    for radius in radii:
        spacing=radius*.72
        for d in np.arange(-86.0,86.001,spacing):
            n=max(1,round(360*math.cos(math.radians(d))/spacing))
            for k in range(n):fields.append((k*360/n,float(d),radius))
    print('fields',len(fields),'pattern upper bound',len(fields)*len(COMB),'field bytes',FIELD_BYTES,flush=True)
    fld=ROOT/f'.astro-data/{prefix}.FLD';pat=ROOT/f'.astro-data/{prefix}.PAT';dire=ROOT/f'.astro-data/{prefix}.DIR'
    arrays=[]
    with fld.open('wb') as file:
        file.write(struct.pack('<4sIII',b'SFLD',1,len(fields),FIELD_BYTES))
        file.truncate(16+len(fields)*FIELD_BYTES)
        tasks=[(i,fields[i:i+256]) for i in range(0,len(fields),256)]
        with mp.get_context('fork').Pool(8) as pool:
            for i,(r,b,idx) in enumerate(pool.imap_unordered(worker,tasks)):
                arrays.append(r);file.seek(16+idx*FIELD_BYTES);file.write(b)
                if i%16==0:print('chunks',i+1,'/',len(tasks),'elapsed',round(time.monotonic()-start,1),flush=True)
    arr=np.concatenate(arrays);print('sorting',len(arr),'patterns',flush=True)
    arr.sort(order=['key','fine','field','a','b'])
    with pat.open('wb') as f:
        f.write(struct.pack('<4sIII',b'SPAT',1,len(arr),DT.itemsize));arr.tofile(f)
    unique,begin=np.unique(arr['key'],return_index=True)
    table=np.zeros(len(unique)+1,dtype=[('key','<u4'),('offset','<u4')])
    table['key'][:-1]=unique;table['offset'][:-1]=begin
    table[-1]=(0xffffffff,len(arr))
    with dire.open('wb') as f:
        f.write(struct.pack('<4sIII',b'SDIR',1,len(unique),len(arr)));table.tofile(f)
    print('DONE fields',len(fields),'patterns',len(arr),'keys',len(unique),'directory RAM',table.nbytes,
          'MB',round((pat.stat().st_size+fld.stat().st_size+dire.stat().st_size)/1e6,1),'seconds',round(time.monotonic()-start,1),flush=True)

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--wide',action='store_true',help='build the optional SKYW.* wide-lens index instead')
    args=ap.parse_args()
    generate(RADII_WIDE,'SKYW') if args.wide else generate()
