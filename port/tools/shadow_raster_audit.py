#!/usr/bin/env python3
"""Depth/ID audit of captured native triangles, following gx_render CPU order.

Diagnostic only: texture alpha is ignored, so counts are geometry-only evidence.
This never emits gameplay PASS and does not change the renderer or captures.
"""
import argparse, json, math
from pathlib import Path
import numpy as np


def clipped_triangles(source):
    polygon=source["vertices"]
    for plane in range(2):
        if not polygon:break
        output=[];a=polygon[-1]
        distance=lambda v:v["w"]-v["clip"][2] if plane else v["w"]+v["clip"][2]
        da=distance(a)
        for b in polygon:
            db=distance(b)
            if (da>=0)!=(db>=0):
                ratio=da/(da-db);w=a["w"]+(b["w"]-a["w"])*ratio
                clip=[x+(y-x)*ratio for x,y in zip(a["clip"],b["clip"])]
                screen=[(x*a["w"]+(y*b["w"]-x*a["w"])*ratio)/w for x,y in zip(a["screen"],b["screen"])] if w>1e-8 else [0,0]
                output.append({"w":w,"clip":clip,"screen":screen,"z":(clip[2]/w+1)*0.5 if w>1e-8 else 0})
            if db>=0:output.append(b)
            a=b;da=db
        polygon=output
    for i in range(1,len(polygon)-1):yield [polygon[0],polygon[i],polygon[i+1]]

def audit(data,depth_mode="z",source_order=False):
    depth=np.full((192,256),1e30,dtype=np.float32)
    opaque=np.full((192,256),63,dtype=np.uint8);translucent=np.full((192,256),255,dtype=np.uint8)
    mask=np.zeros((192,256),dtype=bool);last=np.full((192,256),-1,dtype=np.int32)
    last_shadow=np.zeros((192,256),dtype=bool);previous_mask=False
    counters={key:0 for key in ["mask_fragments","mask_depth_fail","fill_fragments","fill_mask_covered","fill_depth_pass","fill_id_pass","fill_color_writes","shadow_pixels_overwritten"]}
    triangles=data["triangles"]
    if source_order:
        # DS opaque geometry precedes translucent/shadow output after auto sort.
        triangles=[t for t in triangles if ((t["polygon_attr"]>>16)&31)==31 and ((t["polygon_attr"]>>4)&3)!=3]+[t for t in triangles if ((t["polygon_attr"]>>16)&31)!=31 or ((t["polygon_attr"]>>4)&3)==3]
    for source in triangles:
        attr=source["polygon_attr"];pid=(attr>>24)&63;shadow=((attr>>4)&3)==3;is_mask=shadow and pid==0
        for vertices in clipped_triangles(source):
            if is_mask and not previous_mask:mask.fill(False)
            previous_mask=is_mask
            coords=np.array([[math.floor(v) for v in p["screen"]] for p in vertices],dtype=np.float32)
            a,b,c=coords;area=(b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])
            if abs(area)<1e-6 or any(v["w"]<=1e-4 for v in vertices):continue
            if area<0 and not (attr&0x80) or area>=0 and not (attr&0x40):continue
            left=max(0,int(coords[:,0].min()));right=min(255,int(coords[:,0].max()))
            top=max(0,int(coords[:,1].min()));bottom=min(191,int(coords[:,1].max()))
            if left>right or top>bottom:continue
            xx,yy=np.meshgrid(np.arange(left,right+1,dtype=np.float32),np.arange(top,bottom+1,dtype=np.float32))
            weights=[]
            for p,q in [(a,b),(b,c),(c,a)]:weights.append(((q[0]-p[0])*(yy-p[1])-(q[1]-p[1])*(xx-p[0]))/area)
            w0,w1,w2=weights;inside=((w0>=0)&(w1>=0)&(w2>=0))|((w0<=0)&(w1<=0)&(w2<=0))
            for weight,p,q in zip(weights,[a,b,c],[b,c,a]):
                dx,dy=q-p;included=(dy<0 or dy==0 and dx>0) if area>0 else (dy>0 or dy==0 and dx<0)
                if not included:inside &= weight!=0
            if depth_mode=="z":z=w1*np.float32(vertices[0]["z"])+w2*np.float32(vertices[1]["z"])+w0*np.float32(vertices[2]["z"])
            else:z=1/(w1/np.float32(vertices[0]["w"])+w2/np.float32(vertices[1]["w"])+w0/np.float32(vertices[2]["w"]))
            ys=slice(top,bottom+1);xs=slice(left,right+1);dep=depth[ys,xs];ma=mask[ys,xs];op=opaque[ys,xs];tr=translucent[ys,xs];la=last[ys,xs];sh=last_shadow[ys,xs]
            if is_mask:
                counters["mask_fragments"]+=int(inside.sum());failed=inside&(z>=dep);counters["mask_depth_fail"]+=int(failed.sum());ma[failed]=True;continue
            if shadow:
                counters["fill_fragments"]+=int(inside.sum());inside &= ma;counters["fill_mask_covered"]+=int(inside.sum())
            inside &= z<dep
            if shadow:counters["fill_depth_pass"]+=int(inside.sum())
            alpha=(attr>>16)&31
            if alpha==0:continue
            trans=alpha<31 or shadow
            if trans:inside &= tr!=pid
            if shadow:inside &= ~((tr==255)&(op==pid));counters["fill_id_pass"]+=int(inside.sum());counters["fill_color_writes"]+=int(inside.sum())
            if not shadow:counters["shadow_pixels_overwritten"]+=int((inside&sh).sum())
            la[inside]=source["index"];sh[inside]=shadow
            if not trans or attr&(1<<11):dep[inside]=z[inside]
            if trans:tr[inside]=pid
            else:op[inside]=pid;tr[inside]=255
    counters.update({"final_shadow_pixels":int(last_shadow.sum()),"depth_mode":depth_mode,"opaque_first":source_order,"texture_alpha_ignored":True,"final_shadow_coordinates":np.argwhere(last_shadow)[:,::-1].tolist()})
    points=np.argwhere(last_shadow)
    if len(points):counters["shadow_bbox"]=[int(points[:,1].min()),int(points[:,0].min()),int(points[:,1].max()),int(points[:,0].max())]
    return counters

if __name__=="__main__":
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument("capture",type=Path);args=ap.parse_args();data=json.loads(args.capture.read_text())
    print(json.dumps([audit(data,"z"),audit(data,"w"),audit(data,"z",True),audit(data,"w",True)],indent=2))
