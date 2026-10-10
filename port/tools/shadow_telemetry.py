#!/usr/bin/env python3
"""Inspect linked original shadow volumes and retail DS polygon attributes.

Read-only: list state, player shadow controls, source matrices, original shadow
model/material words, and mode-3 mask/fill geometry. No position adjustment.
"""
import argparse
import collections
import hashlib
import json
import re
import struct
from pathlib import Path

SCHEMA="sm64ds-shadow-telemetry-v1"

def inspect_ram(path):
    b=Path(path).read_bytes()
    if len(b)!=4*1024*1024:raise ValueError("Expected retail runner MainRAM")
    def read(a,fmt="I",n=1):
        off=a-0x02000000
        if off<0 or off+struct.calcsize(fmt)*n>len(b):raise ValueError(f"Bad RAM address {a:#x}")
        v=struct.unpack_from("<"+fmt*n,b,off);return v[0] if n==1 else list(v)
    player=read(0x0209f394);head=read(0x0209cef4);nodes=[];seen=set();node=head
    while node:
        if node in seen or len(seen)>=512:raise ValueError("Malformed shadow list")
        seen.add(node);components=read(node+8);matrix=read(node+12)
        model=read(components);material=read(components+4);count=read(model+0x24)
        if count>256:raise ValueError("Invalid material count")
        model_kind={0x020ad560:"builtin_cylinder",0x020ad524:"builtin_cuboid"}.get(model,"other")
        entry={"is_player":node==player+0x2ac,"model_kind":model_kind,
               "scale_fx12":read(node+0x10,"i",3),"opacity":read(node+0x1c,"B"),
               "matrix_fx12":read(matrix,"i",12),"scale_shift":read(model),
               "num_bones":read(model+4),"num_materials":count,"num_display_list_groups":read(model+0xc),"materials":[]}
        for i in range(count):
            m=material+i*0x30
            entry["materials"].append({"tex_param":read(m+0x1c),"palette_base":read(m+0x20),"polygon_attr":read(m+0x24),"dif_amb":read(m+0x28),"spe_emi":read(m+0x2c)})
        lists=[];groups=read(model+0x10)
        for group in range(read(model+0xc)):
            n=read(groups+group*8);table=read(groups+group*8+4)
            if n>256:raise ValueError("Invalid display-list group")
            for k in range(n):
                size=read(table+k*16+8);address=read(table+k*16+12);value=2166136261
                start=address-0x02000000
                if size>1024*1024 or not 0<=start<=len(b)-size:raise ValueError("Invalid display-list stream")
                for byte in b[start:start+size]:value=((value^byte)*16777619)&0xffffffff
                lists.append({"size":size,"fnv1a":value})
        entry["display_lists"]=lists
        nodes.append(entry);node=read(node+0x24)
    return {"schema":SCHEMA,"ram_sha256":hashlib.sha256(b).hexdigest(),
            "freeze":read(0x0209ceec,"B"),"player_shadow_controls":{hex(o):read(player+o,"B") for o in [0x6f5,0x6fb,0x717]},"nodes":nodes}

def inspect_polygons(path,include_all=False):
    polys=[];current=None
    for line in Path(path).read_text().splitlines():
        fields=line.split()
        if fields and fields[0]=="poly":
            current={"index":int(fields[1]),"polygon_attr":int(fields[3],16),"tex_param":int(fields[5],16),"palette_base":int(fields[7],16),"num_vertices":int(fields[9]),"facing":int(fields[11]),"translucent":int(fields[13]),"vertices":[]};polys.append(current)
            if len(fields)>14:current.update({"wbuffer":int(fields[15]),"shadowmask":int(fields[17]),"shadow":int(fields[19])})
        elif fields and fields[0]=="v" and current is not None:
            # clip X/Y/Z/W; texcoord; integer viewport; high-resolution viewport; z/w.
            current["vertices"].append({"clip":list(map(int,fields[3:7])),"uv":list(map(int,fields[8:10])),"screen":list(map(int,fields[11:13])),"hires":list(map(int,fields[14:16])),"z":int(fields[17]),"w":int(fields[19]),"clipped":int(fields[21])})
            if len(fields)>22:current["vertices"][-1].update({"color":list(map(int,fields[23:26])),"final_color":list(map(int,fields[27:30]))})
    histogram=collections.Counter(p["polygon_attr"] for p in polys)
    shadows=[p for p in polys if (p["polygon_attr"]>>4)&3==3]
    result={"polygons":len(polys),"attribute_histogram":{f"{a:08x}":n for a,n in sorted(histogram.items())},"shadow_masks":sum(not((p["polygon_attr"]>>24)&63) for p in shadows),"shadow_fills":sum(bool((p["polygon_attr"]>>24)&63) for p in shadows),"shadow_polygons":shadows}
    if include_all:result["all_polygons"]=polys
    return result

def compare(ds, native):
    if ds.get("schema")!=SCHEMA or native.get("schema")!=SCHEMA:
        raise ValueError("Shadow schema mismatch")
    fields=["model_kind","scale_fx12","opacity","matrix_fx12","scale_shift","num_bones","num_materials","num_display_list_groups","materials","display_lists"]
    expected=[n for n in ds["nodes"] if n["is_player"]]
    actual=[n for n in native["nodes"] if n["is_player"]]
    result={"checkpoint":ds.get("checkpoint"),"ds_node_count":len(ds["nodes"]),"native_node_count":len(native["nodes"]),
            "player_shadow_controls_equal":ds["player_shadow_controls"]==native["player_shadow_controls"],
            "player_shadow_present_ds":bool(expected),"player_shadow_present_native":bool(actual),"player_field_differences":{}}
    if len(expected)==1 and len(actual)==1:
        result["player_field_differences"]={f:{"ds":expected[0][f],"native":actual[0][f]} for f in fields if expected[0][f]!=actual[0][f]}
        result["player_source_fields_equal"]=not result["player_field_differences"]
    else:result["player_source_fields_equal"]=False
    signature=lambda n:json.dumps({f:n[f] for f in fields},sort_keys=True)
    result["all_shadow_source_nodes_equal"] = collections.Counter(map(signature,ds["nodes"]))==collections.Counter(map(signature,native["nodes"]))
    polygons=[t for t in native["triangles"] if ((t["polygon_attr"]>>4)&3)==3]
    result["native_raw_shadow_triangles"]=len(polygons)
    result["ds_culled_shadow_polygons"]=len(ds["shadow_polygons"])
    result["ds_shadow_attribute_set"]=sorted({f"{t['polygon_attr']:08x}" for t in ds["shadow_polygons"]})
    result["native_shadow_attribute_set"]=sorted({f"{t['polygon_attr']:08x}" for t in polygons})
    result["native_shadow_colors_black"]=bool(polygons) and all((v["color"]&0xffffff)==0 for t in polygons for v in t["vertices"])
    result["ds_shadow_colors_black"]=bool(ds["shadow_polygons"]) and all(v.get("final_color")==[0,0,0] for t in ds["shadow_polygons"] for v in t["vertices"])
    write_only={"04000350","04000354","04000540"}
    result["register_differences"]={a:{"ds":v,"native":native["registers"].get(a)} for a,v in ds["registers"].items() if a not in write_only and not a.startswith("render_") and native["registers"].get(a)!=v}
    result["write_only_register_reads_excluded"]=sorted(write_only)
    result["ds_renderer_state"]={a:v for a,v in ds["registers"].items() if a.startswith("render_")}
    # Raw native triangles precede clipping/culling; the DS polygons are final
    # culled GPU output. Counts are deliberately not treated as an equality test.
    return result

if __name__=="__main__":
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument("ram",type=Path);ap.add_argument("--polygons",type=Path);ap.add_argument("--native",type=Path,help="Compare a captured native shadow JSON with the DS RAM/polygons");ap.add_argument("--registers",type=Path);args=ap.parse_args()
    result=inspect_ram(args.ram)
    if args.polygons:result.update(inspect_polygons(args.polygons))
    if args.registers:
        result["registers"]={(a if a.startswith("render_") else f"{int(a,16):08x}"):int(v,16) for a,v in (line.split() for line in args.registers.read_text().splitlines())}
    if args.native:
        if not args.polygons or not args.registers:ap.error("Native comparison requires DS --polygons and --registers")
        result["checkpoint"]=args.ram.stem
        result=compare(result,json.loads(args.native.read_text()))
    print(json.dumps(result,indent=2))
