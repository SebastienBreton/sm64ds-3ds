#!/usr/bin/env python3
"""Compare DS final shadow IDs with geometry-only native depth/ID coverage.

The native reconstruction ignores texture alpha. Pixel counts are diagnostics,
not a visual PASS. The DS buffers are read from the unchanged software core.
"""
import argparse
import json
from pathlib import Path
import numpy as np
from PIL import Image
from shadow_raster_audit import audit


def ds_masks(path):
    raw=np.fromfile(path,dtype='<u4')
    if raw.size!=258*194*2:raise ValueError('Unexpected DS attribute buffer size')
    attr=raw.reshape(2,194,258)[0,1:193,1:257]
    # ID 1 translucent output identifies the mode-3 fills in this captured route.
    # Opaque ID 1 is used by actors/doors, so it is not a global player mask.
    shadow=((attr&(1<<22))!=0)&(((attr>>16)&63)==1)
    opaque_id1=((attr&(1<<22))==0)&(((attr>>24)&63)==1)
    return shadow,opaque_id1


def dilate(mask,radius=1):
    padded=np.pad(mask,radius)
    return np.logical_or.reduce([padded[y:y+192,x:x+256] for y in range(2*radius+1) for x in range(2*radius+1)])


def components(mask):
    seen=mask.copy();result=[]
    while seen.any():
        y,x=map(int,np.argwhere(seen)[0]);stack=[(y,x)];seen[y,x]=False;points=[]
        while stack:
            y,x=stack.pop();points.append([x,y])
            for yy in range(max(0,y-1),min(192,y+2)):
                for xx in range(max(0,x-1),min(256,x+2)):
                    if seen[yy,xx]:seen[yy,xx]=False;stack.append((yy,xx))
        result.append({'pixels':len(points),'coordinates':points})
    return sorted(result,key=lambda r:-r['pixels'])


def compare(native,attr_path,ds_image,native_image):
    ds,body=ds_masks(attr_path)
    results={}
    for mode in ['z','w']:
        r=audit(native,mode);coords=np.array(r.pop('final_shadow_coordinates'),dtype=int).reshape(-1,2)
        coverage=np.zeros_like(ds)
        if len(coords):coverage[coords[:,1],coords[:,0]]=True
        extra=coverage&~ds;outside_body=extra&~body
        boundary=outside_body&dilate(ds,2);detached=outside_body&~dilate(ds,2)
        common=coverage&ds
        n=np.asarray(native_image,dtype=np.int16);d=np.asarray(ds_image,dtype=np.int16)
        delta=np.abs(n-d);luma=lambda a:0.2126*a[:,:,0]+0.7152*a[:,:,1]+0.0722*a[:,:,2]
        visibly_darker=(luma(n)<luma(d)*0.7)&(luma(d)-luma(n)>16)
        results[mode]={**r,'ds_final_shadow_pixels':int(ds.sum()),'intersection_pixels':int(common.sum()),
            'native_only_pixels':int(extra.sum()),'ds_only_pixels':int((ds&~coverage).sum()),
            'native_only_on_ds_opaque_id1':int((extra&body).sum()),
            'native_only_outside_ds_opaque_id1':int(outside_body.sum()),
            'outside_id1_within_2px_ds_shadow':int(boundary.sum()),
            'outside_id1_detached_beyond_2px':int(detached.sum()),
            'detached_components':components(detached),
            'detached_visibly_darker_pixels':int((detached&visibly_darker).sum()),
            'detached_pixels_rgb_max_delta_le8':int((detached&(delta.max(2)<=8)).sum()),
            'detached_rgb_samples':[{'xy':[int(x),int(y)],'ds':d[y,x].tolist(),'native':n[y,x].tolist()} for y,x in np.argwhere(detached)],
            'native_only_components':components(extra),
            'native_only_visibly_darker_pixels':int((extra&visibly_darker).sum()),
            'intersection_rgb_mae':float(delta[common].mean()) if common.any() else None,
            'intersection_pixels_rgb_max_delta_le8':int((common&(delta.max(2)<=8)).sum())}
    return results


if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--native',type=Path,required=True)
    ap.add_argument('--ds-attributes',type=Path,required=True)
    ap.add_argument('--ds-image',type=Path,required=True)
    ap.add_argument('--native-image',type=Path,required=True)
    args=ap.parse_args()
    ds_image=Image.open(args.ds_image).convert('RGB').crop((0,0,256,192))
    native_image=Image.open(args.native_image).convert('RGB')
    if native_image.size!=(256,192):raise ValueError('Native image must use original DS viewport')
    print(json.dumps(compare(json.loads(args.native.read_text()),args.ds_attributes,ds_image,native_image),indent=2))
